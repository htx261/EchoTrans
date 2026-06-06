#pragma once

#include <QString>

struct TranscriptionOptions {
  QString modelPath;
  QString languageCode;
  int threadCount = 4;
  int segmentWindowMs = 10000;
  QString initialPrompt;
  bool timestampsEnabled = true;

  static int maxThreadCount();
  static TranscriptionOptions defaults();
};

struct TranscriptionLoadResult {
  bool success = false;
  QString errorMessage;
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

private:
  struct whisper_context* context_ = nullptr;
  TranscriptionOptions options_;
};
