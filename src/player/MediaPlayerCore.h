#pragma once

#include "player/PlaybackState.h"

#include <QString>

#include <mutex>

class MediaPlayerCore {
public:
  PlaybackState state() const;
  QString mediaPath() const;

  bool open(const QString& filePath);
  bool play();
  bool pause();
  void stop();

private:
  mutable std::mutex mutex_;
  PlaybackState state_ = PlaybackState::Stopped;
  QString mediaPath_;
};
