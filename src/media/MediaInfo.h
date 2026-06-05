#pragma once

#include <QString>

struct MediaInfo {
  QString filePath;
  qint64 durationMs = 0;
  bool hasVideo = false;
  bool hasAudio = false;
  int videoStreamIndex = -1;
  int audioStreamIndex = -1;
};

struct MediaProbeResult {
  bool success = false;
  QString errorMessage;
  MediaInfo info;
};
