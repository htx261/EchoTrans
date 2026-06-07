#include "transcription/TranscriptionAudioPreprocessor.h"

#include <algorithm>
#include <cmath>

TranscriptionAudioPreprocessor::TranscriptionAudioPreprocessor(
    const TranscriptionAudioPreprocessorOptions& options)
    : options_(options) {
  if (options_.targetSampleRate <= 0) {
    options_.targetSampleRate = 16000;
  }
  if (options_.targetChannelCount <= 0) {
    options_.targetChannelCount = 1;
  }
  if (options_.segmentWindowMs <= 0) {
    options_.segmentWindowMs = 2000;
  }
  if (options_.segmentOverlapMs < 0) {
    options_.segmentOverlapMs = 0;
  }
  if (options_.segmentOverlapMs >= options_.segmentWindowMs) {
    options_.segmentOverlapMs = options_.segmentWindowMs - 1;
  }
}

void TranscriptionAudioPreprocessor::appendFrame(
    qint64 ptsMs,
    int sampleRate,
    int channelCount,
    const QVector<float>& interleavedSamples) {
  if (sampleRate <= 0 || channelCount <= 0 || interleavedSamples.isEmpty()) {
    return;
  }

  const QVector<float> normalized =
      normalizeToTarget(sampleRate, channelCount, interleavedSamples);
  if (normalized.isEmpty()) {
    return;
  }

  if (!hasPendingStartPts_) {
    pendingStartPtsMs_ = ptsMs;
    hasPendingStartPts_ = true;
  }

  pendingSamples_.reserve(pendingSamples_.size() + normalized.size());
  for (float sample : normalized) {
    pendingSamples_.push_back(sample);
  }

  emitReadySegments();
}

void TranscriptionAudioPreprocessor::appendEndOfStream() {
  const int overlapSamples = static_cast<int>(
      static_cast<qint64>(options_.targetSampleRate)
      * options_.segmentOverlapMs
      / 1000) * options_.targetChannelCount;
  if (overlapSamples > 0 && pendingSamples_.size() <= overlapSamples) {
    pendingSamples_.clear();
  }

  if (!pendingSamples_.isEmpty()) {
    TranscriptionAudioChunk chunk;
    chunk.startPtsMs = pendingStartPtsMs_;
    chunk.sampleRate = options_.targetSampleRate;
    chunk.channelCount = options_.targetChannelCount;
    chunk.samples = pendingSamples_;
    readyChunks_.push_back(std::move(chunk));
    pendingSamples_.clear();
  }

  TranscriptionAudioChunk endChunk;
  endChunk.sampleRate = options_.targetSampleRate;
  endChunk.channelCount = options_.targetChannelCount;
  endChunk.endOfStream = true;
  readyChunks_.push_back(std::move(endChunk));
  hasPendingStartPts_ = false;
}

QVector<TranscriptionAudioChunk> TranscriptionAudioPreprocessor::takeReadyChunks() {
  QVector<TranscriptionAudioChunk> chunks = readyChunks_;
  readyChunks_.clear();
  return chunks;
}

void TranscriptionAudioPreprocessor::reset() {
  pendingSamples_.clear();
  readyChunks_.clear();
  pendingStartPtsMs_ = 0;
  hasPendingStartPts_ = false;
}

int TranscriptionAudioPreprocessor::segmentSampleCount() const {
  const qint64 count =
      static_cast<qint64>(options_.targetSampleRate) * options_.segmentWindowMs
      / 1000;
  return static_cast<int>(std::max<qint64>(1, count));
}

QVector<float> TranscriptionAudioPreprocessor::normalizeToTarget(
    int sampleRate,
    int channelCount,
    const QVector<float>& interleavedSamples) const {
  const int sourceFrameCount = interleavedSamples.size() / channelCount;
  if (sourceFrameCount <= 0) {
    return {};
  }

  QVector<float> mono;
  mono.reserve(sourceFrameCount);
  for (int frameIndex = 0; frameIndex < sourceFrameCount; ++frameIndex) {
    float sum = 0.0f;
    const int baseIndex = frameIndex * channelCount;
    for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
      sum += interleavedSamples[baseIndex + channelIndex];
    }
    mono.push_back(sum / static_cast<float>(channelCount));
  }

  const int targetFrameCount = static_cast<int>(
      std::floor(static_cast<double>(sourceFrameCount)
                 * options_.targetSampleRate
                 / sampleRate));
  if (targetFrameCount <= 0) {
    return {};
  }

  QVector<float> resampled;
  resampled.reserve(targetFrameCount * options_.targetChannelCount);
  for (int targetIndex = 0; targetIndex < targetFrameCount; ++targetIndex) {
    const double sourcePosition =
        static_cast<double>(targetIndex) * sampleRate / options_.targetSampleRate;
    const int leftIndex = static_cast<int>(std::floor(sourcePosition));
    const int rightIndex = std::min(leftIndex + 1, sourceFrameCount - 1);
    const float ratio = static_cast<float>(sourcePosition - leftIndex);
    const float value = mono[leftIndex] * (1.0f - ratio) + mono[rightIndex] * ratio;
    for (int channelIndex = 0; channelIndex < options_.targetChannelCount;
         ++channelIndex) {
      resampled.push_back(value);
    }
  }

  return resampled;
}

void TranscriptionAudioPreprocessor::emitReadySegments() {
  const int segmentSamples = segmentSampleCount() * options_.targetChannelCount;
  const int overlapSamples = static_cast<int>(
      static_cast<qint64>(options_.targetSampleRate)
      * options_.segmentOverlapMs
      / 1000) * options_.targetChannelCount;
  const int stepSamples = std::max(1, segmentSamples - overlapSamples);
  while (pendingSamples_.size() >= segmentSamples) {
    TranscriptionAudioChunk chunk;
    chunk.startPtsMs = pendingStartPtsMs_;
    chunk.sampleRate = options_.targetSampleRate;
    chunk.channelCount = options_.targetChannelCount;
    chunk.samples = pendingSamples_.mid(0, segmentSamples);
    readyChunks_.push_back(std::move(chunk));

    pendingSamples_.erase(pendingSamples_.begin(), pendingSamples_.begin() + stepSamples);
    pendingStartPtsMs_ += samplesToMs(stepSamples / options_.targetChannelCount);
  }
}

qint64 TranscriptionAudioPreprocessor::samplesToMs(int sampleCount) const {
  return static_cast<qint64>(sampleCount) * 1000 / options_.targetSampleRate;
}
