#include "translation/BaiduTranslator.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {
constexpr int RequestTimeoutMs = 15000;
constexpr int TranslationBatchSize = 8;
constexpr int MinRequestIntervalMs = 100;
constexpr int RateLimitRetryDelayMs = 3000;
constexpr const char* BaiduRateLimitErrorCode = "54003";

struct BatchTranslationResponse {
  QStringList translatedTexts;
  QString errorCode;
  QString errorMessage;
};

bool isCanceled(const BaiduTranslationRequest& request) {
  return request.cancelRequested && request.cancelRequested->load();
}

BaiduTranslationResult canceledResult() {
  BaiduTranslationResult result;
  result.canceled = true;
  result.errorMessage = QStringLiteral("翻译已取消");
  return result;
}

bool sleepWithCancel(const BaiduTranslationRequest& request, int totalMs) {
  int remainingMs = totalMs;
  while (remainingMs > 0) {
    if (isCanceled(request)) {
      return false;
    }

    const int chunkMs = std::min(remainingMs, 50);
    QThread::msleep(static_cast<unsigned long>(chunkMs));
    remainingMs -= chunkMs;
  }

  return !isCanceled(request);
}

void reportProgress(
    const BaiduTranslationRequest& request,
    BaiduTranslationStage stage,
    int percent,
    const QString& message) {
  if (!request.progressCallback) {
    return;
  }

  request.progressCallback(BaiduTranslationProgress{
      stage,
      std::max(0, std::min(100, percent)),
      message});
}

QString baiduSourceLanguage(const QString& languageCode) {
  if (languageCode == QStringLiteral("ja")) {
    return QStringLiteral("jp");
  }
  if (languageCode == QStringLiteral("ko")) {
    return QStringLiteral("kor");
  }
  if (languageCode == QStringLiteral("fr")) {
    return QStringLiteral("fra");
  }
  if (languageCode.trimmed().isEmpty()) {
    return QStringLiteral("auto");
  }
  return languageCode;
}

BatchTranslationResponse translateBatchOnce(
    QNetworkAccessManager* manager,
    const BaiduTranslationRequest& request,
    const QString& text) {
  BatchTranslationResponse response;

  if (!QSslSocket::supportsSsl()) {
    response.errorMessage = QStringLiteral("Qt TLS 不可用：请确认程序目录包含 libssl-1_1-x64.dll 和 libcrypto-1_1-x64.dll");
    return response;
  }

  const QString salt = QString::number(QRandomGenerator::global()->generate());
  const QString sign = BaiduTranslator::buildSign(
      request.settings.appId,
      text,
      salt,
      request.settings.secretKey);

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("q"), text);
  query.addQueryItem(QStringLiteral("from"), baiduSourceLanguage(request.sourceLanguage));
  query.addQueryItem(QStringLiteral("to"), request.targetLanguage);
  query.addQueryItem(QStringLiteral("appid"), request.settings.appId);
  query.addQueryItem(QStringLiteral("salt"), salt);
  query.addQueryItem(QStringLiteral("sign"), sign);

  QNetworkRequest networkRequest(QUrl(QStringLiteral("https://fanyi-api.baidu.com/api/trans/vip/translate")));
  networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

  QNetworkReply* reply = manager->post(networkRequest, query.toString(QUrl::FullyEncoded).toUtf8());
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(RequestTimeoutMs);
  loop.exec();

  if (!timer.isActive()) {
    reply->abort();
    reply->deleteLater();
    response.errorMessage = QStringLiteral("百度翻译请求超时");
    return response;
  }

  timer.stop();
  const QByteArray payload = reply->readAll();
  const QNetworkReply::NetworkError networkError = reply->error();
  const QString networkErrorString = reply->errorString();
  reply->deleteLater();

  if (networkError != QNetworkReply::NoError) {
    response.errorMessage = QStringLiteral("百度翻译网络错误：%1").arg(networkErrorString);
    return response;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    response.errorMessage = QStringLiteral("解析百度翻译响应失败：%1").arg(parseError.errorString());
    return response;
  }

  const QJsonObject object = document.object();
  if (object.contains(QStringLiteral("error_code"))) {
    response.errorCode = object.value(QStringLiteral("error_code")).toString();
    response.errorMessage = QStringLiteral("百度翻译失败：%1 %2")
        .arg(response.errorCode)
        .arg(object.value(QStringLiteral("error_msg")).toString());
    return response;
  }

  const QJsonArray results = object.value(QStringLiteral("trans_result")).toArray();
  if (results.isEmpty()) {
    response.errorMessage = QStringLiteral("百度翻译响应缺少翻译结果");
    return response;
  }

  for (const QJsonValue& value : results) {
    response.translatedTexts.append(value.toObject().value(QStringLiteral("dst")).toString().trimmed());
  }
  return response;
}

BatchTranslationResponse translateBatchWithRetry(
    QNetworkAccessManager* manager,
    const BaiduTranslationRequest& request,
    const QString& text) {
  BatchTranslationResponse response = translateBatchOnce(manager, request, text);
  if (response.errorCode == QString::fromLatin1(BaiduRateLimitErrorCode) && !isCanceled(request)) {
    QThread::msleep(RateLimitRetryDelayMs);
    response = translateBatchOnce(manager, request, text);
  }
  return response;
}
}

QString BaiduTranslator::buildSign(
    const QString& appId,
    const QString& query,
    const QString& salt,
    const QString& secretKey) {
  const QByteArray raw = (appId + query + salt + secretKey).toUtf8();
  return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex());
}

QString BaiduTranslator::joinBatchQueries(const QVector<SubtitleSegment>& segments, int start, int end) {
  QStringList queries;
  for (int index = std::max(0, start); index < std::min(end, segments.size()); ++index) {
    queries.append(segments[index].sourceText);
  }
  return queries.join(QStringLiteral("\n"));
}

int BaiduTranslator::nextRequestDelayMs(qint64 elapsedSinceLastRequestStartMs) {
  if (elapsedSinceLastRequestStartMs <= 0) {
    return MinRequestIntervalMs;
  }

  if (elapsedSinceLastRequestStartMs >= MinRequestIntervalMs) {
    return 0;
  }

  return MinRequestIntervalMs - static_cast<int>(elapsedSinceLastRequestStartMs);
}

BaiduTranslationResult BaiduTranslator::translate(const BaiduTranslationRequest& request) {
  BaiduTranslationResult result;

  if (isCanceled(request)) {
    return canceledResult();
  }

  if (request.settings.appId.trimmed().isEmpty()
      || request.settings.secretKey.trimmed().isEmpty()) {
    result.errorMessage = QStringLiteral("未设置百度翻译 API 信息");
    return result;
  }

  const QVector<SubtitleSegment> sourceSegments = request.sourceTrack.segments();
  if (sourceSegments.isEmpty()) {
    result.success = true;
    result.subtitleTrack = request.sourceTrack;
    return result;
  }

  QNetworkAccessManager manager;
  QVector<SubtitleSegment> translatedSegments = sourceSegments;
  QElapsedTimer lastRequestStartTimer;
  bool hasSentRequest = false;
  for (int start = 0; start < translatedSegments.size(); start += TranslationBatchSize) {
    if (isCanceled(request)) {
      return canceledResult();
    }

    if (hasSentRequest) {
      const int delayMs = nextRequestDelayMs(lastRequestStartTimer.elapsed());
      if (delayMs > 0 && !sleepWithCancel(request, delayMs)) {
        return canceledResult();
      }
    }

    const int end = std::min(start + TranslationBatchSize, translatedSegments.size());
    reportProgress(
        request,
        BaiduTranslationStage::Translating,
        static_cast<int>(start * 100 / std::max(1, translatedSegments.size())),
        QStringLiteral("正在调用百度翻译 %1-%2/%3")
            .arg(start + 1)
            .arg(end)
            .arg(translatedSegments.size()));

    const QString query = joinBatchQueries(translatedSegments, start, end);
    lastRequestStartTimer.restart();
    hasSentRequest = true;
    const BatchTranslationResponse response = translateBatchWithRetry(
        &manager,
        request,
        query);
    if (!response.errorMessage.isEmpty()) {
      result.errorMessage = response.errorMessage;
      return result;
    }

    if (response.translatedTexts.size() != end - start) {
      result.errorMessage = QStringLiteral("百度翻译结果数量不匹配");
      return result;
    }

    for (int batchIndex = 0; batchIndex < response.translatedTexts.size(); ++batchIndex) {
      translatedSegments[start + batchIndex].translatedText = response.translatedTexts[batchIndex];
    }
  }

  reportProgress(
      request,
      BaiduTranslationStage::Finished,
      100,
      QStringLiteral("百度翻译完成"));
  result.success = true;
  result.subtitleTrack.setSegments(translatedSegments);
  return result;
}
