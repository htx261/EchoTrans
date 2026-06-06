#include "transcription/WhisperTranscriber.h"

#include <QFileInfo>
#include <QThread>

#include <algorithm>

extern "C" {
#include <whisper.h>
}

int TranscriptionOptions::maxThreadCount() {
  return std::max(1, QThread::idealThreadCount());
}

TranscriptionOptions TranscriptionOptions::defaults() {
  TranscriptionOptions options;
  const int maxThreads = maxThreadCount();
  options.threadCount = std::max(1, std::min(4, maxThreads));
  options.timestampsEnabled = true;
  return options;
}

WhisperTranscriber::~WhisperTranscriber() {
  unloadModel();
}

TranscriptionLoadResult WhisperTranscriber::loadModel(const TranscriptionOptions& options) {
  unloadModel();

  const QString trimmedModelPath = options.modelPath.trimmed();
  if (trimmedModelPath.isEmpty()) {
    return TranscriptionLoadResult{false, QStringLiteral("未选择转录模型文件")};
  }

  const QFileInfo modelInfo(trimmedModelPath);
  if (!modelInfo.exists() || !modelInfo.isFile()) {
    return TranscriptionLoadResult{false, QStringLiteral("转录模型文件不存在：%1").arg(trimmedModelPath)};
  }

  whisper_context_params contextParams = whisper_context_default_params();
  contextParams.use_gpu = false;

  context_ = whisper_init_from_file_with_params(modelInfo.absoluteFilePath().toUtf8().constData(), contextParams);
  if (!context_) {
    return TranscriptionLoadResult{false, QStringLiteral("加载转录模型失败：%1").arg(modelInfo.absoluteFilePath())};
  }

  options_ = options;
  options_.modelPath = modelInfo.absoluteFilePath();
  options_.threadCount = std::max(1, std::min(options.threadCount, TranscriptionOptions::maxThreadCount()));
  return TranscriptionLoadResult{true, QString()};
}

void WhisperTranscriber::unloadModel() {
  if (context_) {
    whisper_free(context_);
    context_ = nullptr;
  }
}

bool WhisperTranscriber::isModelLoaded() const {
  return context_ != nullptr;
}

TranscriptionOptions WhisperTranscriber::options() const {
  return options_;
}

TranscriptionResult WhisperTranscriber::transcribe(const TranscriptionAudioInput& audio) {
  return transcribe(audio.startPtsMs, audio.samples.constData(), audio.samples.size());
}

TranscriptionResult WhisperTranscriber::transcribe(
    qint64 startPtsMs,
    const float* samples,
    int sampleCount) {
  if (!context_) {
    return TranscriptionResult{false, QStringLiteral("转录模型未加载"), {}};
  }

  if (!samples || sampleCount <= 0) {
    return TranscriptionResult{true, QString(), {}};
  }

  whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.print_realtime = false;
  params.print_progress = false;
  params.print_timestamps = false;
  params.print_special = false;
  params.translate = false;
  params.no_context = false;
  params.single_segment = false;
  params.n_threads = options_.threadCount;

  const QByteArray languageBytes = options_.languageCode.trimmed().toUtf8();
  if (!languageBytes.isEmpty()) {
    params.language = languageBytes.constData();
  }

  const QByteArray promptBytes = options_.initialPrompt.trimmed().toUtf8();
  if (!promptBytes.isEmpty()) {
    params.initial_prompt = promptBytes.constData();
  }

  const int result = whisper_full(
      context_,
      params,
      samples,
      sampleCount);
  if (result != 0) {
    return TranscriptionResult{false, QStringLiteral("whisper.cpp 转录失败"), {}};
  }

  TranscriptionResult transcriptionResult;
  transcriptionResult.success = true;

  const int segmentCount = whisper_full_n_segments(context_);
  transcriptionResult.segments.reserve(segmentCount);
  for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
    const char* text = whisper_full_get_segment_text(context_, segmentIndex);
    const qint64 startMs = startPtsMs + whisper_full_get_segment_t0(context_, segmentIndex) * 10;
    const qint64 endMs = startPtsMs + whisper_full_get_segment_t1(context_, segmentIndex) * 10;
    const QString segmentText = QString::fromUtf8(text).trimmed();
    if (segmentText.isEmpty()) {
      continue;
    }

    transcriptionResult.segments.push_back(TranscriptionTextSegment{
        startMs,
        std::max(startMs + 1, endMs),
        segmentText});
  }

  return transcriptionResult;
}
