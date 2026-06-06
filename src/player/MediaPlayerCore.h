#pragma once

#include "player/PlaybackState.h"
#include "player/PlaybackQueues.h"

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class MediaPlayerCore {
public:
  MediaPlayerCore() = default;
  ~MediaPlayerCore();

  MediaPlayerCore(const MediaPlayerCore&) = delete;
  MediaPlayerCore& operator=(const MediaPlayerCore&) = delete;

  PlaybackState state() const;
  QString mediaPath() const;
  std::size_t activeWorkerCount() const;
  bool playbackQueuesClosed() const;
  bool demuxFinished() const;
  std::size_t demuxedAudioPacketCount() const;
  std::size_t demuxedVideoPacketCount() const;
  QString lastDemuxError() const;
  std::size_t audioPacketQueueSize() const;
  std::size_t videoPacketQueueSize() const;
  std::size_t audioFrameQueueSize() const;
  std::size_t decodedAudioFrameCount() const;
  std::size_t decodedAudioByteCount() const;
  std::size_t audioOutputByteCount() const;
  QString lastAudioDecodeError() const;
  QString lastAudioOutputError() const;

  bool open(const QString& filePath);
  bool play();
  bool pause();
  void stop();

private:
  void startWorkersLocked();
  void demuxLoop();
  void videoDecodePlaceholderLoop();
  void audioDecodeLoop();
  void audioOutputLoop();
  void waitUntilStopRequested();
  void setLastDemuxError(const QString& message);
  void setLastAudioDecodeError(const QString& message);
  void setLastAudioOutputError(const QString& message);

  mutable std::mutex mutex_;
  PlaybackState state_ = PlaybackState::Stopped;
  QString mediaPath_;
  QString lastDemuxError_;
  QString lastAudioDecodeError_;
  QString lastAudioOutputError_;
  std::unique_ptr<PlaybackQueues> playbackQueues_;

  std::atomic_bool stopRequested_{false};
  std::atomic_bool demuxFinished_{false};
  std::atomic_size_t demuxedAudioPacketCount_{0};
  std::atomic_size_t demuxedVideoPacketCount_{0};
  std::atomic_size_t decodedAudioFrameCount_{0};
  std::atomic_size_t decodedAudioByteCount_{0};
  std::atomic_size_t audioOutputByteCount_{0};
  std::atomic_size_t activeWorkerCount_{0};
  std::mutex workerMutex_;
  std::condition_variable workerCondition_;
  std::vector<std::thread> workerThreads_;
};
