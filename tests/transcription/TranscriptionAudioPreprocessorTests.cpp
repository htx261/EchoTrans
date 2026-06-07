#include "transcription/TranscriptionAudioPreprocessor.h"

#include <QtTest/QtTest>

class TranscriptionAudioPreprocessorTests : public QObject {
  Q_OBJECT

private slots:
  void convertsStereo48kToMono16kChunks();
  void flushesTrailingSamplesAndEndOfStream();
  void emitsOverlappedChunks();
};

void TranscriptionAudioPreprocessorTests::convertsStereo48kToMono16kChunks() {
  TranscriptionAudioPreprocessor preprocessor({16000, 1, 500});

  QVector<float> input;
  input.reserve(48000 * 2);
  for (int i = 0; i < 48000; ++i) {
    input.push_back(0.25f);
    input.push_back(0.75f);
  }

  preprocessor.appendFrame(1200, 48000, 2, input);
  const QVector<TranscriptionAudioChunk> chunks = preprocessor.takeReadyChunks();

  QCOMPARE(chunks.size(), 2);
  QCOMPARE(chunks[0].startPtsMs, 1200);
  QCOMPARE(chunks[0].sampleRate, 16000);
  QCOMPARE(chunks[0].channelCount, 1);
  QCOMPARE(chunks[0].samples.size(), 8000);
  QCOMPARE(chunks[1].startPtsMs, 1700);
  QCOMPARE(chunks[1].samples.size(), 8000);
  for (float sample : chunks[0].samples) {
    QVERIFY(qAbs(sample - 0.5f) < 0.0001f);
  }
}

void TranscriptionAudioPreprocessorTests::flushesTrailingSamplesAndEndOfStream() {
  TranscriptionAudioPreprocessor preprocessor({16000, 1, 1000});

  QVector<float> input(4800 * 2, 0.2f);
  preprocessor.appendFrame(0, 48000, 2, input);
  QVERIFY(preprocessor.takeReadyChunks().isEmpty());

  preprocessor.appendEndOfStream();
  const QVector<TranscriptionAudioChunk> chunks = preprocessor.takeReadyChunks();

  QCOMPARE(chunks.size(), 2);
  QCOMPARE(chunks[0].startPtsMs, 0);
  QCOMPARE(chunks[0].samples.size(), 1600);
  QVERIFY(!chunks[0].endOfStream);
  QVERIFY(chunks[1].endOfStream);
  QVERIFY(chunks[1].samples.isEmpty());
}

void TranscriptionAudioPreprocessorTests::emitsOverlappedChunks() {
  TranscriptionAudioPreprocessor preprocessor({16000, 1, 8000, 2000});

  QVector<float> input;
  input.reserve(48000 * 14 * 2);
  for (int i = 0; i < 48000 * 14; ++i) {
    input.push_back(0.4f);
    input.push_back(0.6f);
  }

  preprocessor.appendFrame(1000, 48000, 2, input);
  const QVector<TranscriptionAudioChunk> chunks = preprocessor.takeReadyChunks();

  QCOMPARE(chunks.size(), 2);
  QCOMPARE(chunks[0].startPtsMs, 1000);
  QCOMPARE(chunks[0].samples.size(), 16000 * 8);
  QCOMPARE(chunks[1].startPtsMs, 7000);
  QCOMPARE(chunks[1].samples.size(), 16000 * 8);
}

QTEST_MAIN(TranscriptionAudioPreprocessorTests)
#include "TranscriptionAudioPreprocessorTests.moc"
