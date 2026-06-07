#pragma once

#include <QVector>
#include <QtGlobal>

struct TranscriptionAudioPreprocessorOptions {
  int targetSampleRate = 16000;
  int targetChannelCount = 1;
  int segmentWindowMs = 10000;
  int segmentOverlapMs = 0;
};

struct TranscriptionAudioChunk {
  qint64 startPtsMs = 0;
  int sampleRate = 16000;
  int channelCount = 1;
  QVector<float> samples;
  bool endOfStream = false;
};

class TranscriptionAudioPreprocessor {
public:
  explicit TranscriptionAudioPreprocessor(
      const TranscriptionAudioPreprocessorOptions& options = {});

  void appendFrame(
      qint64 ptsMs,
      int sampleRate,
      int channelCount,
      const QVector<float>& interleavedSamples);
  void appendEndOfStream();
  QVector<TranscriptionAudioChunk> takeReadyChunks();
  void reset();

private:
  int segmentSampleCount() const;
  QVector<float> normalizeToTarget(
      int sampleRate,
      int channelCount,
      const QVector<float>& interleavedSamples) const;
  void emitReadySegments();
  qint64 samplesToMs(int sampleCount) const;

  TranscriptionAudioPreprocessorOptions options_;
  QVector<float> pendingSamples_;
  QVector<TranscriptionAudioChunk> readyChunks_;
  qint64 pendingStartPtsMs_ = 0;
  bool hasPendingStartPts_ = false;
};
