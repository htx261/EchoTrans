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
  options.segmentWindowMs = 10000;
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
