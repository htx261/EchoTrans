#pragma once

#include <QVector>
#include <QtGlobal>

struct TranscriptionAudioFrame {
  qint64 ptsMs = 0;
  int sampleRate = 0;
  int channelCount = 0;
  QVector<float> samples;
  bool endOfStream = false;
};
