#pragma once

#include "subtitle/SubtitleTrack.h"
#include "transcription/WhisperTranscriber.h"

#include <QString>

#include <atomic>
#include <functional>
#include <memory>

enum class MediaSubtitlePreparationStage {
  LoadingModel,
  ExtractingAudio,
  Transcribing,
  Translating,
  Finished
};

struct MediaSubtitlePreparationProgress {
  MediaSubtitlePreparationStage stage = MediaSubtitlePreparationStage::LoadingModel;
  int percent = 0;
  QString message;
};

struct MediaSubtitlePreparationRequest {
  QString mediaPath;
  TranscriptionOptions options;
  std::function<void(const MediaSubtitlePreparationProgress&)> progressCallback;
  std::shared_ptr<std::atomic_bool> cancelRequested;
};

struct MediaSubtitlePreparationResult {
  bool success = false;
  bool canceled = false;
  QString errorMessage;
  SubtitleTrack subtitleTrack;
};

class MediaSubtitlePreparer {
public:
  MediaSubtitlePreparationResult prepare(const MediaSubtitlePreparationRequest& request);
};
