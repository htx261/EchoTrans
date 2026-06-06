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
      qint64 startPtsMs,
      const float* samples,
      int sampleCount);

private:
  struct whisper_context* context_ = nullptr;
  TranscriptionOptions options_;
};
