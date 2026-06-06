#include <QtTest/QtTest>

#include "transcription/WhisperTranscriber.h"

#include <QThread>

class WhisperTranscriberTests : public QObject {
  Q_OBJECT

private slots:
  void defaultOptionsUseAutomaticLanguageAndBoundedThreads();
  void unavailableModelReportsErrorWithoutCommandLineFallback();
};

void WhisperTranscriberTests::defaultOptionsUseAutomaticLanguageAndBoundedThreads() {
  const TranscriptionOptions options = TranscriptionOptions::defaults();

  QVERIFY(options.modelPath.isEmpty());
  QVERIFY(options.languageCode.isEmpty());
  QCOMPARE(options.segmentWindowMs, 10000);
  QVERIFY(options.threadCount >= 1);
  QVERIFY(options.threadCount <= TranscriptionOptions::maxThreadCount());
  QVERIFY(options.timestampsEnabled);
}

void WhisperTranscriberTests::unavailableModelReportsErrorWithoutCommandLineFallback() {
  WhisperTranscriber transcriber;
  TranscriptionOptions options = TranscriptionOptions::defaults();
  options.modelPath = QStringLiteral("Z:/missing/ggml-base.bin");

  const TranscriptionLoadResult result = transcriber.loadModel(options);

  QVERIFY(!result.success);
  QVERIFY(result.errorMessage.contains(QStringLiteral("模型")));
  QVERIFY(!transcriber.isModelLoaded());
}

QTEST_MAIN(WhisperTranscriberTests)
#include "WhisperTranscriberTests.moc"
