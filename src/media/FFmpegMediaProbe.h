#pragma once

#include "media/MediaInfo.h"

#include <QString>

class FFmpegMediaProbe {
public:
  static MediaProbeResult probe(const QString& filePath);
};
