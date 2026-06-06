#include <QtTest/QtTest>

#include "transcription/MediaSubtitlePreparer.h"

#include <atomic>
#include <memory>

class MediaSubtitlePreparerTests : public QObject {
  Q_OBJECT

private slots:
  void missingModelFailsBeforeAudioDecode();
  void canceledRequestStopsBeforeLoadingModel();
};

void MediaSubtitlePreparerTests::missingModelFailsBeforeAudioDecode() {
  MediaSubtitlePreparer preparer;
  MediaSubtitlePreparationRequest request;
  request.mediaPath = QStringLiteral("Z:/missing/media.mp4");
  request.options = TranscriptionOptions::defaults();
  request.options.modelPath = QStringLiteral("Z:/missing/ggml-base.bin");

  const MediaSubtitlePreparationResult result = preparer.prepare(request);

  QVERIFY(!result.success);
  QVERIFY(result.errorMessage.contains(QStringLiteral("模型")));
  QVERIFY(result.subtitleTrack.isEmpty());
}

void MediaSubtitlePreparerTests::canceledRequestStopsBeforeLoadingModel() {
  MediaSubtitlePreparer preparer;
  MediaSubtitlePreparationRequest request;
  request.mediaPath = QStringLiteral("Z:/missing/media.mp4");
  request.options = TranscriptionOptions::defaults();
  request.options.modelPath = QStringLiteral("Z:/missing/ggml-base.bin");
  request.cancelRequested = std::make_shared<std::atomic_bool>(true);

  const MediaSubtitlePreparationResult result = preparer.prepare(request);

  QVERIFY(!result.success);
  QVERIFY(result.canceled);
  QVERIFY(result.errorMessage.contains(QStringLiteral("取消")));
}

QTEST_MAIN(MediaSubtitlePreparerTests)
#include "MediaSubtitlePreparerTests.moc"
