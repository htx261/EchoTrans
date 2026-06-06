#include <QtTest/QtTest>

#include "transcription/WhisperTranscriber.h"

#include <QThread>

class WhisperTranscriberTests : public QObject {
  Q_OBJECT

private slots:
  void defaultOptionsUseAutomaticLanguageAndBoundedThreads();
  void unavailableModelReportsErrorWithoutCommandLineFallback();
  void transcribeReportsErrorWhenModelIsNotLoaded();
};

void WhisperTranscriberTests::defaultOptionsUseAutomaticLanguageAndBoundedThreads() {
  const TranscriptionOptions options = TranscriptionOptions::defaults();

  QVERIFY(options.modelPath.isEmpty());
  QVERIFY(options.languageCode.isEmpty());
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

void WhisperTranscriberTests::transcribeReportsErrorWhenModelIsNotLoaded() {
  WhisperTranscriber transcriber;

  const TranscriptionResult result = transcriber.transcribe(
      TranscriptionAudioInput{0, 16000, 1, QVector<float>(16000, 0.0f)});

  QVERIFY(!result.success);
  QVERIFY(result.errorMessage.contains(QStringLiteral("模型")));
  QVERIFY(result.segments.isEmpty());
}

QTEST_MAIN(WhisperTranscriberTests)
#include "WhisperTranscriberTests.moc"
