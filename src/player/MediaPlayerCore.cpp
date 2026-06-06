#include "player/MediaPlayerCore.h"

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

bool MediaPlayerCore::open(const QString& filePath) {
  const QString trimmedPath = filePath.trimmed();
  if (trimmedPath.isEmpty()) {
    return false;
  }

  stop();

  std::lock_guard<std::mutex> lock(mutex_);
  mediaPath_ = trimmedPath;
  playbackQueues_ = std::make_unique<PlaybackQueues>();
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

  workerThreads_.emplace_back(&MediaPlayerCore::workerLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::workerLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::workerLoop, this);
  workerThreads_.emplace_back(&MediaPlayerCore::workerLoop, this);
}

void MediaPlayerCore::workerLoop() {
  activeWorkerCount_.fetch_add(1);

  std::unique_lock<std::mutex> lock(workerMutex_);
  workerCondition_.wait(lock, [this]() {
    return stopRequested_.load();
  });

  activeWorkerCount_.fetch_sub(1);
}
