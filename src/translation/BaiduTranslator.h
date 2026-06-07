#pragma once

#include "subtitle/SubtitleTrack.h"

#include <QString>

#include <atomic>
#include <functional>
#include <memory>

enum class BaiduTranslationStage {
  Translating,
  Finished
};

struct BaiduTranslationSettings {
  QString appId;
  QString secretKey;
};

struct BaiduTranslationProgress {
  BaiduTranslationStage stage = BaiduTranslationStage::Translating;
  int percent = 0;
  QString message;
};

struct BaiduTranslationRequest {
  SubtitleTrack sourceTrack;
  BaiduTranslationSettings settings;
  QString sourceLanguage = QStringLiteral("auto");
  QString targetLanguage = QStringLiteral("zh");
  std::function<void(const BaiduTranslationProgress&)> progressCallback;
  std::shared_ptr<std::atomic_bool> cancelRequested;
};

struct BaiduTranslationResult {
  bool success = false;
  bool canceled = false;
  QString errorMessage;
  SubtitleTrack subtitleTrack;
};

class BaiduTranslator {
public:
  BaiduTranslationResult translate(const BaiduTranslationRequest& request);

  static QString buildSign(
      const QString& appId,
      const QString& query,
      const QString& salt,
      const QString& secretKey);
  static QString joinBatchQueries(const QVector<SubtitleSegment>& segments, int start, int end);
  static int nextRequestDelayMs(qint64 elapsedSinceLastRequestStartMs);
};
