#include "player/MediaPlayerCore.h"

PlaybackState MediaPlayerCore::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

QString MediaPlayerCore::mediaPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mediaPath_;
}

bool MediaPlayerCore::open(const QString& filePath) {
  const QString trimmedPath = filePath.trimmed();
  if (trimmedPath.isEmpty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  mediaPath_ = trimmedPath;
  state_ = PlaybackState::Opening;
  return true;
}

bool MediaPlayerCore::play() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mediaPath_.isEmpty()) {
    return false;
  }

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
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = PlaybackState::Stopped;
  mediaPath_.clear();
}
