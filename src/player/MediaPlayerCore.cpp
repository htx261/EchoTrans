#include "player/MediaPlayerCore.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

extern "C" {
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

namespace {
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
  lastDemuxError_.clear();
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
  lastDemuxError_.clear();

  workerThreads_.emplace_back(&MediaPlayerCore::demuxLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::placeholderWorkerLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::placeholderWorkerLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::placeholderWorkerLoop, this);
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
        queues->videoPackets.push(std::move(playbackPacket));
      } else if (streamIndex == audioStreamIndex && queues) {
        queues->audioPackets.push(std::move(playbackPacket));
      }

      av_packet_unref(packet);
    }
  }

  av_packet_free(&packet);
  avformat_close_input(&formatContext);
  demuxFinished_.store(true);
  waitUntilStopRequested();
  activeWorkerCount_.fetch_sub(1);
}

void MediaPlayerCore::placeholderWorkerLoop() {
  activeWorkerCount_.fetch_add(1);
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
