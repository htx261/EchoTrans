#include <QtTest/QtTest>

#include "media/FFmpegMediaProbe.h"

#include <QDir>
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

QString writeSilentWav(QTemporaryDir* dir) {
  return writeSilentWavFile(dir->filePath(QStringLiteral("silent.wav")));
}
}

class FFmpegMediaProbeTests : public QObject {
  Q_OBJECT

private slots:
  void rejectsMissingFile();
  void readsAudioFileInfo();
  void readsAudioFileInfoFromUnicodePath();
};

void FFmpegMediaProbeTests::rejectsMissingFile() {
  const MediaProbeResult result = FFmpegMediaProbe::probe(QStringLiteral("Z:/missing/media.mp4"));

  QVERIFY(!result.success);
  QVERIFY(!result.errorMessage.isEmpty());
  QVERIFY(!result.info.hasAudio);
  QVERIFY(!result.info.hasVideo);
}

void FFmpegMediaProbeTests::readsAudioFileInfo() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeSilentWav(&dir);
  QVERIFY(!path.isEmpty());

  const MediaProbeResult result = FFmpegMediaProbe::probe(path);

  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(result.info.filePath, QDir::fromNativeSeparators(path));
  QVERIFY(result.info.durationMs >= 900);
  QVERIFY(!result.info.hasVideo);
  QVERIFY(result.info.hasAudio);
  QCOMPARE(result.info.audioStreamIndex, 0);
}

void FFmpegMediaProbeTests::readsAudioFileInfoFromUnicodePath() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeSilentWavFile(dir.filePath(QStringLiteral("中文 文件.wav")));
  QVERIFY(!path.isEmpty());

  const MediaProbeResult result = FFmpegMediaProbe::probe(path);

  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(result.info.filePath, QDir::fromNativeSeparators(path));
  QVERIFY(result.info.hasAudio);
}

QTEST_MAIN(FFmpegMediaProbeTests)
#include "FFmpegMediaProbeTests.moc"
