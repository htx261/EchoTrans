#pragma once

#include <QtGlobal>

enum class VideoFrameDecision {
  Wait,
  Display,
  Drop
};

class VideoFrameSynchronizer {
public:
  static constexpr qint64 EarlyThresholdMs = 40;
  static constexpr qint64 LateThresholdMs = 120;

  static VideoFrameDecision decide(qint64 videoPtsMs, qint64 audioClockMs) {
    const qint64 deltaMs = videoPtsMs - audioClockMs;
    if (deltaMs > EarlyThresholdMs) {
      return VideoFrameDecision::Wait;
    }

    if (deltaMs < -LateThresholdMs) {
      return VideoFrameDecision::Drop;
    }

    return VideoFrameDecision::Display;
  }
};
