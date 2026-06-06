#include <QtTest/QtTest>

#include "player/MediaPlayerCore.h"

#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>

#include <atomic>
#include <cstring>

namespace {
void appendAscii(QByteArray* bytes, const char* text) {
  bytes->append(text, static_cast<int>(std::strlen(text)));
}

void appendLe16(QByteArray* bytes, quint16 value) {
  bytes->append(static_cast<char>(value & 0xff));
  bytes->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* bytes, quint32 value) {
  bytes->append(static_cast<char>(value & 0xff));
  bytes->append(static_cast<char>((value >> 8) & 0xff));
  bytes->append(static_cast<char>((value >> 16) & 0xff));
  bytes->append(static_cast<char>((value >> 24) & 0xff));
}

QString writeSilentWavFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return QString();
  }

  const quint16 channels = 1;
  const quint32 sampleRate = 8000;
  const quint16 bitsPerSample = 16;
  const quint16 blockAlign = channels * bitsPerSample / 8;
  const quint32 byteRate = sampleRate * blockAlign;
  const quint32 sampleCount = 8000;
  const quint32 dataSize = sampleCount * blockAlign;

  QByteArray wav;
  appendAscii(&wav, "RIFF");
  appendLe32(&wav, 36 + dataSize);
  appendAscii(&wav, "WAVE");
  appendAscii(&wav, "fmt ");
  appendLe32(&wav, 16);
  appendLe16(&wav, 1);
  appendLe16(&wav, channels);
  appendLe32(&wav, sampleRate);
  appendLe32(&wav, byteRate);
  appendLe16(&wav, blockAlign);
  appendLe16(&wav, bitsPerSample);
  appendAscii(&wav, "data");
  appendLe32(&wav, dataSize);
  wav.append(QByteArray(static_cast<int>(dataSize), '\0'));

  file.write(wav);
  return path;
}

QString writeTestAudioVideoFile(const QString& path) {
  const QString ffmpegPath = QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe");
  if (!QFile::exists(ffmpegPath)) {
    return QString();
  }

  QProcess ffmpeg;
  ffmpeg.start(ffmpegPath, QStringList()
      << QStringLiteral("-y")
      << QStringLiteral("-f") << QStringLiteral("lavfi")
      << QStringLiteral("-i") << QStringLiteral("testsrc=size=64x48:rate=10:duration=1")
      << QStringLiteral("-f") << QStringLiteral("lavfi")
      << QStringLiteral("-i") << QStringLiteral("sine=frequency=440:duration=1")
      << QStringLiteral("-shortest")
      << QStringLiteral("-c:v") << QStringLiteral("mpeg4")
      << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le")
      << path);

  if (!ffmpeg.waitForFinished(10000) || ffmpeg.exitCode() != 0) {
    return QString();
  }

  return path;
}
}

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
  void demuxThreadQueuesAudioPackets();
  void demuxThreadReportsOpenFailure();
  void audioDecodeThreadProducesPcmFrames();
  void audioOutputThreadConsumesFramesOrReportsDeviceError();
  void videoDecodeThreadProducesImages();
  void publishesVideoFramesFromAudioClock();
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

void MediaPlayerCoreTests::demuxThreadQueuesAudioPackets() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeSilentWavFile(dir.filePath(QStringLiteral("silent.wav")));
  QVERIFY(!path.isEmpty());

  MediaPlayerCore player;
  QVERIFY(player.open(path));
  QVERIFY(player.play());

  QTRY_VERIFY_WITH_TIMEOUT(player.demuxFinished(), 1000);
  QVERIFY2(player.lastDemuxError().isEmpty(), qPrintable(player.lastDemuxError()));
  QVERIFY(player.demuxedAudioPacketCount() > 0);
  QCOMPARE(player.demuxedVideoPacketCount(), static_cast<std::size_t>(0));
}

void MediaPlayerCoreTests::demuxThreadReportsOpenFailure() {
  MediaPlayerCore player;

  QVERIFY(player.open(QStringLiteral("Z:/missing/media.wav")));
  QVERIFY(player.play());

  QTRY_VERIFY_WITH_TIMEOUT(player.demuxFinished(), 1000);
  QVERIFY(!player.lastDemuxError().isEmpty());
  QCOMPARE(player.audioPacketQueueSize(), static_cast<std::size_t>(0));
  QCOMPARE(player.videoPacketQueueSize(), static_cast<std::size_t>(0));
}

void MediaPlayerCoreTests::audioDecodeThreadProducesPcmFrames() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeSilentWavFile(dir.filePath(QStringLiteral("silent.wav")));
  QVERIFY(!path.isEmpty());

  MediaPlayerCore player;
  QVERIFY(player.open(path));
  QVERIFY(player.play());

  QTRY_VERIFY_WITH_TIMEOUT(player.decodedAudioFrameCount() > 0, 1500);
  QVERIFY2(player.lastAudioDecodeError().isEmpty(), qPrintable(player.lastAudioDecodeError()));
  QVERIFY(player.decodedAudioByteCount() > 0);
}

void MediaPlayerCoreTests::audioOutputThreadConsumesFramesOrReportsDeviceError() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeSilentWavFile(dir.filePath(QStringLiteral("silent.wav")));
  QVERIFY(!path.isEmpty());

  MediaPlayerCore player;
  QVERIFY(player.open(path));
  QVERIFY(player.play());

  QTRY_VERIFY_WITH_TIMEOUT(
      player.audioOutputByteCount() > 0 || !player.lastAudioOutputError().isEmpty(),
      2000);

  QVERIFY(player.audioOutputByteCount() > 0 || !player.lastAudioOutputError().isEmpty());
}

void MediaPlayerCoreTests::videoDecodeThreadProducesImages() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeTestAudioVideoFile(dir.filePath(QStringLiteral("sample.avi")));
  QVERIFY(!path.isEmpty());

  MediaPlayerCore player;
  QVERIFY(player.open(path));
  QVERIFY(player.play());

  QTRY_VERIFY_WITH_TIMEOUT(player.decodedVideoFrameCount() > 0, 2000);
  QVERIFY2(player.lastVideoDecodeError().isEmpty(), qPrintable(player.lastVideoDecodeError()));

  QImage image;
  QTRY_VERIFY_WITH_TIMEOUT(player.takeVideoFrame(&image), 1000);
  QVERIFY(!image.isNull());
  QCOMPARE(image.size(), QSize(64, 48));
}

void MediaPlayerCoreTests::publishesVideoFramesFromAudioClock() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeTestAudioVideoFile(dir.filePath(QStringLiteral("sample.avi")));
  QVERIFY(!path.isEmpty());

  MediaPlayerCore player;
  std::atomic_size_t callbackCount(0);
  player.setVideoFrameCallback([&](const QImage& image, qint64 audioClockMs) {
    if (!image.isNull() && audioClockMs >= 0) {
      callbackCount.fetch_add(1);
    }
  });

  QVERIFY(player.open(path));
  QVERIFY(player.play());

  QTRY_VERIFY_WITH_TIMEOUT(player.audioClockMs() > 0, 2000);
  QTRY_VERIFY_WITH_TIMEOUT(callbackCount.load() > 0, 3000);
}

QTEST_MAIN(MediaPlayerCoreTests)
#include "MediaPlayerCoreTests.moc"
