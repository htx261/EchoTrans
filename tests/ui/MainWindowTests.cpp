#include <QtTest/QtTest>

#include "subtitle/SubtitleTrack.h"
#include "ui/MainWindow.h"

#include <QFile>
#include <QCoreApplication>
#include <QLabel>
#include <QSpinBox>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
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

QString writeTestAudioVideoFile(const QString& path, int durationSeconds = 1) {
  const QString ffmpegPath = QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe");
  if (!QFile::exists(ffmpegPath)) {
    return QString();
  }

  QProcess ffmpeg;
  ffmpeg.start(ffmpegPath, QStringList()
      << QStringLiteral("-y")
      << QStringLiteral("-f") << QStringLiteral("lavfi")
      << QStringLiteral("-i") << QStringLiteral("testsrc=size=64x48:rate=10:duration=%1").arg(durationSeconds)
      << QStringLiteral("-f") << QStringLiteral("lavfi")
      << QStringLiteral("-i") << QStringLiteral("sine=frequency=440:duration=%1").arg(durationSeconds)
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

class MainWindowTests : public QObject {
  Q_OBJECT

private slots:
  void startsAndStopsPlaybackFromMediaInfo();
  void displaysVideoFramesFromPlayback();
  void pauseButtonTogglesPauseAndResume();
  void seekSliderClickAndDragRequestSeek();
  void updatesControlsWhenPlaybackFinishes();
  void stopsPlaybackWhenMediaOpenFails();
  void displaysSubtitleForCurrentPlaybackPosition();
  void showsTranscriptionOptionsWithDescriptions();
  void showsTaskModeAndCancelControls();
  void usesCompactWorkspaceLayout();
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

void MainWindowTests::displaysVideoFramesFromPlayback() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeTestAudioVideoFile(dir.filePath(QStringLiteral("sample.avi")));
  QVERIFY(!path.isEmpty());

  MediaInfo info;
  info.filePath = path;
  info.hasAudio = true;
  info.hasVideo = true;

  MainWindow window;
  window.resize(640, 480);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));

  QVERIFY(window.startPlayback(info));
  QTRY_VERIFY_WITH_TIMEOUT(window.displayedVideoFrameCount() > 0, 3000);

  window.stopPlayback();
}

void MainWindowTests::pauseButtonTogglesPauseAndResume() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeTestAudioVideoFile(dir.filePath(QStringLiteral("sample.avi")), 3);
  QVERIFY(!path.isEmpty());

  MediaInfo info;
  info.filePath = path;
  info.durationMs = 3000;
  info.hasAudio = true;
  info.hasVideo = true;

  MainWindow window;
  QVERIFY(window.startPlayback(info));

  auto* pauseButton = window.findChild<QPushButton*>(QStringLiteral("pauseButton"));
  QVERIFY(pauseButton);

  QTest::mouseClick(pauseButton, Qt::LeftButton);
  QCOMPARE(window.playbackState(), PlaybackState::Paused);

  QTest::mouseClick(pauseButton, Qt::LeftButton);
  QCOMPARE(window.playbackState(), PlaybackState::Playing);
}

void MainWindowTests::seekSliderClickAndDragRequestSeek() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeTestAudioVideoFile(dir.filePath(QStringLiteral("sample.avi")), 4);
  QVERIFY(!path.isEmpty());

  MediaInfo info;
  info.filePath = path;
  info.durationMs = 4000;
  info.hasAudio = true;
  info.hasVideo = true;

  MainWindow window;
  window.resize(640, 480);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QVERIFY(window.startPlayback(info));

  auto* seekSlider = window.findChild<QSlider*>(QStringLiteral("seekSlider"));
  QVERIFY(seekSlider);

  seekSlider->setSliderDown(true);
  seekSlider->setValue(3000);
  QTest::qWait(100);
  QVERIFY(!window.seekInProgress());

  seekSlider->setSliderDown(false);
  QMetaObject::invokeMethod(seekSlider, "sliderReleased", Qt::DirectConnection);

  QTRY_VERIFY_WITH_TIMEOUT(window.seekInProgress(), 1000);
}

void MainWindowTests::updatesControlsWhenPlaybackFinishes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeTestAudioVideoFile(dir.filePath(QStringLiteral("sample.avi")));
  QVERIFY(!path.isEmpty());

  MediaInfo info;
  info.filePath = path;
  info.durationMs = 1000;
  info.hasAudio = true;
  info.hasVideo = true;

  MainWindow window;
  QVERIFY(window.startPlayback(info));

  auto* pauseButton = window.findChild<QPushButton*>(QStringLiteral("pauseButton"));
  auto* seekSlider = window.findChild<QSlider*>(QStringLiteral("seekSlider"));
  QVERIFY(pauseButton);
  QVERIFY(seekSlider);

  QTRY_COMPARE_WITH_TIMEOUT(window.playbackState(), PlaybackState::Paused, 4000);
  QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("已暂停"), 3000);
  QVERIFY(pauseButton->isEnabled());
  QCOMPARE(pauseButton->text(), QStringLiteral("继续"));
  QCOMPARE(seekSlider->value(), 0);
}

void MainWindowTests::stopsPlaybackWhenMediaOpenFails() {
  MediaInfo info;
  info.filePath = QStringLiteral("Z:/missing/media.wav");
  info.durationMs = 1000;
  info.hasAudio = true;

  MainWindow window;
  QVERIFY(window.startPlayback(info));

  QTRY_VERIFY_WITH_TIMEOUT(window.statusBar()->currentMessage().contains(QStringLiteral("失败")), 3000);
  QTRY_COMPARE_WITH_TIMEOUT(window.playbackState(), PlaybackState::Stopped, 3000);
}

void MainWindowTests::displaysSubtitleForCurrentPlaybackPosition() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString path = writeTestAudioVideoFile(dir.filePath(QStringLiteral("sample.avi")), 4);
  QVERIFY(!path.isEmpty());

  MediaInfo info;
  info.filePath = path;
  info.durationMs = 4000;
  info.hasAudio = true;
  info.hasVideo = true;

  SubtitleTrack track;
  track.setSegments({
      SubtitleSegment{0, 1000, QStringLiteral("Opening"), QStringLiteral("开场")},
      SubtitleSegment{1000, 2500, QStringLiteral("Architecture"), QStringLiteral("架构")}
  });

  MainWindow window;
  window.setSubtitleTrack(track);
  QVERIFY(window.startPlayback(info));

  auto* subtitleLabel = window.findChild<QLabel*>(QStringLiteral("subtitleLabel"));
  auto* seekSlider = window.findChild<QSlider*>(QStringLiteral("seekSlider"));
  QVERIFY(subtitleLabel);
  QVERIFY(seekSlider);

  seekSlider->setValue(1500);
  QMetaObject::invokeMethod(seekSlider, "sliderReleased", Qt::DirectConnection);

  QCOMPARE(subtitleLabel->text(), QStringLiteral("架构"));
}

void MainWindowTests::showsTranscriptionOptionsWithDescriptions() {
  MainWindow window;

  const QStringList descriptionObjectNames = {
      QStringLiteral("transcriptionModelDescription"),
      QStringLiteral("transcriptionLanguageDescription"),
      QStringLiteral("transcriptionThreadDescription"),
      QStringLiteral("transcriptionPromptDescription")
  };

  for (const QString& objectName : descriptionObjectNames) {
    auto* description = window.findChild<QLabel*>(objectName);
    QVERIFY2(description, qPrintable(objectName));
    QVERIFY2(!description->text().trimmed().isEmpty(), qPrintable(objectName));
  }

  auto* threadSpinBox = window.findChild<QSpinBox*>(QStringLiteral("transcriptionThreadSpinBox"));
  auto* threadDescription = window.findChild<QLabel*>(QStringLiteral("transcriptionThreadDescription"));
  QVERIFY(threadSpinBox);
  QVERIFY(threadDescription);
  QVERIFY(threadSpinBox->maximum() >= threadSpinBox->value());
  QVERIFY(threadDescription->text().contains(QString::number(threadSpinBox->maximum())));

  QVERIFY(!window.findChild<QWidget*>(QStringLiteral("transcriptionTranslateCheckBox")));
  QVERIFY(!window.findChild<QWidget*>(QStringLiteral("transcriptionWindowSpinBox")));
}

void MainWindowTests::showsTaskModeAndCancelControls() {
  MainWindow window;

  auto* directPlayButton = window.findChild<QPushButton*>(QStringLiteral("directPlayButton"));
  auto* transcribeButton = window.findChild<QPushButton*>(QStringLiteral("transcribeButton"));
  auto* translateButton = window.findChild<QPushButton*>(QStringLiteral("translateSubtitleButton"));
  auto* cancelButton = window.findChild<QPushButton*>(QStringLiteral("cancelSubtitlePreparationButton"));

  QVERIFY(directPlayButton);
  QVERIFY(transcribeButton);
  QVERIFY(translateButton);
  QVERIFY(cancelButton);
  QVERIFY(!directPlayButton->isEnabled());
  QVERIFY(!transcribeButton->isEnabled());
  QVERIFY(!translateButton->isEnabled());
  QVERIFY(!cancelButton->isEnabled());
}

void MainWindowTests::usesCompactWorkspaceLayout() {
  MainWindow window;

  QVERIFY(window.findChild<QWidget*>(QStringLiteral("topToolbar")));
  QVERIFY(window.findChild<QWidget*>(QStringLiteral("mainWorkspace")));
  QVERIFY(window.findChild<QWidget*>(QStringLiteral("currentSubtitlePanel")));
  QVERIFY(window.findChild<QWidget*>(QStringLiteral("transcriptPanel")));

  const auto labels = window.findChildren<QLabel*>();
  for (QLabel* label : labels) {
    QVERIFY(label->text() != QStringLiteral("AI 同声传译助手"));
  }
}

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
