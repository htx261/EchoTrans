#include "player/MediaPlayerCore.h"
#include "player/VideoFrameSynchronizer.h"

#include <QDir>
#include <QFileInfo>
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QIODevice>
#include <QThread>

#include <algorithm>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {
constexpr int OutputSampleRate = 48000;
constexpr int OutputChannelCount = 2;
constexpr int OutputBytesPerSample = 2;
constexpr AVSampleFormat OutputSampleFormat = AV_SAMPLE_FMT_S16;

QString ffmpegErrorToString(int errorCode) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(errorCode, buffer, sizeof(buffer));
  return QString::fromUtf8(buffer);
}

qint64 packetTimestampMs(const AVPacket* packet, const AVStream* stream) {
  if (!packet || !stream || packet->pts == AV_NOPTS_VALUE) {
    return 0;
  }

  return av_rescale_q(packet->pts, stream->time_base, AVRational{1, 1000});
}

qint64 packetDurationMs(const AVPacket* packet, const AVStream* stream) {
  if (!packet || !stream || packet->duration <= 0) {
    return 0;
  }

  return av_rescale_q(packet->duration, stream->time_base, AVRational{1, 1000});
}

QAudioFormat outputAudioFormat() {
  QAudioFormat format;
  format.setSampleRate(OutputSampleRate);
  format.setChannelCount(OutputChannelCount);
  format.setSampleSize(OutputBytesPerSample * 8);
  format.setCodec(QStringLiteral("audio/pcm"));
  format.setByteOrder(QAudioFormat::LittleEndian);
  format.setSampleType(QAudioFormat::SignedInt);
  return format;
}

bool receiveDecodedAudioFrames(
    AVCodecContext* codecContext,
    SwrContext* resampler,
    AVFrame* frame,
    PlaybackQueues* queues,
    std::atomic_size_t* decodedFrameCount,
    std::atomic_size_t* decodedByteCount,
    qint64 minimumPtsMs,
    QString* errorMessage) {
  while (true) {
    const int receiveResult = avcodec_receive_frame(codecContext, frame);
    if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
      return true;
    }

    if (receiveResult < 0) {
      *errorMessage = QStringLiteral("解码音频帧失败：%1").arg(ffmpegErrorToString(receiveResult));
      return false;
    }

    const int maxOutputSamples = static_cast<int>(av_rescale_rnd(
        swr_get_delay(resampler, codecContext->sample_rate) + frame->nb_samples,
        OutputSampleRate,
        codecContext->sample_rate,
        AV_ROUND_UP));
    const int maxBufferSize = av_samples_get_buffer_size(
        nullptr,
        OutputChannelCount,
        maxOutputSamples,
        OutputSampleFormat,
        1);

    if (maxBufferSize <= 0) {
      *errorMessage = QStringLiteral("计算音频输出缓冲区失败");
      return false;
    }

    PlaybackFrame playbackFrame;
    playbackFrame.ptsMs = frame->pts == AV_NOPTS_VALUE
        ? 0
        : av_rescale_q(frame->pts, codecContext->time_base, AVRational{1, 1000});

    if (playbackFrame.ptsMs < minimumPtsMs) {
      av_frame_unref(frame);
      continue;
    }

    playbackFrame.sampleRate = OutputSampleRate;
    playbackFrame.channelCount = OutputChannelCount;
    playbackFrame.bytesPerSample = OutputBytesPerSample;
    playbackFrame.pcmData.resize(maxBufferSize);

    uint8_t* outputData[1] = {
      reinterpret_cast<uint8_t*>(playbackFrame.pcmData.data())
    };

    const int convertedSamples = swr_convert(
        resampler,
        outputData,
        maxOutputSamples,
        const_cast<const uint8_t**>(frame->extended_data),
        frame->nb_samples);

    if (convertedSamples < 0) {
      *errorMessage = QStringLiteral("重采样音频帧失败：%1").arg(ffmpegErrorToString(convertedSamples));
      return false;
    }

    const int outputBufferSize = av_samples_get_buffer_size(
        nullptr,
        OutputChannelCount,
        convertedSamples,
        OutputSampleFormat,
        1);

    if (outputBufferSize <= 0) {
      *errorMessage = QStringLiteral("生成音频输出缓冲区失败");
      return false;
    }

    playbackFrame.pcmData.resize(outputBufferSize);
    if (!queues->audioFrames.push(std::move(playbackFrame))) {
      return false;
    }

    decodedFrameCount->fetch_add(1);
    decodedByteCount->fetch_add(static_cast<std::size_t>(outputBufferSize));
    av_frame_unref(frame);
  }
}

qint64 audioFrameDurationMs(const PlaybackFrame& frame) {
  const int bytesPerSecond = frame.sampleRate * frame.channelCount * frame.bytesPerSample;
  if (bytesPerSecond <= 0 || frame.pcmData.isEmpty()) {
    return 0;
  }

  return static_cast<qint64>(
      static_cast<double>(frame.pcmData.size()) * 1000.0 / static_cast<double>(bytesPerSecond));
}
}

void AvPacketDeleter::operator()(AVPacket* packet) const {
  av_packet_free(&packet);
}

MediaPlayerCore::~MediaPlayerCore() {
  stop();
}

PlaybackState MediaPlayerCore::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

QString MediaPlayerCore::mediaPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mediaPath_;
}

std::size_t MediaPlayerCore::activeWorkerCount() const {
  return activeWorkerCount_.load();
}

bool MediaPlayerCore::playbackQueuesClosed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !playbackQueues_ || playbackQueues_->isClosed();
}

bool MediaPlayerCore::demuxFinished() const {
  return demuxFinished_.load();
}

std::size_t MediaPlayerCore::demuxedAudioPacketCount() const {
  return demuxedAudioPacketCount_.load();
}

std::size_t MediaPlayerCore::demuxedVideoPacketCount() const {
  return demuxedVideoPacketCount_.load();
}

QString MediaPlayerCore::lastDemuxError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastDemuxError_;
}

std::size_t MediaPlayerCore::audioPacketQueueSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return playbackQueues_ ? playbackQueues_->audioPackets.size() : 0;
}

std::size_t MediaPlayerCore::videoPacketQueueSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return playbackQueues_ ? playbackQueues_->videoPackets.size() : 0;
}

std::size_t MediaPlayerCore::audioFrameQueueSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return playbackQueues_ ? playbackQueues_->audioFrames.size() : 0;
}

std::size_t MediaPlayerCore::decodedAudioFrameCount() const {
  return decodedAudioFrameCount_.load();
}

std::size_t MediaPlayerCore::decodedAudioByteCount() const {
  return decodedAudioByteCount_.load();
}

std::size_t MediaPlayerCore::audioOutputByteCount() const {
  return audioOutputByteCount_.load();
}

qint64 MediaPlayerCore::audioClockMs() const {
  return audioClockMs_.load();
}

QString MediaPlayerCore::lastAudioDecodeError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastAudioDecodeError_;
}

QString MediaPlayerCore::lastAudioOutputError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastAudioOutputError_;
}

std::size_t MediaPlayerCore::decodedVideoFrameCount() const {
  return decodedVideoFrameCount_.load();
}

qint64 MediaPlayerCore::lastPublishedVideoPtsMs() const {
  return lastPublishedVideoPtsMs_.load();
}

bool MediaPlayerCore::takeVideoFrame(QImage* image) {
  if (!image) {
    return false;
  }

  PlaybackQueues* queues = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queues = playbackQueues_.get();
  }

  if (!queues) {
    return false;
  }

  PlaybackFrame frame;
  if (!queues->videoFrames.tryPop(frame) || frame.endOfStream || frame.image.isNull()) {
    return false;
  }

  *image = frame.image;
  return true;
}

void MediaPlayerCore::setVideoFrameCallback(std::function<void(const QImage&, qint64)> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  videoFrameCallback_ = std::move(callback);
}

QString MediaPlayerCore::lastVideoDecodeError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastVideoDecodeError_;
}

bool MediaPlayerCore::open(const QString& filePath) {
  const QString trimmedPath = filePath.trimmed();
  if (trimmedPath.isEmpty()) {
    return false;
  }

  stop();

  std::lock_guard<std::mutex> lock(mutex_);
  mediaPath_ = trimmedPath;
  playbackQueues_ = std::make_unique<PlaybackQueues>();
  startPositionMs_.store(0);
  seekInProgress_.store(false);
  resetPlaybackCounters(0);
  lastDemuxError_.clear();
  lastAudioDecodeError_.clear();
  lastAudioOutputError_.clear();
  lastVideoDecodeError_.clear();
  state_ = PlaybackState::Opening;
  return true;
}

bool MediaPlayerCore::play() {
  if (state() == PlaybackState::Paused) {
    return resume();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (mediaPath_.isEmpty()) {
    return false;
  }

  startWorkersLocked();
  state_ = PlaybackState::Playing;
  return true;
}

bool MediaPlayerCore::pause() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != PlaybackState::Playing) {
      return false;
    }

    pauseAcknowledged_.store(false);
    paused_.store(true);
    state_ = PlaybackState::Paused;
  }

  std::unique_lock<std::mutex> acknowledgedLock(pauseAcknowledgedMutex_);
  pauseAcknowledgedCondition_.wait_for(acknowledgedLock, std::chrono::milliseconds(300), [this]() {
    return pauseAcknowledged_.load() || stopRequested_.load();
  });
  return true;
}

bool MediaPlayerCore::resume() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != PlaybackState::Paused) {
      return false;
    }

    paused_.store(false);
    pauseAcknowledged_.store(false);
    if (activeWorkerCount_.load() == 0) {
      for (std::thread& worker : workerThreads_) {
        if (worker.joinable()) {
          worker.join();
        }
      }
      workerThreads_.clear();
      playbackQueues_ = std::make_unique<PlaybackQueues>();
      startWorkersLocked();
    }
    state_ = PlaybackState::Playing;
  }

  pauseCondition_.notify_all();
  return true;
}

bool MediaPlayerCore::seekTo(qint64 positionMs) {
  if (positionMs < 0 || mediaPath().isEmpty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(seekThreadMutex_);
  if (seekInProgress_.load()) {
    return false;
  }

  if (seekThread_.joinable()) {
    seekThread_.join();
  }

  seekInProgress_.store(true);
  seekThread_ = std::thread(&MediaPlayerCore::performSeek, this, positionMs);
  return true;
}

bool MediaPlayerCore::seekInProgress() const {
  return seekInProgress_.load();
}

void MediaPlayerCore::stop() {
  std::unique_lock<std::mutex> controlLock(controlMutex_);
  stopWorkers(true);
  controlLock.unlock();

  std::lock_guard<std::mutex> seekLock(seekThreadMutex_);
  if (seekThread_.joinable()
      && seekThread_.get_id() != std::this_thread::get_id()) {
    seekThread_.join();
  }
}

void MediaPlayerCore::resetPlaybackCounters(qint64 clockMs) {
  demuxFinished_.store(false);
  demuxedAudioPacketCount_.store(0);
  demuxedVideoPacketCount_.store(0);
  decodedAudioFrameCount_.store(0);
  decodedAudioByteCount_.store(0);
  audioOutputByteCount_.store(0);
  audioClockMs_.store(clockMs);
  decodedVideoFrameCount_.store(0);
  lastPublishedVideoPtsMs_.store(-1);
}

void MediaPlayerCore::stopWorkers(bool clearMediaPath) {
  std::vector<std::thread> workersToJoin;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != PlaybackState::Stopped || !workerThreads_.empty()) {
      state_ = PlaybackState::Stopping;
    }

    stopRequested_.store(true);
    paused_.store(false);
    pauseAcknowledged_.store(false);
    if (playbackQueues_) {
      playbackQueues_->closeAll();
    }

    workersToJoin.swap(workerThreads_);
  }

  workerCondition_.notify_all();
  pauseCondition_.notify_all();
  pauseAcknowledgedCondition_.notify_all();

  for (std::thread& worker : workersToJoin) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  state_ = PlaybackState::Stopped;
  if (clearMediaPath) {
    mediaPath_.clear();
  }
}

void MediaPlayerCore::performSeek(qint64 positionMs) {
  const QString path = mediaPath();
  const bool resumeAfterSeek = state() == PlaybackState::Playing;

  {
    std::lock_guard<std::mutex> controlLock(controlMutex_);
    stopWorkers(false);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      mediaPath_ = path;
      playbackQueues_ = std::make_unique<PlaybackQueues>();
      startPositionMs_.store(positionMs);
      resetPlaybackCounters(positionMs);
      lastDemuxError_.clear();
      lastAudioDecodeError_.clear();
      lastAudioOutputError_.clear();
      lastVideoDecodeError_.clear();
      state_ = PlaybackState::Opening;
    }

    if (resumeAfterSeek) {
      play();
    } else {
      state_ = PlaybackState::Paused;
      paused_.store(true);
    }
  }

  seekInProgress_.store(false);
}

void MediaPlayerCore::startWorkersLocked() {
  if (!workerThreads_.empty()) {
    return;
  }

  stopRequested_.store(false);
  if (!playbackQueues_ || playbackQueues_->isClosed()) {
    playbackQueues_ = std::make_unique<PlaybackQueues>();
  }

  resetPlaybackCounters(startPositionMs_.load());
  lastDemuxError_.clear();
  lastAudioDecodeError_.clear();
  lastAudioOutputError_.clear();
  lastVideoDecodeError_.clear();

  workerThreads_.emplace_back(&MediaPlayerCore::demuxLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::videoDecodeLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::audioDecodeLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::audioOutputLoop, this);
}

void MediaPlayerCore::demuxLoop() {
  activeWorkerCount_.fetch_add(1);

  QString path;
  PlaybackQueues* queues = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    path = mediaPath_;
    queues = playbackQueues_.get();
  }

  AVFormatContext* formatContext = nullptr;
  const QString absolutePath = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
  const QByteArray encodedPath = absolutePath.toUtf8();

  int result = avformat_open_input(&formatContext, encodedPath.constData(), nullptr, nullptr);
  if (result < 0) {
    setLastDemuxError(QStringLiteral("打开媒体文件失败：%1").arg(ffmpegErrorToString(result)));
    demuxFinished_.store(true);
    finishWorker();
    return;
  }

  result = avformat_find_stream_info(formatContext, nullptr);
  if (result < 0) {
    setLastDemuxError(QStringLiteral("读取媒体流信息失败：%1").arg(ffmpegErrorToString(result)));
    avformat_close_input(&formatContext);
    demuxFinished_.store(true);
    finishWorker();
    return;
  }

  const int videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const int audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  const qint64 seekPositionMs = startPositionMs_.load();
  if (seekPositionMs > 0) {
    av_seek_frame(formatContext, -1, seekPositionMs * AV_TIME_BASE / 1000, AVSEEK_FLAG_BACKWARD);
  }
  AVPacket* packet = av_packet_alloc();

  if (!packet) {
    setLastDemuxError(QStringLiteral("创建 FFmpeg 包失败"));
  } else {
    while (!stopRequested_.load()) {
      waitWhilePaused();
      if (stopRequested_.load()) {
        break;
      }

      result = av_read_frame(formatContext, packet);
      if (result < 0) {
        if (result != AVERROR_EOF) {
          setLastDemuxError(QStringLiteral("读取媒体包失败：%1").arg(ffmpegErrorToString(result)));
        }
        break;
      }

      const int streamIndex = packet->stream_index;
      AVStream* stream = formatContext->streams[streamIndex];
      PlaybackPacket playbackPacket;
      playbackPacket.streamIndex = streamIndex;
      playbackPacket.ptsMs = packetTimestampMs(packet, stream);
      playbackPacket.durationMs = packetDurationMs(packet, stream);
      playbackPacket.packet = AvPacketPtr(av_packet_clone(packet), AvPacketDeleter{});

      if (!playbackPacket.packet) {
        setLastDemuxError(QStringLiteral("复制 FFmpeg 包失败"));
        av_packet_unref(packet);
        break;
      }

      if (streamIndex == videoStreamIndex && queues) {
        if (queues->videoPackets.push(std::move(playbackPacket))) {
          demuxedVideoPacketCount_.fetch_add(1);
        }
      } else if (streamIndex == audioStreamIndex && queues) {
        if (queues->audioPackets.push(std::move(playbackPacket))) {
          demuxedAudioPacketCount_.fetch_add(1);
        }
      }

      av_packet_unref(packet);
    }
  }

  av_packet_free(&packet);
  avformat_close_input(&formatContext);
  if (queues) {
    PlaybackPacket audioEnd;
    audioEnd.endOfStream = true;
    queues->audioPackets.push(std::move(audioEnd));

    PlaybackPacket videoEnd;
    videoEnd.endOfStream = true;
    queues->videoPackets.push(std::move(videoEnd));
  }
  demuxFinished_.store(true);
  finishWorker();
}

void MediaPlayerCore::videoDecodeLoop() {
  activeWorkerCount_.fetch_add(1);

  QString path;
  PlaybackQueues* queues = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    path = mediaPath_;
    queues = playbackQueues_.get();
  }

  if (!queues) {
    setLastVideoDecodeError(QStringLiteral("视频队列未初始化"));
    finishWorker();
    return;
  }

  AVFormatContext* formatContext = nullptr;
  const QString absolutePath = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
  const QByteArray encodedPath = absolutePath.toUtf8();

  int result = avformat_open_input(&formatContext, encodedPath.constData(), nullptr, nullptr);
  if (result < 0) {
    setLastVideoDecodeError(QStringLiteral("打开视频解码输入失败：%1").arg(ffmpegErrorToString(result)));
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->videoFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  result = avformat_find_stream_info(formatContext, nullptr);
  if (result < 0) {
    setLastVideoDecodeError(QStringLiteral("读取视频流信息失败：%1").arg(ffmpegErrorToString(result)));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->videoFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  const int videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (videoStreamIndex < 0) {
    setLastVideoDecodeError(QStringLiteral("未找到视频流"));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->videoFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  AVStream* videoStream = formatContext->streams[videoStreamIndex];
  const qint64 minimumPtsMs = startPositionMs_.load();
  const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
  if (!codec) {
    setLastVideoDecodeError(QStringLiteral("未找到视频解码器"));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->videoFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  AVCodecContext* codecContext = avcodec_alloc_context3(codec);
  if (!codecContext) {
    setLastVideoDecodeError(QStringLiteral("创建视频解码上下文失败"));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->videoFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  result = avcodec_parameters_to_context(codecContext, videoStream->codecpar);
  if (result < 0) {
    setLastVideoDecodeError(QStringLiteral("复制视频解码参数失败：%1").arg(ffmpegErrorToString(result)));
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->videoFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  codecContext->time_base = videoStream->time_base;
  result = avcodec_open2(codecContext, codec, nullptr);
  if (result < 0) {
    setLastVideoDecodeError(QStringLiteral("打开视频解码器失败：%1").arg(ffmpegErrorToString(result)));
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->videoFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  SwsContext* scaler = sws_getContext(
      codecContext->width,
      codecContext->height,
      codecContext->pix_fmt,
      codecContext->width,
      codecContext->height,
      AV_PIX_FMT_BGRA,
      SWS_BILINEAR,
      nullptr,
      nullptr,
      nullptr);

  AVFrame* frame = av_frame_alloc();
  if (!scaler || !frame) {
    setLastVideoDecodeError(QStringLiteral("创建视频转换缓冲失败"));
  } else {
    PlaybackPacket packet;
    while (!stopRequested_.load() && queues->videoPackets.waitPop(packet)) {
      waitWhilePaused();
      if (stopRequested_.load()) {
        break;
      }

      const int sendResult = packet.endOfStream
          ? avcodec_send_packet(codecContext, nullptr)
          : avcodec_send_packet(codecContext, packet.packet.get());

      if (sendResult < 0 && sendResult != AVERROR_EOF) {
        setLastVideoDecodeError(QStringLiteral("发送视频包到解码器失败：%1").arg(ffmpegErrorToString(sendResult)));
        break;
      }

      while (true) {
        const int receiveResult = avcodec_receive_frame(codecContext, frame);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
          break;
        }

        if (receiveResult < 0) {
          setLastVideoDecodeError(QStringLiteral("解码视频帧失败：%1").arg(ffmpegErrorToString(receiveResult)));
          break;
        }

        QByteArray imageBytes(codecContext->width * codecContext->height * 4, '\0');
        uint8_t* outputData[1] = {
          reinterpret_cast<uint8_t*>(imageBytes.data())
        };
        int outputLineSize[1] = {
          codecContext->width * 4
        };

        sws_scale(
            scaler,
            frame->data,
            frame->linesize,
            0,
            codecContext->height,
            outputData,
            outputLineSize);

        QImage image(
            reinterpret_cast<const uchar*>(imageBytes.constData()),
            codecContext->width,
            codecContext->height,
            outputLineSize[0],
            QImage::Format_RGB32);

        PlaybackFrame playbackFrame;
        playbackFrame.ptsMs = frame->pts == AV_NOPTS_VALUE
            ? packet.ptsMs
            : av_rescale_q(frame->pts, videoStream->time_base, AVRational{1, 1000});

        if (playbackFrame.ptsMs < minimumPtsMs) {
          av_frame_unref(frame);
          continue;
        }

        playbackFrame.image = image.copy();

        if (!queues->videoFrames.push(std::move(playbackFrame))) {
          av_frame_unref(frame);
          break;
        }

        decodedVideoFrameCount_.fetch_add(1);
        av_frame_unref(frame);
      }

      if (packet.endOfStream) {
        break;
      }
    }
  }

  av_frame_free(&frame);
  if (scaler) {
    sws_freeContext(scaler);
  }
  avcodec_free_context(&codecContext);
  avformat_close_input(&formatContext);

  PlaybackFrame endFrame;
  endFrame.endOfStream = true;
  queues->videoFrames.push(std::move(endFrame));

  finishWorker();
}

void MediaPlayerCore::audioDecodeLoop() {
  activeWorkerCount_.fetch_add(1);

  QString path;
  PlaybackQueues* queues = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    path = mediaPath_;
    queues = playbackQueues_.get();
  }

  if (!queues) {
    setLastAudioDecodeError(QStringLiteral("音频队列未初始化"));
    finishWorker();
    return;
  }

  AVFormatContext* formatContext = nullptr;
  const QString absolutePath = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
  const QByteArray encodedPath = absolutePath.toUtf8();

  int result = avformat_open_input(&formatContext, encodedPath.constData(), nullptr, nullptr);
  if (result < 0) {
    setLastAudioDecodeError(QStringLiteral("打开音频解码输入失败：%1").arg(ffmpegErrorToString(result)));
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  result = avformat_find_stream_info(formatContext, nullptr);
  if (result < 0) {
    setLastAudioDecodeError(QStringLiteral("读取音频流信息失败：%1").arg(ffmpegErrorToString(result)));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  const int audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (audioStreamIndex < 0) {
    setLastAudioDecodeError(QStringLiteral("未找到音频流"));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  AVStream* audioStream = formatContext->streams[audioStreamIndex];
  const AVCodec* codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
  if (!codec) {
    setLastAudioDecodeError(QStringLiteral("未找到音频解码器"));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  AVCodecContext* codecContext = avcodec_alloc_context3(codec);
  if (!codecContext) {
    setLastAudioDecodeError(QStringLiteral("创建音频解码上下文失败"));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  result = avcodec_parameters_to_context(codecContext, audioStream->codecpar);
  if (result < 0) {
    setLastAudioDecodeError(QStringLiteral("复制音频解码参数失败：%1").arg(ffmpegErrorToString(result)));
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  codecContext->time_base = audioStream->time_base;
  result = avcodec_open2(codecContext, codec, nullptr);
  if (result < 0) {
    setLastAudioDecodeError(QStringLiteral("打开音频解码器失败：%1").arg(ffmpegErrorToString(result)));
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  AVChannelLayout outputLayout;
  av_channel_layout_default(&outputLayout, OutputChannelCount);

  SwrContext* resampler = nullptr;
  result = swr_alloc_set_opts2(
      &resampler,
      &outputLayout,
      OutputSampleFormat,
      OutputSampleRate,
      &codecContext->ch_layout,
      codecContext->sample_fmt,
      codecContext->sample_rate,
      0,
      nullptr);

  if (result >= 0) {
    result = swr_init(resampler);
  }

  if (result < 0) {
    setLastAudioDecodeError(QStringLiteral("初始化音频重采样失败：%1").arg(ffmpegErrorToString(result)));
    swr_free(&resampler);
    av_channel_layout_uninit(&outputLayout);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    finishWorker();
    return;
  }

  AVFrame* frame = av_frame_alloc();
  const qint64 minimumPtsMs = startPositionMs_.load();
  if (!frame) {
    setLastAudioDecodeError(QStringLiteral("创建音频帧失败"));
  } else {
    PlaybackPacket packet;
    while (!stopRequested_.load() && queues->audioPackets.waitPop(packet)) {
      waitWhilePaused();
      if (stopRequested_.load()) {
        break;
      }

      const int sendResult = packet.endOfStream
          ? avcodec_send_packet(codecContext, nullptr)
          : avcodec_send_packet(codecContext, packet.packet.get());

      if (sendResult < 0 && sendResult != AVERROR_EOF) {
        setLastAudioDecodeError(QStringLiteral("发送音频包到解码器失败：%1").arg(ffmpegErrorToString(sendResult)));
        break;
      }

      QString decodeError;
      if (!receiveDecodedAudioFrames(
              codecContext,
              resampler,
              frame,
              queues,
              &decodedAudioFrameCount_,
              &decodedAudioByteCount_,
              minimumPtsMs,
              &decodeError)) {
        if (!decodeError.isEmpty()) {
          setLastAudioDecodeError(decodeError);
        }
        break;
      }

      if (packet.endOfStream) {
        break;
      }
    }
  }

  av_frame_free(&frame);
  swr_free(&resampler);
  av_channel_layout_uninit(&outputLayout);
  avcodec_free_context(&codecContext);
  avformat_close_input(&formatContext);

  PlaybackFrame endFrame;
  endFrame.endOfStream = true;
  queues->audioFrames.push(std::move(endFrame));

  finishWorker();
}

void MediaPlayerCore::audioOutputLoop() {
  activeWorkerCount_.fetch_add(1);

  PlaybackQueues* queues = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queues = playbackQueues_.get();
  }

  if (!queues) {
    setLastAudioOutputError(QStringLiteral("音频帧队列未初始化"));
    finishWorker();
    return;
  }

  const QAudioDeviceInfo outputDeviceInfo = QAudioDeviceInfo::defaultOutputDevice();
  QAudioOutput* audioOutput = nullptr;
  QIODevice* audioDevice = nullptr;

  if (outputDeviceInfo.isNull()) {
    setLastAudioOutputError(QStringLiteral("未找到默认音频输出设备"));
  } else {
    QAudioFormat format = outputAudioFormat();
    if (!outputDeviceInfo.isFormatSupported(format)) {
      format = outputDeviceInfo.nearestFormat(format);
    }

    audioOutput = new QAudioOutput(outputDeviceInfo, format);
    audioDevice = audioOutput->start();
    if (!audioDevice) {
      setLastAudioOutputError(QStringLiteral("启动 Qt 音频输出失败"));
      delete audioOutput;
      audioOutput = nullptr;
    }
  }

  PlaybackFrame frame;
  PlaybackFrame pendingVideoFrame;
  bool hasAudioClockBase = false;
  qint64 audioClockBaseMs = 0;
  qint64 lastAudioFrameEndMs = audioClockMs_.load();
  auto updateOutputClock = [&]() {
    if (!audioOutput || !hasAudioClockBase) {
      return;
    }

    const qint64 clockMs = audioClockBaseMs + audioOutput->processedUSecs() / 1000;
    audioClockMs_.store(clockMs);
    publishVideoFramesForClock(clockMs, &pendingVideoFrame);
  };

  while (!stopRequested_.load() && queues->audioFrames.waitPop(frame)) {
    waitWhilePaused();
    if (stopRequested_.load()) {
      break;
    }

    if (frame.endOfStream) {
      break;
    }

    if (!audioDevice) {
      const qint64 simulatedClockMs = frame.ptsMs + audioFrameDurationMs(frame);
      audioClockMs_.store(simulatedClockMs);
      publishVideoFramesForClock(simulatedClockMs, &pendingVideoFrame);
      continue;
    }

    if (!hasAudioClockBase) {
      hasAudioClockBase = true;
      audioClockBaseMs = frame.ptsMs;
    }
    lastAudioFrameEndMs = frame.ptsMs + audioFrameDurationMs(frame);

    qsizetype offset = 0;
    while (offset < frame.pcmData.size() && !stopRequested_.load()) {
      waitWhilePaused();
      if (stopRequested_.load()) {
        break;
      }

      const int writableBytes = audioOutput->bytesFree();
      if (writableBytes <= 0) {
        updateOutputClock();
        QThread::msleep(5);
        continue;
      }

      const qsizetype bytesToWrite = std::min<qsizetype>(
          writableBytes,
          frame.pcmData.size() - offset);
      const qint64 written = audioDevice->write(frame.pcmData.constData() + offset, bytesToWrite);
      if (written <= 0) {
        QThread::msleep(5);
        continue;
      }

      offset += written;
      audioOutputByteCount_.fetch_add(static_cast<std::size_t>(written));
      updateOutputClock();
    }
  }

  QElapsedTimer drainTimer;
  drainTimer.start();
  while (audioOutput
      && hasAudioClockBase
      && !stopRequested_.load()
      && audioClockMs_.load() < lastAudioFrameEndMs
      && drainTimer.elapsed() < 1000) {
    updateOutputClock();
    QThread::msleep(10);
  }

  if (hasAudioClockBase && !stopRequested_.load()) {
    audioClockMs_.store(lastAudioFrameEndMs);
    publishVideoFramesForClock(lastAudioFrameEndMs, &pendingVideoFrame);
  }

  if (audioOutput) {
    audioOutput->stop();
    delete audioOutput;
  }

  finishWorker();
}

void MediaPlayerCore::publishVideoFramesForClock(qint64 audioClockMs, PlaybackFrame* pendingVideoFrame) {
  PlaybackQueues* queues = nullptr;
  std::function<void(const QImage&, qint64)> callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queues = playbackQueues_.get();
    callback = videoFrameCallback_;
  }

  if (!queues || !callback) {
    return;
  }

  auto publishFrame = [&](const PlaybackFrame& frame) {
    if (!frame.image.isNull()) {
      lastPublishedVideoPtsMs_.store(frame.ptsMs);
      callback(frame.image, audioClockMs);
    }
  };

  if (pendingVideoFrame && !pendingVideoFrame->image.isNull()) {
    const VideoFrameDecision decision = VideoFrameSynchronizer::decide(pendingVideoFrame->ptsMs, audioClockMs);
    if (decision == VideoFrameDecision::Wait) {
      return;
    }

    if (decision == VideoFrameDecision::Display) {
      publishFrame(*pendingVideoFrame);
    }

    *pendingVideoFrame = PlaybackFrame();
  }

  PlaybackFrame frame;
  while (queues->videoFrames.tryPop(frame)) {
    if (frame.endOfStream) {
      return;
    }

    const VideoFrameDecision decision = VideoFrameSynchronizer::decide(frame.ptsMs, audioClockMs);
    if (decision == VideoFrameDecision::Wait) {
      if (pendingVideoFrame) {
        *pendingVideoFrame = std::move(frame);
      }
      return;
    }

    if (decision == VideoFrameDecision::Display) {
      publishFrame(frame);
    }
  }
}

void MediaPlayerCore::waitUntilStopRequested() {
  std::unique_lock<std::mutex> lock(workerMutex_);
  workerCondition_.wait(lock, [this]() {
    return stopRequested_.load();
  });
}

void MediaPlayerCore::finishWorker() {
  const std::size_t previousCount = activeWorkerCount_.fetch_sub(1);
  if (previousCount <= 1 && !stopRequested_.load()) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == PlaybackState::Opening
        || state_ == PlaybackState::Playing
        || state_ == PlaybackState::Paused) {
      if (!lastDemuxError_.isEmpty()) {
        state_ = PlaybackState::Stopped;
      } else {
        startPositionMs_.store(0);
        resetPlaybackCounters(0);
        paused_.store(true);
        pauseAcknowledged_.store(false);
        state_ = PlaybackState::Paused;
      }
    }
  }

  workerCondition_.notify_all();
}

void MediaPlayerCore::waitWhilePaused() {
  std::unique_lock<std::mutex> lock(pauseMutex_);
  if (paused_.load() && !stopRequested_.load()) {
    pauseAcknowledged_.store(true);
    pauseAcknowledgedCondition_.notify_all();
  }

  pauseCondition_.wait(lock, [this]() {
    return !paused_.load() || stopRequested_.load();
  });
}

void MediaPlayerCore::setLastDemuxError(const QString& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  lastDemuxError_ = message;
}

void MediaPlayerCore::setLastAudioDecodeError(const QString& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  lastAudioDecodeError_ = message;
}

void MediaPlayerCore::setLastAudioOutputError(const QString& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  lastAudioOutputError_ = message;
}

void MediaPlayerCore::setLastVideoDecodeError(const QString& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  lastVideoDecodeError_ = message;
}
