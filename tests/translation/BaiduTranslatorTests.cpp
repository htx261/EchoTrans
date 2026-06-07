#include <QtTest/QtTest>

#include "translation/BaiduTranslator.h"

class BaiduTranslatorTests : public QObject {
  Q_OBJECT

private slots:
  void missingApiSettingsReturnsError();
  void buildsExpectedSign();
  void joinsBatchQueriesWithNewlines();
};

void BaiduTranslatorTests::missingApiSettingsReturnsError() {
  SubtitleTrack track;
  track.setSegments({
      SubtitleSegment{0, 1000, QStringLiteral("Hello"), QString()}
  });

  BaiduTranslationRequest request;
  request.sourceTrack = track;

  BaiduTranslator translator;
  const BaiduTranslationResult result = translator.translate(request);

  QVERIFY(!result.success);
  QVERIFY(result.errorMessage.contains(QStringLiteral("百度翻译")));
  QVERIFY(result.subtitleTrack.isEmpty());
}

void BaiduTranslatorTests::buildsExpectedSign() {
  const QString sign = BaiduTranslator::buildSign(
      QStringLiteral("2015063000000001"),
      QStringLiteral("apple"),
      QStringLiteral("1435660288"),
      QStringLiteral("12345678"));

  QCOMPARE(sign, QStringLiteral("f89f9594663708c1605f3d736d01d2d4"));
}

void BaiduTranslatorTests::joinsBatchQueriesWithNewlines() {
  QVector<SubtitleSegment> segments = {
      SubtitleSegment{0, 1000, QStringLiteral("Hello"), QString()},
      SubtitleSegment{1000, 2000, QStringLiteral("World"), QString()},
      SubtitleSegment{2000, 3000, QStringLiteral("Later"), QString()}
  };

  QCOMPARE(BaiduTranslator::joinBatchQueries(segments, 0, 2), QStringLiteral("Hello\nWorld"));
  QCOMPARE(BaiduTranslator::joinBatchQueries(segments, 1, 3), QStringLiteral("World\nLater"));
}

QTEST_MAIN(BaiduTranslatorTests)
#include "BaiduTranslatorTests.moc"
