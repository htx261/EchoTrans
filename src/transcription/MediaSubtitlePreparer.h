#pragma once

#include "subtitle/SubtitleTrack.h"
#include "transcription/WhisperTranscriber.h"

#include <QString>

#include <functional>

enum class MediaSubtitlePreparationStage {
  LoadingModel,
  ExtractingAudio,
  Transcribing,
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
};

struct MediaSubtitlePreparationResult {
  bool success = false;
  QString errorMessage;
  SubtitleTrack subtitleTrack;
};

class MediaSubtitlePreparer {
public:
  MediaSubtitlePreparationResult prepare(const MediaSubtitlePreparationRequest& request);
};
