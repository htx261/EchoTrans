#include <QtTest/QtTest>

#include "subtitle/SubtitleTrack.h"
#include "ui/MainWindow.h"

#include <QFile>
#include <QCoreApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
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
  void showsBaiduTranslationSettings();
  void savesBaiduTranslationSettings();
  void switchesTranslationSettingsByTaskType();
  void importsWhisperModelAtRuntime();
  void liveInterpretationModeShowsAccuracyWarning();
  void liveInterpretationStartsPlaybackBeforeSubtitlesFinish();
  void showsLatestLiveSubtitleWhenRecognitionLagsPlayback();
  void showsTaskModeAndCancelControls();
  void placesPlaybackControlsBelowVideo();
  void overlaysSubtitleOnVideo();
  void constrainsGrowingSubtitleText();
  void translateButtonStartsTranslationTaskFlow();
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

void MainWindowTests::showsBaiduTranslationSettings() {
  MainWindow window;

  auto* appIdEdit = window.findChild<QLineEdit*>(QStringLiteral("baiduAppIdEdit"));
  auto* secretKeyEdit = window.findChild<QLineEdit*>(QStringLiteral("baiduSecretKeyEdit"));
  auto* saveButton = window.findChild<QPushButton*>(QStringLiteral("saveBaiduSettingsButton"));
  auto* description = window.findChild<QLabel*>(QStringLiteral("baiduTranslationDescription"));

  QVERIFY(appIdEdit);
  QVERIFY(secretKeyEdit);
  QVERIFY(saveButton);
  QVERIFY(description);
  QVERIFY(description->text().contains(QStringLiteral("百度")));
}

void MainWindowTests::savesBaiduTranslationSettings() {
  QCoreApplication::setOrganizationName(QStringLiteral("EchoTransTests"));
  QCoreApplication::setApplicationName(QStringLiteral("MainWindowBaiduSettings"));
  QSettings().clear();

  MainWindow window;
  auto* appIdEdit = window.findChild<QLineEdit*>(QStringLiteral("baiduAppIdEdit"));
  auto* secretKeyEdit = window.findChild<QLineEdit*>(QStringLiteral("baiduSecretKeyEdit"));
  auto* saveButton = window.findChild<QPushButton*>(QStringLiteral("saveBaiduSettingsButton"));
  QVERIFY(appIdEdit);
  QVERIFY(secretKeyEdit);
  QVERIFY(saveButton);

  appIdEdit->setText(QStringLiteral("test-app-id"));
  secretKeyEdit->setText(QStringLiteral("test-secret-key"));
  QTest::mouseClick(saveButton, Qt::LeftButton);

  QSettings settings;
  QCOMPARE(settings.value(QStringLiteral("baiduTranslator/appId")).toString(), QStringLiteral("test-app-id"));
  QCOMPARE(settings.value(QStringLiteral("baiduTranslator/secretKey")).toString(), QStringLiteral("test-secret-key"));
  settings.clear();
}

void MainWindowTests::switchesTranslationSettingsByTaskType() {
  MainWindow window;

  auto* taskTypeComboBox = window.findChild<QComboBox*>(QStringLiteral("taskTypeComboBox"));
  auto* startTaskButton = window.findChild<QPushButton*>(QStringLiteral("startTaskButton"));
  auto* translationSettingsPanel = window.findChild<QWidget*>(QStringLiteral("translationSettingsPanel"));
  QVERIFY(taskTypeComboBox);
  QVERIFY(startTaskButton);
  QVERIFY(translationSettingsPanel);

  QVERIFY(taskTypeComboBox->findData(QStringLiteral("direct_play")) >= 0);
  QCOMPARE(taskTypeComboBox->currentData().toString(), QStringLiteral("direct_play"));
  QVERIFY(translationSettingsPanel->isHidden());

  taskTypeComboBox->setCurrentIndex(taskTypeComboBox->findData(QStringLiteral("translate")));
  QVERIFY(!translationSettingsPanel->isHidden());
}

void MainWindowTests::liveInterpretationModeShowsAccuracyWarning() {
  MainWindow window;

  auto* taskTypeComboBox = window.findChild<QComboBox*>(QStringLiteral("taskTypeComboBox"));
  auto* translationSettingsPanel = window.findChild<QWidget*>(QStringLiteral("translationSettingsPanel"));
  auto* liveDescription = window.findChild<QLabel*>(QStringLiteral("liveInterpretationDescription"));
  QVERIFY(taskTypeComboBox);
  QVERIFY(translationSettingsPanel);
  QVERIFY(liveDescription);

  taskTypeComboBox->setCurrentIndex(taskTypeComboBox->findData(QStringLiteral("live_interpretation")));

  QVERIFY(translationSettingsPanel->isHidden());
  QVERIFY(!liveDescription->isHidden());
  QVERIFY(liveDescription->text().contains(QStringLiteral("实时转录")));
  QVERIFY(liveDescription->text().contains(QStringLiteral("不如预处理字幕")));
}

void MainWindowTests::liveInterpretationStartsPlaybackBeforeSubtitlesFinish() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString mediaPath = writeTestAudioVideoFile(dir.filePath(QStringLiteral("sample.avi")), 3);
  QVERIFY(!mediaPath.isEmpty());
  const QString modelPath = dir.filePath(QStringLiteral("fake-model.bin"));
  QFile modelFile(modelPath);
  QVERIFY(modelFile.open(QIODevice::WriteOnly));
  modelFile.write("fake model");
  modelFile.close();

  MainWindow window;

  MediaInfo info;
  info.filePath = mediaPath;
  info.durationMs = 3000;
  info.hasAudio = true;
  info.hasVideo = true;
  window.setPendingPlaybackInfoForTest(info);

  auto* modelComboBox = window.findChild<QComboBox*>(QStringLiteral("transcriptionModelComboBox"));
  auto* taskTypeComboBox = window.findChild<QComboBox*>(QStringLiteral("taskTypeComboBox"));
  auto* startTaskButton = window.findChild<QPushButton*>(QStringLiteral("startTaskButton"));
  auto* cancelButton = window.findChild<QPushButton*>(QStringLiteral("cancelTaskButton"));
  QVERIFY(modelComboBox);
  QVERIFY(taskTypeComboBox);
  QVERIFY(startTaskButton);
  QVERIFY(cancelButton);

  modelComboBox->clear();
  modelComboBox->addItem(QStringLiteral("fake-model.bin"), modelPath);
  taskTypeComboBox->setCurrentIndex(taskTypeComboBox->findData(QStringLiteral("live_interpretation")));
  startTaskButton->setEnabled(true);

  QTest::mouseClick(startTaskButton, Qt::LeftButton);

  QCOMPARE(window.playbackState(), PlaybackState::Playing);
  QVERIFY(cancelButton->isEnabled());
}

void MainWindowTests::showsLatestLiveSubtitleWhenRecognitionLagsPlayback() {
  MainWindow window;

  auto* subtitleLabel = window.findChild<QLabel*>(QStringLiteral("subtitleLabel"));
  QVERIFY(subtitleLabel);

  window.displayLiveSubtitleSegmentForTest(
      SubtitleSegment{
          0,
          1000,
          QStringLiteral("recognized subtitle"),
          QString()},
      2500);

  QCOMPARE(subtitleLabel->text(), QStringLiteral("recognized subtitle"));
}

void MainWindowTests::importsWhisperModelAtRuntime() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString sourcePath = dir.filePath(QStringLiteral("ggml-test-import.bin"));
  QFile sourceFile(sourcePath);
  QVERIFY(sourceFile.open(QIODevice::WriteOnly));
  sourceFile.write("test model");
  sourceFile.close();

  MainWindow window;
  QVERIFY(window.importWhisperModelForTest(sourcePath));

  auto* modelComboBox = window.findChild<QComboBox*>(QStringLiteral("transcriptionModelComboBox"));
  QVERIFY(modelComboBox);

  const QString importedPath = modelComboBox->currentData().toString();
  QVERIFY(importedPath.endsWith(QStringLiteral("ggml-test-import.bin")));
  QVERIFY(QFile::exists(importedPath));
  QFile::remove(importedPath);
}

void MainWindowTests::showsTaskModeAndCancelControls() {
  MainWindow window;

  auto* startTaskButton = window.findChild<QPushButton*>(QStringLiteral("startTaskButton"));
  auto* cancelButton = window.findChild<QPushButton*>(QStringLiteral("cancelTaskButton"));
  auto* taskOptionsPanel = window.findChild<QWidget*>(QStringLiteral("taskOptionsPanel"));

  QVERIFY(startTaskButton);
  QVERIFY(cancelButton);
  QVERIFY(taskOptionsPanel);
  QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("directPlayButton")));
  QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("transcribeButton")));
  QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("translateSubtitleButton")));
  QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("cancelSubtitlePreparationButton")));
  QCOMPARE(cancelButton->parentWidget(), taskOptionsPanel);
  QVERIFY(!startTaskButton->isEnabled());
  QVERIFY(!cancelButton->isEnabled());
}

void MainWindowTests::placesPlaybackControlsBelowVideo() {
  MainWindow window;

  auto* playbackControls = window.findChild<QWidget*>(QStringLiteral("playbackControls"));
  auto* pauseButton = window.findChild<QPushButton*>(QStringLiteral("pauseButton"));
  auto* stopButton = window.findChild<QPushButton*>(QStringLiteral("stopButton"));
  auto* videoContainer = window.findChild<QWidget*>(QStringLiteral("videoContainer"));
  QVERIFY(playbackControls);
  QVERIFY(pauseButton);
  QVERIFY(stopButton);
  QVERIFY(videoContainer);
  QCOMPARE(pauseButton->parentWidget(), playbackControls);
  QCOMPARE(stopButton->parentWidget(), playbackControls);
}

void MainWindowTests::overlaysSubtitleOnVideo() {
  MainWindow window;

  auto* subtitleLabel = window.findChild<QLabel*>(QStringLiteral("subtitleLabel"));
  auto* videoContainer = window.findChild<QWidget*>(QStringLiteral("videoContainer"));
  QVERIFY(subtitleLabel);
  QVERIFY(videoContainer);
  QCOMPARE(subtitleLabel->parentWidget(), videoContainer);
  QVERIFY(subtitleLabel->styleSheet().contains(QStringLiteral("rgba")));
  QVERIFY(subtitleLabel->styleSheet().contains(QStringLiteral("color: white")));
}

void MainWindowTests::constrainsGrowingSubtitleText() {
  MainWindow window;

  auto* subtitleLabel = window.findChild<QLabel*>(QStringLiteral("subtitleLabel"));
  auto* transcriptScrollArea = window.findChild<QScrollArea*>(QStringLiteral("transcriptScrollArea"));
  auto* transcriptListLabel = window.findChild<QLabel*>(QStringLiteral("transcriptListLabel"));
  QVERIFY(subtitleLabel);
  QVERIFY(transcriptScrollArea);
  QVERIFY(transcriptListLabel);

  QVERIFY(subtitleLabel->maximumHeight() > 0);
  QVERIFY(subtitleLabel->maximumHeight() <= 96);
  QCOMPARE(transcriptScrollArea->widget(), transcriptListLabel);
  QCOMPARE(transcriptScrollArea->widgetResizable(), true);
}

void MainWindowTests::translateButtonStartsTranslationTaskFlow() {
  MainWindow window;

  MediaInfo info;
  info.filePath = QStringLiteral("Z:/missing/media.mp4");
  info.hasAudio = true;
  window.setPendingPlaybackInfoForTest(info);

  auto* modelComboBox = window.findChild<QComboBox*>(QStringLiteral("transcriptionModelComboBox"));
  auto* taskTypeComboBox = window.findChild<QComboBox*>(QStringLiteral("taskTypeComboBox"));
  auto* startTaskButton = window.findChild<QPushButton*>(QStringLiteral("startTaskButton"));
  auto* cancelButton = window.findChild<QPushButton*>(QStringLiteral("cancelTaskButton"));
  QVERIFY(modelComboBox);
  QVERIFY(taskTypeComboBox);
  QVERIFY(startTaskButton);
  QVERIFY(cancelButton);

  modelComboBox->clear();
  modelComboBox->addItem(QStringLiteral("missing.bin"), QStringLiteral("Z:/missing/ggml-base.bin"));
  taskTypeComboBox->setCurrentIndex(taskTypeComboBox->findData(QStringLiteral("translate")));
  startTaskButton->setEnabled(true);

  QTest::mouseClick(startTaskButton, Qt::LeftButton);

  QVERIFY(cancelButton->isEnabled());
  QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("翻译字幕")));
  QTRY_VERIFY_WITH_TIMEOUT(window.statusBar()->currentMessage().contains(QStringLiteral("失败")), 3000);
}

void MainWindowTests::usesCompactWorkspaceLayout() {
  MainWindow window;

  QVERIFY(window.findChild<QWidget*>(QStringLiteral("topToolbar")));
  QVERIFY(window.findChild<QWidget*>(QStringLiteral("mainWorkspace")));
  QVERIFY(window.findChild<QWidget*>(QStringLiteral("videoContainer")));
  QVERIFY(window.findChild<QWidget*>(QStringLiteral("transcriptPanel")));

  const auto labels = window.findChildren<QLabel*>();
  for (QLabel* label : labels) {
    QVERIFY(label->text() != QStringLiteral("AI 同声传译助手"));
  }
}

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
