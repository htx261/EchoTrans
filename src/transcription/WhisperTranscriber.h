#pragma once

#include <QString>
#include <QVector>

struct TranscriptionOptions {
  QString modelPath;
  QString languageCode;
  int threadCount = 4;
  QString initialPrompt;
  bool timestampsEnabled = true;

  static int maxThreadCount();
  static TranscriptionOptions defaults();
};

struct TranscriptionLoadResult {
  bool success = false;
  QString errorMessage;
};

struct TranscriptionTextSegment {
  qint64 startMs = 0;
  qint64 endMs = 0;
  QString text;
};

struct TranscriptionAudioInput {
  qint64 startPtsMs = 0;
  int sampleRate = 16000;
  int channelCount = 1;
  QVector<float> samples;
};

struct TranscriptionRuntimeOptions {
  bool noContext = false;
  bool noTimestamps = false;
  bool singleSegment = false;
  int maxTokens = -1;
  int audioContext = 0;

  static TranscriptionRuntimeOptions defaults();
  static TranscriptionRuntimeOptions streaming();
};

struct TranscriptionResult {
  bool success = false;
  QString errorMessage;
  QVector<TranscriptionTextSegment> segments;
};

class WhisperTranscriber {
public:
  WhisperTranscriber() = default;
  ~WhisperTranscriber();

  WhisperTranscriber(const WhisperTranscriber&) = delete;
  WhisperTranscriber& operator=(const WhisperTranscriber&) = delete;

  TranscriptionLoadResult loadModel(const TranscriptionOptions& options);
  void unloadModel();
  bool isModelLoaded() const;
  TranscriptionOptions options() const;
  TranscriptionResult transcribe(const TranscriptionAudioInput& audio);
  TranscriptionResult transcribe(
      const TranscriptionAudioInput& audio,
      const TranscriptionRuntimeOptions& runtimeOptions);
  TranscriptionResult transcribe(
      qint64 startPtsMs,
      const float* samples,
      int sampleCount);
  TranscriptionResult transcribe(
      qint64 startPtsMs,
      const float* samples,
      int sampleCount,
      const TranscriptionRuntimeOptions& runtimeOptions);

private:
  struct whisper_context* context_ = nullptr;
  TranscriptionOptions options_;
};
