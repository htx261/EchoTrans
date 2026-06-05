#include "core/DependencyReport.h"

#include <QDir>
#include <QFileInfo>

namespace {
QString normalizedPath(const QString& path) {
  return QDir::fromNativeSeparators(path);
}

bool fileExists(const QString& path) {
  return QFileInfo::exists(path);
}
}

DependencyStatus DependencyReport::checkPath(const QString& path) {
  DependencyStatus status;
  status.path = path;
  status.available = QFileInfo::exists(path);
  return status;
}

bool DependencyReport::isReady() const {
  return ffmpegAvailable
      && whisperAvailable
      && ctranslate2Available
      && whisperModelAvailable
      && translationModelAvailable
      && tokenizerAvailable;
}

QStringList DependencyReport::missingItems() const {
  QStringList items;

  if (!ffmpegAvailable) {
    items.append(QStringLiteral("FFmpeg"));
  }
  if (!whisperAvailable) {
    items.append(QStringLiteral("whisper.cpp"));
  }
  if (!ctranslate2Available) {
    items.append(QStringLiteral("CTranslate2"));
  }
  if (!whisperModelAvailable) {
    items.append(QStringLiteral("Whisper 模型"));
  }
  if (!translationModelAvailable) {
    items.append(QStringLiteral("NLLB 翻译模型"));
  }
  if (!tokenizerAvailable) {
    items.append(QStringLiteral("NLLB Tokenizer"));
  }

  return items;
}

QString DependencyReport::startupMessage() const {
  if (isReady()) {
    return QStringLiteral("启动检查通过：本地依赖与模型已就绪");
  }

  return QStringLiteral("启动检查失败：缺少 %1").arg(missingItems().join(QStringLiteral("、")));
}

DependencyReport DependencyReport::fromConfiguredPaths() {
  DependencyReport report;

  report.ffmpegPath = normalizedPath(QStringLiteral(ECHOTRANS_FFMPEG_ROOT));
  report.whisperPath = normalizedPath(QStringLiteral(ECHOTRANS_WHISPER_ROOT));
  report.ctranslate2Path = normalizedPath(QStringLiteral(ECHOTRANS_CTRANSLATE2_ROOT));
  const QString modelsRoot = normalizedPath(QStringLiteral(ECHOTRANS_MODELS_ROOT));

  report.whisperModelPath = modelsRoot + QStringLiteral("/whisper/ggml-small.bin");
  report.translationModelPath = modelsRoot
      + QStringLiteral("/translation/nllb-200-distilled-600m-ct2-int8/model.bin");
  report.tokenizerPath = modelsRoot
      + QStringLiteral("/tokenizers/nllb-200-distilled-600m/tokenizer.json");

  report.ffmpegAvailable = fileExists(report.ffmpegPath + QStringLiteral("/include/libavformat/avformat.h"))
      && fileExists(report.ffmpegPath + QStringLiteral("/lib/avformat.lib"));
  report.whisperAvailable = fileExists(report.whisperPath + QStringLiteral("/include/whisper.h"));
  report.ctranslate2Available = fileExists(report.ctranslate2Path
      + QStringLiteral("/include/ctranslate2/translator.h"));
  report.whisperModelAvailable = fileExists(report.whisperModelPath);
  report.translationModelAvailable = fileExists(report.translationModelPath);
  report.tokenizerAvailable = fileExists(report.tokenizerPath);

  return report;
}
