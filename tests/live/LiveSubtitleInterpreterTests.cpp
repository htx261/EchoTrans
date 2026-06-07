#include "live/LiveSubtitleInterpreter.h"

#include <QElapsedTimer>
#include <QtTest/QtTest>

class LiveSubtitleInterpreterTests : public QObject {
  Q_OBJECT

private slots:
  void translatesChunksAsTheyBecomeReady();
  void emitsFirstStreamWindowAfterStep();
  void resetsStreamWindowToKeepAudioAfterConfirmationBoundary();
  void emitsLiveTranscriptionWithoutTranslation();
  void ignoresNonSpeechSoundLabelsButKeepsSpokenMusicText();
  void doesNotWaitForTranslationIntervalBeforeReadingMoreAudio();
};

void LiveSubtitleInterpreterTests::translatesChunksAsTheyBecomeReady() {
  QVector<TranscriptionAudioFrame> frames;
  TranscriptionAudioFrame frame;
  frame.ptsMs = 0;
  frame.sampleRate = 48000;
  frame.channelCount = 2;
  frame.samples.reserve(48000 * 8 * 2);
  for (int i = 0; i < 48000 * 8; ++i) {
    frame.samples.push_back(0.1f);
    frame.samples.push_back(0.1f);
  }
  frames.push_back(frame);

  int nextFrame = 0;
  int translationCalls = 0;
  QVector<SubtitleSegment> emittedSegments;

  LiveSubtitleInterpreterRequest request;
  request.options = TranscriptionOptions::defaults();
  request.translationSettings.appId = QStringLiteral("app-id");
  request.translationSettings.secretKey = QStringLiteral("secret");
  request.streamStepMs = 8000;
  request.streamLengthMs = 8000;
  request.streamKeepMs = 0;
  request.translationIntervalMs = 0;
  request.takeAudioFrame = [&](TranscriptionAudioFrame* output) {
    if (nextFrame >= frames.size()) {
      return false;
    }
    *output = frames[nextFrame++];
    return true;
  };
  request.isPlaybackActive = [&]() {
    return nextFrame < frames.size();
  };
  request.transcribeChunk = [](const TranscriptionAudioInput& audio) {
    TranscriptionResult result;
    result.success = true;
    result.segments.push_back(TranscriptionTextSegment{
        audio.startPtsMs,
        audio.startPtsMs + 8000,
        QStringLiteral("hello world")});
    return result;
  };
  request.translateTrack = [&](const BaiduTranslationRequest& translationRequest) {
    ++translationCalls;
    BaiduTranslationResult result;
    result.success = true;
    QVector<SubtitleSegment> segments = translationRequest.sourceTrack.segments();
    for (SubtitleSegment& segment : segments) {
      segment.translatedText = QStringLiteral("你好世界");
    }
    result.subtitleTrack.setSegments(segments);
    return result;
  };
  request.segmentCallback = [&](const SubtitleSegment& segment) {
    emittedSegments.push_back(segment);
  };

  LiveSubtitleInterpreter interpreter;
  const LiveSubtitleInterpreterResult result = interpreter.run(request);

  QVERIFY(result.success);
  QCOMPARE(translationCalls, 1);
  QCOMPARE(emittedSegments.size(), 1);
  QCOMPARE(emittedSegments[0].sourceText, QStringLiteral("hello world"));
  QCOMPARE(emittedSegments[0].translatedText, QStringLiteral("你好世界"));
}

void LiveSubtitleInterpreterTests::emitsFirstStreamWindowAfterStep() {
  QVector<TranscriptionAudioFrame> frames;
  TranscriptionAudioFrame frame;
  frame.ptsMs = 0;
  frame.sampleRate = 48000;
  frame.channelCount = 2;
  frame.samples.reserve(48000 / 2 * 2);
  for (int i = 0; i < 48000 / 2; ++i) {
    frame.samples.push_back(0.2f);
    frame.samples.push_back(0.2f);
  }
  frames.push_back(frame);

  int nextFrame = 0;
  QVector<TranscriptionAudioInput> transcribedInputs;

  LiveSubtitleInterpreterRequest request;
  request.options = TranscriptionOptions::defaults();
  request.translationSettings.appId = QStringLiteral("app-id");
  request.translationSettings.secretKey = QStringLiteral("secret");
  request.streamStepMs = 500;
  request.streamLengthMs = 4000;
  request.streamKeepMs = 200;
  request.translationIntervalMs = 0;
  request.takeAudioFrame = [&](TranscriptionAudioFrame* output) {
    if (nextFrame >= frames.size()) {
      return false;
    }
    *output = frames[nextFrame++];
    return true;
  };
  request.isPlaybackActive = [&]() {
    return nextFrame < frames.size();
  };
  request.transcribeChunk = [&](const TranscriptionAudioInput& audio) {
    transcribedInputs.push_back(audio);
    TranscriptionResult result;
    result.success = true;
    result.segments.push_back(TranscriptionTextSegment{
        audio.startPtsMs,
        audio.startPtsMs + 500,
        QStringLiteral("fast")});
    return result;
  };
  request.translateTrack = [](const BaiduTranslationRequest& translationRequest) {
    BaiduTranslationResult result;
    result.success = true;
    result.subtitleTrack = translationRequest.sourceTrack;
    return result;
  };

  LiveSubtitleInterpreter interpreter;
  const LiveSubtitleInterpreterResult result = interpreter.run(request);

  QVERIFY(result.success);
  QCOMPARE(transcribedInputs.size(), 1);
  QCOMPARE(transcribedInputs[0].startPtsMs, 0);
  QCOMPARE(transcribedInputs[0].samples.size(), 8000);
}

void LiveSubtitleInterpreterTests::resetsStreamWindowToKeepAudioAfterConfirmationBoundary() {
  QVector<TranscriptionAudioFrame> frames;
  for (int frameIndex = 0; frameIndex < 8; ++frameIndex) {
    TranscriptionAudioFrame frame;
    frame.ptsMs = frameIndex * 500;
    frame.sampleRate = 48000;
    frame.channelCount = 2;
    frame.samples.reserve(48000 / 2 * 2);
    for (int i = 0; i < 48000 / 2; ++i) {
      frame.samples.push_back(0.2f);
      frame.samples.push_back(0.2f);
    }
    frames.push_back(frame);
  }

  int nextFrame = 0;
  QVector<TranscriptionAudioInput> transcribedInputs;

  LiveSubtitleInterpreterRequest request;
  request.options = TranscriptionOptions::defaults();
  request.translationSettings.appId = QStringLiteral("app-id");
  request.translationSettings.secretKey = QStringLiteral("secret");
  request.streamStepMs = 500;
  request.streamLengthMs = 4000;
  request.streamKeepMs = 200;
  request.translationIntervalMs = 0;
  request.takeAudioFrame = [&](TranscriptionAudioFrame* output) {
    if (nextFrame >= frames.size()) {
      return false;
    }
    *output = frames[nextFrame++];
    return true;
  };
  request.isPlaybackActive = [&]() {
    return nextFrame < frames.size();
  };
  request.transcribeChunk = [&](const TranscriptionAudioInput& audio) {
    transcribedInputs.push_back(audio);
    TranscriptionResult result;
    result.success = true;
    result.segments.push_back(TranscriptionTextSegment{
        audio.startPtsMs,
        audio.startPtsMs + audio.samples.size() * 1000 / audio.sampleRate,
        QStringLiteral("rolling")});
    return result;
  };
  request.translateTrack = [](const BaiduTranslationRequest& translationRequest) {
    BaiduTranslationResult result;
    result.success = true;
    result.subtitleTrack = translationRequest.sourceTrack;
    return result;
  };

  LiveSubtitleInterpreter interpreter;
  const LiveSubtitleInterpreterResult result = interpreter.run(request);

  QVERIFY(result.success);
  QCOMPARE(transcribedInputs.size(), 8);
  QCOMPARE(transcribedInputs[6].samples.size(), 56000);
  QCOMPARE(transcribedInputs[7].samples.size(), 11200);
  QCOMPARE(transcribedInputs[7].startPtsMs, 3300);
}

void LiveSubtitleInterpreterTests::emitsLiveTranscriptionWithoutTranslation() {
  QVector<TranscriptionAudioFrame> frames;
  for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
    TranscriptionAudioFrame frame;
    frame.ptsMs = frameIndex * 500;
    frame.sampleRate = 48000;
    frame.channelCount = 2;
    frame.samples.reserve(48000 / 2 * 2);
    for (int i = 0; i < 48000 / 2; ++i) {
      frame.samples.push_back(0.2f);
      frame.samples.push_back(0.2f);
    }
    frames.push_back(frame);
  }

  int nextFrame = 0;
  int transcribeCalls = 0;
  int translationCalls = 0;
  QVector<SubtitleSegment> emittedSegments;

  LiveSubtitleInterpreterRequest request;
  request.options = TranscriptionOptions::defaults();
  request.translationSettings.appId = QStringLiteral("app-id");
  request.translationSettings.secretKey = QStringLiteral("secret");
  request.streamStepMs = 500;
  request.streamLengthMs = 4000;
  request.streamKeepMs = 200;
  request.translationIntervalMs = 0;
  request.translateSegments = false;
  request.takeAudioFrame = [&](TranscriptionAudioFrame* output) {
    if (nextFrame >= frames.size()) {
      return false;
    }
    *output = frames[nextFrame++];
    return true;
  };
  request.isPlaybackActive = [&]() {
    return nextFrame < frames.size();
  };
  request.transcribeChunk = [&](const TranscriptionAudioInput& audio) {
    ++transcribeCalls;
    TranscriptionResult result;
    result.success = true;
    result.segments.push_back(TranscriptionTextSegment{
        audio.startPtsMs,
        audio.startPtsMs + audio.samples.size() * 1000 / audio.sampleRate,
        transcribeCalls == 1 ? QStringLiteral("hel") : QStringLiteral("hello")});
    return result;
  };
  request.translateTrack = [&](const BaiduTranslationRequest& translationRequest) {
    Q_UNUSED(translationRequest);
    ++translationCalls;
    BaiduTranslationResult result;
    result.success = true;
    return result;
  };
  request.segmentCallback = [&](const SubtitleSegment& segment) {
    emittedSegments.push_back(segment);
  };

  LiveSubtitleInterpreter interpreter;
  const LiveSubtitleInterpreterResult result = interpreter.run(request);

  QVERIFY(result.success);
  QCOMPARE(translationCalls, 0);
  QCOMPARE(emittedSegments.size(), 2);
  QCOMPARE(emittedSegments[0].sourceText, QStringLiteral("hel"));
  QCOMPARE(emittedSegments[1].sourceText, QStringLiteral("hello"));
  QCOMPARE(result.subtitleTrack.segments().size(), 1);
  QCOMPARE(result.subtitleTrack.segments()[0].startMs, 0);
  QCOMPARE(result.subtitleTrack.segments()[0].endMs, 1000);
  QCOMPARE(result.subtitleTrack.segments()[0].sourceText, QStringLiteral("hello"));
}

void LiveSubtitleInterpreterTests::ignoresNonSpeechSoundLabelsButKeepsSpokenMusicText() {
  QVector<TranscriptionAudioFrame> frames;
  for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
    TranscriptionAudioFrame frame;
    frame.ptsMs = frameIndex * 500;
    frame.sampleRate = 48000;
    frame.channelCount = 2;
    frame.samples.reserve(48000 / 2 * 2);
    for (int i = 0; i < 48000 / 2; ++i) {
      frame.samples.push_back(0.2f);
      frame.samples.push_back(0.2f);
    }
    frames.push_back(frame);
  }

  int nextFrame = 0;
  int transcribeCalls = 0;
  QVector<SubtitleSegment> emittedSegments;

  LiveSubtitleInterpreterRequest request;
  request.options = TranscriptionOptions::defaults();
  request.streamStepMs = 500;
  request.streamLengthMs = 4000;
  request.streamKeepMs = 200;
  request.translationIntervalMs = 0;
  request.translateSegments = false;
  request.takeAudioFrame = [&](TranscriptionAudioFrame* output) {
    if (nextFrame >= frames.size()) {
      return false;
    }
    *output = frames[nextFrame++];
    return true;
  };
  request.isPlaybackActive = [&]() {
    return nextFrame < frames.size();
  };
  request.transcribeChunk = [&](const TranscriptionAudioInput& audio) {
    ++transcribeCalls;
    TranscriptionResult result;
    result.success = true;
    result.segments.push_back(TranscriptionTextSegment{
        audio.startPtsMs,
        audio.startPtsMs + audio.samples.size() * 1000 / audio.sampleRate,
        transcribeCalls == 1
            ? QStringLiteral("[Music]")
            : QStringLiteral("I like music theory")});
    return result;
  };
  request.segmentCallback = [&](const SubtitleSegment& segment) {
    emittedSegments.push_back(segment);
  };

  LiveSubtitleInterpreter interpreter;
  const LiveSubtitleInterpreterResult result = interpreter.run(request);

  QVERIFY(result.success);
  QCOMPARE(emittedSegments.size(), 1);
  QCOMPARE(emittedSegments[0].sourceText, QStringLiteral("I like music theory"));
  QCOMPARE(result.subtitleTrack.segments().size(), 1);
  QCOMPARE(result.subtitleTrack.segments()[0].sourceText, QStringLiteral("I like music theory"));
}

void LiveSubtitleInterpreterTests::doesNotWaitForTranslationIntervalBeforeReadingMoreAudio() {
  QVector<TranscriptionAudioFrame> frames;
  for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
    TranscriptionAudioFrame frame;
    frame.ptsMs = frameIndex * 500;
    frame.sampleRate = 48000;
    frame.channelCount = 2;
    frame.samples.reserve(48000 / 2 * 2);
    for (int i = 0; i < 48000 / 2; ++i) {
      frame.samples.push_back(0.2f);
      frame.samples.push_back(0.2f);
    }
    frames.push_back(frame);
  }

  int nextFrame = 0;
  int transcribeCalls = 0;
  QElapsedTimer timer;
  timer.start();

  LiveSubtitleInterpreterRequest request;
  request.options = TranscriptionOptions::defaults();
  request.translationSettings.appId = QStringLiteral("app-id");
  request.translationSettings.secretKey = QStringLiteral("secret");
  request.streamStepMs = 500;
  request.streamLengthMs = 4000;
  request.streamKeepMs = 200;
  request.translationIntervalMs = 5000;
  request.takeAudioFrame = [&](TranscriptionAudioFrame* output) {
    if (nextFrame >= frames.size()) {
      return false;
    }
    *output = frames[nextFrame++];
    return true;
  };
  request.isPlaybackActive = [&]() {
    return nextFrame < frames.size();
  };
  request.transcribeChunk = [&](const TranscriptionAudioInput& audio) {
    ++transcribeCalls;
    const qint64 startMs = (transcribeCalls - 1) * 500;
    TranscriptionResult result;
    result.success = true;
    result.segments.push_back(TranscriptionTextSegment{
        startMs,
        startMs + 500,
        QStringLiteral("fast")});
    return result;
  };
  request.translateTrack = [](const BaiduTranslationRequest& translationRequest) {
    BaiduTranslationResult result;
    result.success = true;
    result.subtitleTrack = translationRequest.sourceTrack;
    return result;
  };

  LiveSubtitleInterpreter interpreter;
  const LiveSubtitleInterpreterResult result = interpreter.run(request);

  QVERIFY(result.success);
  QCOMPARE(transcribeCalls, 2);
  QVERIFY(timer.elapsed() < 1000);
}

QTEST_MAIN(LiveSubtitleInterpreterTests)
#include "LiveSubtitleInterpreterTests.moc"
