#include "player/MediaPlayerCore.h"

#include <QDir>
#include <QFileInfo>
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QIODevice>
#include <QThread>

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

QString MediaPlayerCore::lastAudioDecodeError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastAudioDecodeError_;
}

QString MediaPlayerCore::lastAudioOutputError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastAudioOutputError_;
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
  demuxFinished_.store(false);
  demuxedAudioPacketCount_.store(0);
  demuxedVideoPacketCount_.store(0);
  decodedAudioFrameCount_.store(0);
  decodedAudioByteCount_.store(0);
  audioOutputByteCount_.store(0);
  lastDemuxError_.clear();
  lastAudioDecodeError_.clear();
  lastAudioOutputError_.clear();
  state_ = PlaybackState::Opening;
  return true;
}

bool MediaPlayerCore::play() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mediaPath_.isEmpty()) {
    return false;
  }

  startWorkersLocked();
  state_ = PlaybackState::Playing;
  return true;
}

bool MediaPlayerCore::pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != PlaybackState::Playing) {
    return false;
  }

  state_ = PlaybackState::Paused;
  return true;
}

void MediaPlayerCore::stop() {
  std::vector<std::thread> workersToJoin;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != PlaybackState::Stopped || !workerThreads_.empty()) {
      state_ = PlaybackState::Stopping;
    }

    stopRequested_.store(true);
    if (playbackQueues_) {
      playbackQueues_->closeAll();
    }

    workersToJoin.swap(workerThreads_);
  }

  workerCondition_.notify_all();

  for (std::thread& worker : workersToJoin) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  state_ = PlaybackState::Stopped;
  mediaPath_.clear();
}

void MediaPlayerCore::startWorkersLocked() {
  if (!workerThreads_.empty()) {
    return;
  }

  stopRequested_.store(false);
  if (!playbackQueues_ || playbackQueues_->isClosed()) {
    playbackQueues_ = std::make_unique<PlaybackQueues>();
  }

  demuxFinished_.store(false);
  demuxedAudioPacketCount_.store(0);
  demuxedVideoPacketCount_.store(0);
  decodedAudioFrameCount_.store(0);
  decodedAudioByteCount_.store(0);
  audioOutputByteCount_.store(0);
  lastDemuxError_.clear();
  lastAudioDecodeError_.clear();
  lastAudioOutputError_.clear();

  workerThreads_.emplace_back(&MediaPlayerCore::demuxLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::videoDecodePlaceholderLoop, this);
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
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
    return;
  }

  result = avformat_find_stream_info(formatContext, nullptr);
  if (result < 0) {
    setLastDemuxError(QStringLiteral("读取媒体流信息失败：%1").arg(ffmpegErrorToString(result)));
    avformat_close_input(&formatContext);
    demuxFinished_.store(true);
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
    return;
  }

  const int videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const int audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  AVPacket* packet = av_packet_alloc();

  if (!packet) {
    setLastDemuxError(QStringLiteral("创建 FFmpeg 包失败"));
  } else {
    while (!stopRequested_.load()) {
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
  waitUntilStopRequested();
  activeWorkerCount_.fetch_sub(1);
}

void MediaPlayerCore::videoDecodePlaceholderLoop() {
  activeWorkerCount_.fetch_add(1);

  PlaybackQueues* queues = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queues = playbackQueues_.get();
  }

  if (queues) {
    PlaybackPacket packet;
    while (!stopRequested_.load() && queues->videoPackets.waitPop(packet)) {
      if (packet.endOfStream) {
        break;
      }
    }
  }

  waitUntilStopRequested();
  activeWorkerCount_.fetch_sub(1);
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
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
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
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
    return;
  }

  result = avformat_find_stream_info(formatContext, nullptr);
  if (result < 0) {
    setLastAudioDecodeError(QStringLiteral("读取音频流信息失败：%1").arg(ffmpegErrorToString(result)));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
    return;
  }

  const int audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (audioStreamIndex < 0) {
    setLastAudioDecodeError(QStringLiteral("未找到音频流"));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
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
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
    return;
  }

  AVCodecContext* codecContext = avcodec_alloc_context3(codec);
  if (!codecContext) {
    setLastAudioDecodeError(QStringLiteral("创建音频解码上下文失败"));
    avformat_close_input(&formatContext);
    PlaybackFrame endFrame;
    endFrame.endOfStream = true;
    queues->audioFrames.push(std::move(endFrame));
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
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
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
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
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
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
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
    return;
  }

  AVFrame* frame = av_frame_alloc();
  if (!frame) {
    setLastAudioDecodeError(QStringLiteral("创建音频帧失败"));
  } else {
    PlaybackPacket packet;
    while (!stopRequested_.load() && queues->audioPackets.waitPop(packet)) {
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

  waitUntilStopRequested();
  activeWorkerCount_.fetch_sub(1);
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
    waitUntilStopRequested();
    activeWorkerCount_.fetch_sub(1);
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
  while (!stopRequested_.load() && queues->audioFrames.waitPop(frame)) {
    if (frame.endOfStream) {
      break;
    }

    if (!audioDevice) {
      continue;
    }

    qsizetype offset = 0;
    while (offset < frame.pcmData.size() && !stopRequested_.load()) {
      const int writableBytes = audioOutput->bytesFree();
      if (writableBytes <= 0) {
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
    }
  }

  if (audioOutput) {
    audioOutput->stop();
    delete audioOutput;
  }

  waitUntilStopRequested();
  activeWorkerCount_.fetch_sub(1);
}

void MediaPlayerCore::waitUntilStopRequested() {
  std::unique_lock<std::mutex> lock(workerMutex_);
  workerCondition_.wait(lock, [this]() {
    return stopRequested_.load();
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
