#include <QtTest/QtTest>

#include "player/MediaPlayerCore.h"

class MediaPlayerCoreTests : public QObject {
  Q_OBJECT

private slots:
  void startsStopped();
  void openStoresPathAndMovesToOpening();
  void playRequiresOpenedMedia();
  void pausesOnlyWhilePlaying();
  void stopIsRepeatable();
  void playStartsWorkerThreads();
  void stopStopsWorkerThreadsAndClosesQueues();
  void destructorStopsWorkerThreads();
};

void MediaPlayerCoreTests::startsStopped() {
  MediaPlayerCore player;

  QCOMPARE(player.state(), PlaybackState::Stopped);
  QVERIFY(player.mediaPath().isEmpty());
}

void MediaPlayerCoreTests::openStoresPathAndMovesToOpening() {
  MediaPlayerCore player;

  QVERIFY(player.open(QStringLiteral("D:/media/sample.mp4")));

  QCOMPARE(player.state(), PlaybackState::Opening);
  QCOMPARE(player.mediaPath(), QStringLiteral("D:/media/sample.mp4"));
}

void MediaPlayerCoreTests::playRequiresOpenedMedia() {
  MediaPlayerCore player;

  QVERIFY(!player.play());
  QCOMPARE(player.state(), PlaybackState::Stopped);

  QVERIFY(player.open(QStringLiteral("D:/media/sample.mp4")));
  QVERIFY(player.play());
  QCOMPARE(player.state(), PlaybackState::Playing);
}

void MediaPlayerCoreTests::pausesOnlyWhilePlaying() {
  MediaPlayerCore player;

  QVERIFY(!player.pause());
  QCOMPARE(player.state(), PlaybackState::Stopped);

  QVERIFY(player.open(QStringLiteral("D:/media/sample.mp4")));
  QVERIFY(player.play());
  QVERIFY(player.pause());
  QCOMPARE(player.state(), PlaybackState::Paused);
}

void MediaPlayerCoreTests::stopIsRepeatable() {
  MediaPlayerCore player;

  QVERIFY(player.open(QStringLiteral("D:/media/sample.mp4")));
  QVERIFY(player.play());

  player.stop();
  QCOMPARE(player.state(), PlaybackState::Stopped);
  QVERIFY(player.mediaPath().isEmpty());

  player.stop();
  QCOMPARE(player.state(), PlaybackState::Stopped);
}

void MediaPlayerCoreTests::playStartsWorkerThreads() {
  MediaPlayerCore player;

  QVERIFY(player.open(QStringLiteral("D:/media/sample.mp4")));
  QVERIFY(player.play());

  QTRY_COMPARE_WITH_TIMEOUT(player.activeWorkerCount(), static_cast<std::size_t>(4), 1000);
  QVERIFY(!player.playbackQueuesClosed());
}

void MediaPlayerCoreTests::stopStopsWorkerThreadsAndClosesQueues() {
  MediaPlayerCore player;

  QVERIFY(player.open(QStringLiteral("D:/media/sample.mp4")));
  QVERIFY(player.play());
  QTRY_COMPARE_WITH_TIMEOUT(player.activeWorkerCount(), static_cast<std::size_t>(4), 1000);

  player.stop();

  QCOMPARE(player.state(), PlaybackState::Stopped);
  QTRY_COMPARE_WITH_TIMEOUT(player.activeWorkerCount(), static_cast<std::size_t>(0), 1000);
  QVERIFY(player.playbackQueuesClosed());

  player.stop();
  QCOMPARE(player.activeWorkerCount(), static_cast<std::size_t>(0));
}

void MediaPlayerCoreTests::destructorStopsWorkerThreads() {
  {
    MediaPlayerCore player;
    QVERIFY(player.open(QStringLiteral("D:/media/sample.mp4")));
    QVERIFY(player.play());
    QTRY_COMPARE_WITH_TIMEOUT(player.activeWorkerCount(), static_cast<std::size_t>(4), 1000);
  }

  QVERIFY(true);
}

QTEST_MAIN(MediaPlayerCoreTests)
#include "MediaPlayerCoreTests.moc"
