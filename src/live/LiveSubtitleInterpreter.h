#pragma once

#include "player/TranscriptionAudioFrame.h"
#include "subtitle/SubtitleTrack.h"
#include "transcription/WhisperTranscriber.h"
#include "translation/BaiduTranslator.h"

#include <QString>

#include <atomic>
#include <functional>
#include <memory>

enum class LiveSubtitleInterpreterStage {
  LoadingModel,
  WaitingAudio,
  Transcribing,
  Translating,
  Finished
};

struct LiveSubtitleInterpreterProgress {
  LiveSubtitleInterpreterStage stage = LiveSubtitleInterpreterStage::WaitingAudio;
  QString message;
};

struct LiveSubtitleInterpreterRequest {
  TranscriptionOptions options;
  BaiduTranslationSettings translationSettings;
  QString sourceLanguage = QStringLiteral("auto");
  QString targetLanguage = QStringLiteral("zh");
  int streamStepMs = 500;
  int streamLengthMs = 4000;
  int streamKeepMs = 200;
  int translationIntervalMs = 1500;
  bool translateSegments = true;
  std::function<bool(TranscriptionAudioFrame*)> takeAudioFrame;
  std::function<bool()> isPlaybackActive;
  std::function<TranscriptionResult(const TranscriptionAudioInput&)> transcribeChunk;
  std::function<BaiduTranslationResult(const BaiduTranslationRequest&)> translateTrack;
  std::function<void(const LiveSubtitleInterpreterProgress&)> progressCallback;
  std::function<void(const SubtitleSegment&)> segmentCallback;
  std::shared_ptr<std::atomic_bool> cancelRequested;
};

struct LiveSubtitleInterpreterResult {
  bool success = false;
  bool canceled = false;
  QString errorMessage;
  SubtitleTrack subtitleTrack;
  int emittedSegmentCount = 0;
};

class LiveSubtitleInterpreter {
public:
  LiveSubtitleInterpreterResult run(const LiveSubtitleInterpreterRequest& request);
};
