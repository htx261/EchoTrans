#include <QtTest/QtTest>

#include "player/VideoFrameSynchronizer.h"

class VideoFrameSynchronizerTests : public QObject {
  Q_OBJECT

private slots:
  void waitsForFutureFrames();
  void displaysFramesNearAudioClock();
  void dropsLateFrames();
};

void VideoFrameSynchronizerTests::waitsForFutureFrames() {
  QCOMPARE(VideoFrameSynchronizer::decide(180, 100), VideoFrameDecision::Wait);
}

void VideoFrameSynchronizerTests::displaysFramesNearAudioClock() {
  QCOMPARE(VideoFrameSynchronizer::decide(125, 100), VideoFrameDecision::Display);
  QCOMPARE(VideoFrameSynchronizer::decide(90, 100), VideoFrameDecision::Display);
}

void VideoFrameSynchronizerTests::dropsLateFrames() {
  QCOMPARE(VideoFrameSynchronizer::decide(0, 180), VideoFrameDecision::Drop);
}

QTEST_MAIN(VideoFrameSynchronizerTests)
#include "VideoFrameSynchronizerTests.moc"
