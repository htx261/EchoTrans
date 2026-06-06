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

  bool open(const QString& filePath);
  bool play();
  bool pause();
  void stop();

private:
  void startWorkersLocked();
  void workerLoop();

  mutable std::mutex mutex_;
  PlaybackState state_ = PlaybackState::Stopped;
  QString mediaPath_;
  std::unique_ptr<PlaybackQueues> playbackQueues_;

  std::atomic_bool stopRequested_{false};
  std::atomic_size_t activeWorkerCount_{0};
  std::mutex workerMutex_;
  std::condition_variable workerCondition_;
  std::vector<std::thread> workerThreads_;
};
