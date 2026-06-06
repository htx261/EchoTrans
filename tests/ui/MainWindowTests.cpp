#include <QtTest/QtTest>

#include "ui/MainWindow.h"

#include <QFile>
#include <QTemporaryDir>

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
}

class MainWindowTests : public QObject {
  Q_OBJECT

private slots:
  void startsAndStopsPlaybackFromMediaInfo();
};

void MainWindowTests::startsAndStopsPlaybackFromMediaInfo() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeSilentWavFile(dir.filePath(QStringLiteral("silent.wav")));
  QVERIFY(!path.isEmpty());

  MediaInfo info;
  info.filePath = path;
  info.hasAudio = true;

  MainWindow window;
  QVERIFY(window.startPlayback(info));
  QCOMPARE(window.playbackState(), PlaybackState::Playing);

  window.stopPlayback();
  QCOMPARE(window.playbackState(), PlaybackState::Stopped);
}

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
