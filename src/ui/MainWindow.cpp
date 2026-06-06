#include "ui/MainWindow.h"

#include "core/DependencyReport.h"
#include "media/FFmpegMediaProbe.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QFont>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {
class SeekSlider : public QSlider {
public:
  explicit SeekSlider(QWidget* parent = nullptr)
      : QSlider(Qt::Horizontal, parent) {
  }

protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && maximum() > minimum()) {
      const int value = QStyle::sliderValueFromPosition(
          minimum(),
          maximum(),
          event->pos().x(),
          width());
      setValue(value);
    }

    QSlider::mousePressEvent(event);
  }
};

QString formatDuration(qint64 durationMs) {
  const qint64 totalSeconds = durationMs / 1000;
  const qint64 hours = totalSeconds / 3600;
  const qint64 minutes = (totalSeconds % 3600) / 60;
  const qint64 seconds = totalSeconds % 60;

  return QStringLiteral("%1:%2:%3")
      .arg(hours, 2, 10, QLatin1Char('0'))
      .arg(minutes, 2, 10, QLatin1Char('0'))
      .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString modelsRootPath() {
  return QString::fromUtf8(ECHOTRANS_MODELS_ROOT);
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      openButton_(new QPushButton(QStringLiteral("打开媒体文件"), this)),
      pauseButton_(new QPushButton(QStringLiteral("暂停"), this)),
      stopButton_(new QPushButton(QStringLiteral("停止播放"), this)),
      statusLabel_(new QLabel(this)),
      videoLabel_(new QLabel(this)),
      subtitleLabel_(new QLabel(this)),
      subtitleTimeLabel_(new QLabel(this)),
      transcriptListLabel_(new QLabel(this)),
      mediaInfoLabel_(new QLabel(this)),
      timeLabel_(new QLabel(this)),
      seekSlider_(new SeekSlider(this)),
      transcriptionModelComboBox_(new QComboBox(this)),
      transcriptionLanguageComboBox_(new QComboBox(this)),
      transcriptionThreadSpinBox_(new QSpinBox(this)),
      transcriptionPromptEdit_(new QLineEdit(this)),
      mediaProbeWatcher_(new QFutureWatcher<MediaProbeResult>(this)),
      subtitlePreparationWatcher_(new QFutureWatcher<MediaSubtitlePreparationResult>(this)),
      playbackStatusTimer_(new QTimer(this)) {
  setWindowTitle(QStringLiteral("EchoTrans"));
  resize(1440, 820);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(16, 16, 16, 12);
  layout->setSpacing(12);

  const DependencyReport report = DependencyReport::fromConfiguredPaths();

  statusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  QFont statusFont = statusLabel_->font();
  statusFont.setPointSize(10);
  statusLabel_->setFont(statusFont);
  statusLabel_->setText(report.isReady()
      ? QStringLiteral("依赖正常")
      : QStringLiteral("依赖异常"));

  mediaInfoLabel_->setAlignment(Qt::AlignLeft);
  mediaInfoLabel_->setWordWrap(true);
  mediaInfoLabel_->setText(QStringLiteral("未打开文件"));

  videoLabel_->setAlignment(Qt::AlignCenter);
  videoLabel_->setMinimumSize(640, 360);
  videoLabel_->setStyleSheet(QStringLiteral("background-color: #101010; color: white;"));
  videoLabel_->setText(QStringLiteral("打开媒体文件后显示画面"));

  subtitleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  subtitleLabel_->setWordWrap(true);
  subtitleLabel_->setObjectName(QStringLiteral("subtitleLabel"));
  QFont subtitleFont = subtitleLabel_->font();
  subtitleFont.setPointSize(15);
  subtitleLabel_->setFont(subtitleFont);
  subtitleLabel_->setMinimumHeight(48);
  subtitleLabel_->setStyleSheet(QStringLiteral("color: #111827; background-color: #ffffff;"));

  subtitleTimeLabel_->setObjectName(QStringLiteral("subtitleTimeLabel"));
  subtitleTimeLabel_->setText(QStringLiteral("00:00:00"));
  subtitleTimeLabel_->setStyleSheet(QStringLiteral("color: #667085;"));

  transcriptListLabel_->setObjectName(QStringLiteral("transcriptListLabel"));
  transcriptListLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  transcriptListLabel_->setWordWrap(true);
  transcriptListLabel_->setText(QStringLiteral("暂无转录字幕"));
  transcriptListLabel_->setStyleSheet(QStringLiteral("color: #475467;"));

  stopButton_->setEnabled(false);
  pauseButton_->setEnabled(false);
  pauseButton_->setObjectName(QStringLiteral("pauseButton"));
  seekSlider_->setObjectName(QStringLiteral("seekSlider"));
  seekSlider_->setRange(0, 0);
  timeLabel_->setText(QStringLiteral("00:00:00 / 00:00:00"));

  auto* topToolbar = new QWidget(central);
  topToolbar->setObjectName(QStringLiteral("topToolbar"));
  auto* topToolbarLayout = new QHBoxLayout(topToolbar);
  topToolbarLayout->setContentsMargins(0, 0, 0, 0);
  topToolbarLayout->addWidget(openButton_);
  topToolbarLayout->addWidget(pauseButton_);
  topToolbarLayout->addWidget(stopButton_);
  topToolbarLayout->addStretch(1);
  topToolbarLayout->addWidget(mediaInfoLabel_, 1);
  topToolbarLayout->addWidget(statusLabel_);

  auto* seekLayout = new QHBoxLayout();
  seekLayout->addWidget(seekSlider_, 1);
  seekLayout->addWidget(timeLabel_);

  connect(openButton_, &QPushButton::clicked, this, &MainWindow::openMediaFile);
  connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::togglePauseResume);
  connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopPlayback);
  connect(seekSlider_, &QSlider::sliderReleased, this, &MainWindow::seekToSliderValue);
  connect(mediaProbeWatcher_, &QFutureWatcher<MediaProbeResult>::finished,
      this, &MainWindow::onMediaProbeFinished);
  connect(subtitlePreparationWatcher_, &QFutureWatcher<MediaSubtitlePreparationResult>::finished,
      this, &MainWindow::onSubtitlePreparationFinished);
  connect(playbackStatusTimer_, &QTimer::timeout, this, &MainWindow::updatePlaybackStatus);

  player_.setVideoFrameCallback([this](const QImage& image, qint64) {
    QMetaObject::invokeMethod(this, [this, image]() {
      displayVideoFrame(image);
    }, Qt::QueuedConnection);
  });

  auto* mainWorkspace = new QWidget(central);
  mainWorkspace->setObjectName(QStringLiteral("mainWorkspace"));
  auto* workspaceLayout = new QHBoxLayout(mainWorkspace);
  workspaceLayout->setContentsMargins(0, 0, 0, 0);
  workspaceLayout->setSpacing(12);

  auto* leftPane = new QWidget(mainWorkspace);
  auto* leftLayout = new QVBoxLayout(leftPane);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(10);
  leftLayout->addWidget(videoLabel_, 1);
  leftLayout->addLayout(seekLayout);

  auto* currentSubtitlePanel = new QGroupBox(QStringLiteral("当前字幕"), leftPane);
  currentSubtitlePanel->setObjectName(QStringLiteral("currentSubtitlePanel"));
  auto* currentSubtitleLayout = new QVBoxLayout(currentSubtitlePanel);
  auto* subtitleHeaderLayout = new QHBoxLayout();
  subtitleHeaderLayout->addStretch(1);
  subtitleHeaderLayout->addWidget(subtitleTimeLabel_);
  currentSubtitleLayout->addLayout(subtitleHeaderLayout);
  currentSubtitleLayout->addWidget(subtitleLabel_);
  leftLayout->addWidget(currentSubtitlePanel);

  auto* rightPane = new QWidget(mainWorkspace);
  rightPane->setMinimumWidth(420);
  auto* rightLayout = new QVBoxLayout(rightPane);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(10);
  setupTranscriptionOptions(rightLayout, rightPane);

  auto* transcriptPanel = new QGroupBox(QStringLiteral("转录字幕"), rightPane);
  transcriptPanel->setObjectName(QStringLiteral("transcriptPanel"));
  auto* transcriptLayout = new QVBoxLayout(transcriptPanel);
  transcriptLayout->addWidget(transcriptListLabel_, 1);
  rightLayout->addWidget(transcriptPanel, 1);

  workspaceLayout->addWidget(leftPane, 3);
  workspaceLayout->addWidget(rightPane, 2);

  layout->addWidget(topToolbar);
  layout->addWidget(mainWorkspace, 1);
  setCentralWidget(central);

  statusBar()->showMessage(report.isReady()
      ? QStringLiteral("启动检查通过")
      : QStringLiteral("启动检查失败"));
}

MainWindow::~MainWindow() {
  ++subtitlePreparationGeneration_;
  if (subtitlePreparationWatcher_->isRunning()) {
    subtitlePreparationWatcher_->waitForFinished();
  }
  player_.setVideoFrameCallback(nullptr);
  player_.stop();
}

QLabel* MainWindow::createOptionDescription(const QString& objectName, const QString& text, QWidget* parent) {
  auto* description = new QLabel(text, parent);
  description->setObjectName(objectName);
  description->setWordWrap(true);
  QFont descriptionFont = description->font();
  descriptionFont.setPointSize(std::max(8, descriptionFont.pointSize() - 1));
  description->setFont(descriptionFont);
  description->setStyleSheet(QStringLiteral("color: #666666;"));
  return description;
}

void MainWindow::setupTranscriptionOptions(QVBoxLayout* layout, QWidget* parent) {
  auto* group = new QGroupBox(QStringLiteral("转录选项"), parent);
  group->setObjectName(QStringLiteral("transcriptionOptionsPanel"));
  auto* groupLayout = new QFormLayout(group);
  groupLayout->setLabelAlignment(Qt::AlignRight);
  groupLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  transcriptionModelComboBox_->setObjectName(QStringLiteral("transcriptionModelComboBox"));
  populateTranscriptionModels();
  auto* modelContainer = new QWidget(group);
  auto* modelLayout = new QVBoxLayout(modelContainer);
  modelLayout->setContentsMargins(0, 0, 0, 0);
  modelLayout->addWidget(transcriptionModelComboBox_);
  modelLayout->addWidget(createOptionDescription(
      QStringLiteral("transcriptionModelDescription"),
      QStringLiteral("选择 models/whisper 目录下的 whisper.cpp .bin 模型文件。"),
      modelContainer));
  groupLayout->addRow(QStringLiteral("模型文件"), modelContainer);

  transcriptionLanguageComboBox_->setObjectName(QStringLiteral("transcriptionLanguageComboBox"));
  transcriptionLanguageComboBox_->addItem(QStringLiteral("自动检测"), QString());
  transcriptionLanguageComboBox_->addItem(QStringLiteral("英语"), QStringLiteral("en"));
  transcriptionLanguageComboBox_->addItem(QStringLiteral("中文"), QStringLiteral("zh"));
  transcriptionLanguageComboBox_->addItem(QStringLiteral("日语"), QStringLiteral("ja"));
  transcriptionLanguageComboBox_->addItem(QStringLiteral("韩语"), QStringLiteral("ko"));
  transcriptionLanguageComboBox_->addItem(QStringLiteral("法语"), QStringLiteral("fr"));
  transcriptionLanguageComboBox_->addItem(QStringLiteral("德语"), QStringLiteral("de"));
  auto* languageContainer = new QWidget(group);
  auto* languageLayout = new QVBoxLayout(languageContainer);
  languageLayout->setContentsMargins(0, 0, 0, 0);
  languageLayout->addWidget(transcriptionLanguageComboBox_);
  languageLayout->addWidget(createOptionDescription(
      QStringLiteral("transcriptionLanguageDescription"),
      QStringLiteral("只用于识别语音所属语言；whisper.cpp 不负责翻译。"),
      languageContainer));
  groupLayout->addRow(QStringLiteral("语言"), languageContainer);

  const int maxThreads = TranscriptionOptions::maxThreadCount();
  transcriptionThreadSpinBox_->setObjectName(QStringLiteral("transcriptionThreadSpinBox"));
  transcriptionThreadSpinBox_->setRange(1, maxThreads);
  transcriptionThreadSpinBox_->setValue(TranscriptionOptions::defaults().threadCount);
  auto* threadContainer = new QWidget(group);
  auto* threadLayout = new QVBoxLayout(threadContainer);
  threadLayout->setContentsMargins(0, 0, 0, 0);
  threadLayout->addWidget(transcriptionThreadSpinBox_);
  threadLayout->addWidget(createOptionDescription(
      QStringLiteral("transcriptionThreadDescription"),
      QStringLiteral("控制 whisper.cpp 推理线程数，当前最大可用线程数为 %1。").arg(maxThreads),
      threadContainer));
  groupLayout->addRow(QStringLiteral("线程数"), threadContainer);

  transcriptionPromptEdit_->setObjectName(QStringLiteral("transcriptionPromptEdit"));
  transcriptionPromptEdit_->setPlaceholderText(QStringLiteral("可填写会议主题、课程术语或专有名词"));
  auto* promptContainer = new QWidget(group);
  auto* promptLayout = new QVBoxLayout(promptContainer);
  promptLayout->setContentsMargins(0, 0, 0, 0);
  promptLayout->addWidget(transcriptionPromptEdit_);
  promptLayout->addWidget(createOptionDescription(
      QStringLiteral("transcriptionPromptDescription"),
      QStringLiteral("作为上下文提示词传给 whisper.cpp，提高专有名词识别稳定性。"),
      promptContainer));
  groupLayout->addRow(QStringLiteral("Prompt"), promptContainer);

  layout->addWidget(group);
}

void MainWindow::populateTranscriptionModels() {
  transcriptionModelComboBox_->clear();

  const QDir whisperModelDir(modelsRootPath() + QStringLiteral("/whisper"));
  const QFileInfoList models = whisperModelDir.entryInfoList(
      QStringList() << QStringLiteral("*.bin"),
      QDir::Files,
      QDir::Name);

  for (const QFileInfo& model : models) {
    transcriptionModelComboBox_->addItem(model.fileName(), model.absoluteFilePath());
  }

  if (transcriptionModelComboBox_->count() == 0) {
    transcriptionModelComboBox_->addItem(QStringLiteral("未找到 whisper 模型"), QString());
  }
}

void MainWindow::openMediaFile() {
  if (subtitlePreparationWatcher_->isRunning()) {
    statusBar()->showMessage(QStringLiteral("正在准备字幕，请稍候"));
    return;
  }

  const QString filePath = QFileDialog::getOpenFileName(
      this,
      QStringLiteral("打开媒体文件"),
      QString(),
      QStringLiteral("媒体文件 (*.mp4 *.mkv *.avi *.mov *.mp3 *.wav);;所有文件 (*.*)"));

  if (filePath.isEmpty()) {
    return;
  }

  stopPlayback();
  ++subtitlePreparationGeneration_;
  subtitleTrack_.setSegments({});
  updateSubtitle(0);
  updateTranscriptPanel(0);
  openButton_->setEnabled(false);
  mediaInfoLabel_->setText(QStringLiteral("读取中..."));
  statusBar()->showMessage(QStringLiteral("正在打开媒体文件"));

  mediaProbeWatcher_->setFuture(QtConcurrent::run([filePath]() {
    return FFmpegMediaProbe::probe(filePath);
  }));
}

void MainWindow::onMediaProbeFinished() {
  showMediaInfo(mediaProbeWatcher_->result());
}

void MainWindow::onSubtitlePreparationFinished() {
  openButton_->setEnabled(true);

  const MediaSubtitlePreparationResult result = subtitlePreparationWatcher_->result();
  if (!result.success) {
    statusBar()->showMessage(QStringLiteral("字幕准备失败：%1").arg(result.errorMessage));
    transcriptListLabel_->setText(QStringLiteral("字幕准备失败"));
    return;
  }

  subtitleTrack_ = result.subtitleTrack;
  updateSubtitle(0);
  updateTranscriptPanel(0);
  statusBar()->showMessage(QStringLiteral("字幕准备完成，开始播放"));
  startPlayback(pendingPlaybackInfo_);
}

void MainWindow::showMediaInfo(const MediaProbeResult& result) {
  if (!result.success) {
    mediaInfoLabel_->setText(QStringLiteral("打开失败"));
    statusBar()->showMessage(QStringLiteral("媒体打开失败"));
    return;
  }

  const MediaInfo& info = result.info;
  mediaInfoLabel_->setText(QStringLiteral("%1  %2")
      .arg(QFileInfo(info.filePath).fileName())
      .arg(formatDuration(info.durationMs)));

  if (info.hasAudio) {
    startSubtitlePreparation(info);
  } else {
    openButton_->setEnabled(true);
    statusBar()->showMessage(QStringLiteral("媒体没有音频，直接播放"));
    startPlayback(info);
  }
}

void MainWindow::startSubtitlePreparation(const MediaInfo& info) {
  pendingPlaybackInfo_ = info;
  const int generation = ++subtitlePreparationGeneration_;
  const TranscriptionOptions options = transcriptionOptions();
  if (options.modelPath.trimmed().isEmpty()) {
    openButton_->setEnabled(true);
    statusBar()->showMessage(QStringLiteral("未选择转录模型，无法准备字幕"));
    transcriptListLabel_->setText(QStringLiteral("未选择转录模型"));
    return;
  }

  transcriptListLabel_->setText(QStringLiteral("正在准备字幕"));
  statusBar()->showMessage(QStringLiteral("正在准备字幕"));
  subtitlePreparationWatcher_->setFuture(QtConcurrent::run([this, info, options, generation]() {
    MediaSubtitlePreparer preparer;
    MediaSubtitlePreparationRequest request;
    request.mediaPath = info.filePath;
    request.options = options;
    request.progressCallback = [this, generation](const MediaSubtitlePreparationProgress& progress) {
      QMetaObject::invokeMethod(this, [this, generation, progress]() {
        updateSubtitlePreparationProgress(generation, progress);
      }, Qt::QueuedConnection);
    };
    return preparer.prepare(request);
  }));
}

void MainWindow::updateSubtitlePreparationProgress(
    int generation,
    const MediaSubtitlePreparationProgress& progress) {
  if (generation != subtitlePreparationGeneration_) {
    return;
  }

  const QString message = progress.percent > 0
      ? QStringLiteral("%1 %2%").arg(progress.message).arg(progress.percent)
      : progress.message;
  statusBar()->showMessage(message);
  transcriptListLabel_->setText(message);
}

bool MainWindow::startPlayback(const MediaInfo& info) {
  player_.stop();
  playbackStatusTimer_->stop();
  pauseButton_->setEnabled(false);
  stopButton_->setEnabled(false);
  displayedVideoFrameCount_ = 0;
  durationMs_ = info.durationMs;
  currentHasAudio_ = info.hasAudio;
  currentHasVideo_ = info.hasVideo;
  pendingSeekPositionMs_ = -1;
  seekSlider_->setRange(0, static_cast<int>(durationMs_));
  seekSlider_->setValue(0);
  updateTimeLabel(0);
  updateSubtitle(0);
  updateTranscriptPanel(0);

  if (!info.hasAudio && !info.hasVideo) {
    statusBar()->showMessage(QStringLiteral("媒体没有可播放的音视频流"));
    return false;
  }

  if (!player_.open(info.filePath)) {
    statusBar()->showMessage(QStringLiteral("播放器打开失败"));
    return false;
  }

  if (!player_.play()) {
    statusBar()->showMessage(QStringLiteral("播放器启动失败"));
    return false;
  }

  stopButton_->setEnabled(true);
  pauseButton_->setEnabled(true);
  pauseButton_->setText(QStringLiteral("暂停"));
  playbackStatusTimer_->start(300);
  if (info.hasVideo) {
    videoLabel_->setText(QStringLiteral("正在准备视频画面"));
  } else {
    videoLabel_->setText(QStringLiteral("当前媒体没有视频画面"));
  }
  updatePlaybackStatus();
  return true;
}

void MainWindow::stopPlayback() {
  playbackStatusTimer_->stop();
  player_.stop();
  pendingSeekPositionMs_ = -1;
  currentHasAudio_ = false;
  currentHasVideo_ = false;
  pauseButton_->setEnabled(false);
  pauseButton_->setText(QStringLiteral("暂停"));
  stopButton_->setEnabled(false);
  seekSlider_->setValue(0);
  updateTimeLabel(0);
  updateSubtitle(0);
  updateTranscriptPanel(0);
  videoLabel_->setText(QStringLiteral("打开媒体文件后显示画面"));
  mediaInfoLabel_->setText(QStringLiteral("未打开文件"));
  statusBar()->showMessage(QStringLiteral("播放已停止"));
}

PlaybackState MainWindow::playbackState() const {
  return player_.state();
}

std::size_t MainWindow::displayedVideoFrameCount() const {
  return displayedVideoFrameCount_;
}

bool MainWindow::seekInProgress() const {
  return player_.seekInProgress();
}

void MainWindow::setSubtitleTrack(const SubtitleTrack& track) {
  subtitleTrack_ = track;
  updateSubtitle(player_.audioClockMs());
  updateTranscriptPanel(player_.audioClockMs());
}

TranscriptionOptions MainWindow::transcriptionOptions() const {
  TranscriptionOptions options = TranscriptionOptions::defaults();
  options.modelPath = transcriptionModelComboBox_
      ? transcriptionModelComboBox_->currentData().toString()
      : QString();
  options.languageCode = transcriptionLanguageComboBox_
      ? transcriptionLanguageComboBox_->currentData().toString()
      : QString();
  options.threadCount = transcriptionThreadSpinBox_
      ? transcriptionThreadSpinBox_->value()
      : options.threadCount;
  options.initialPrompt = transcriptionPromptEdit_
      ? transcriptionPromptEdit_->text()
      : QString();
  options.timestampsEnabled = true;
  return options;
}

void MainWindow::updatePlaybackStatus() {
  QString fatalError;
  if (!player_.lastDemuxError().isEmpty()) {
    fatalError = QStringLiteral("解封装失败：%1").arg(player_.lastDemuxError());
  } else if (currentHasAudio_ && !player_.lastAudioDecodeError().isEmpty()) {
    fatalError = QStringLiteral("音频解码失败：%1").arg(player_.lastAudioDecodeError());
  } else if (currentHasVideo_ && !player_.lastVideoDecodeError().isEmpty()) {
    fatalError = QStringLiteral("视频解码失败：%1").arg(player_.lastVideoDecodeError());
  }

  if (!fatalError.isEmpty()) {
    statusBar()->showMessage(fatalError);
    if (player_.activeWorkerCount() == 0) {
      playbackStatusTimer_->stop();
      player_.stop();
      pauseButton_->setEnabled(false);
      pauseButton_->setText(QStringLiteral("暂停"));
      stopButton_->setEnabled(false);
      videoLabel_->setText(QStringLiteral("播放异常"));
      statusBar()->showMessage(fatalError);
    }
    return;
  }

  if (player_.state() == PlaybackState::Stopped) {
    playbackStatusTimer_->stop();
    pauseButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    return;
  }

  const bool isSeeking = player_.seekInProgress();
  if (!isSeeking) {
    pendingSeekPositionMs_ = -1;
  }

  qint64 positionMs = isSeeking && pendingSeekPositionMs_ >= 0
      ? pendingSeekPositionMs_
      : player_.audioClockMs();
  if (player_.state() == PlaybackState::Paused && player_.activeWorkerCount() == 0) {
    positionMs = 0;
  }
  if (!seekSlider_->isSliderDown() && durationMs_ > 0) {
    seekSlider_->setValue(static_cast<int>(std::min(positionMs, durationMs_)));
  }
  updateTimeLabel(positionMs);
  updateSubtitle(positionMs);
  updateTranscriptPanel(positionMs);

  pauseButton_->setText(player_.state() == PlaybackState::Paused
      ? QStringLiteral("继续")
      : QStringLiteral("暂停"));

  if (isSeeking) {
    statusBar()->showMessage(QStringLiteral("正在跳转..."));
    return;
  }

  if (player_.state() == PlaybackState::Paused) {
    statusBar()->showMessage(QStringLiteral("已暂停"));
    return;
  }

  if (currentHasAudio_ && !player_.lastAudioOutputError().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("音频输出失败：%1").arg(player_.lastAudioOutputError()));
    return;
  }

  if (player_.audioOutputByteCount() > 0 && displayedVideoFrameCount_ > 0) {
    statusBar()->showMessage(QStringLiteral("正在播放音频和视频"));
    return;
  }

  if (displayedVideoFrameCount_ > 0) {
    statusBar()->showMessage(QStringLiteral("正在播放视频"));
    return;
  }

  if (player_.audioOutputByteCount() > 0) {
    statusBar()->showMessage(QStringLiteral("正在播放音频"));
    return;
  }

  if (player_.decodedAudioFrameCount() > 0) {
    statusBar()->showMessage(QStringLiteral("音频已解码，等待输出"));
    return;
  }

  statusBar()->showMessage(QStringLiteral("正在准备音频播放"));
}

void MainWindow::displayVideoFrame(const QImage& image) {
  if (image.isNull()) {
    return;
  }

  videoLabel_->setPixmap(QPixmap::fromImage(image).scaled(
      videoLabel_->size(),
      Qt::KeepAspectRatio,
      Qt::SmoothTransformation));
  ++displayedVideoFrameCount_;
}

void MainWindow::togglePauseResume() {
  if (player_.state() == PlaybackState::Paused) {
    player_.resume();
  } else {
    player_.pause();
  }

  updatePlaybackStatus();
}

void MainWindow::seekToSliderValue() {
  if (durationMs_ <= 0) {
    return;
  }

  pendingSeekPositionMs_ = seekSlider_->value();
  updateSubtitle(pendingSeekPositionMs_);
  updateTranscriptPanel(pendingSeekPositionMs_);
  if (!player_.seekTo(pendingSeekPositionMs_) && !player_.seekInProgress()) {
    pendingSeekPositionMs_ = -1;
  }
  updatePlaybackStatus();
}

void MainWindow::updateTimeLabel(qint64 positionMs) {
  const qint64 boundedPosition = durationMs_ > 0
      ? std::min(positionMs, durationMs_)
      : positionMs;
  timeLabel_->setText(QStringLiteral("%1 / %2")
      .arg(formatDuration(boundedPosition))
      .arg(formatDuration(durationMs_)));
}

void MainWindow::updateSubtitle(qint64 positionMs) {
  if (!subtitleLabel_) {
    return;
  }

  const QString text = subtitleTrack_.textAt(positionMs);
  subtitleTimeLabel_->setText(formatDuration(positionMs));
  subtitleLabel_->setText(text.isEmpty()
      ? QStringLiteral("暂无当前字幕")
      : text);
}

void MainWindow::updateTranscriptPanel(qint64 positionMs) {
  if (!transcriptListLabel_) {
    return;
  }

  const QVector<SubtitleSegment> segments = subtitleTrack_.segments();
  if (segments.isEmpty()) {
    transcriptListLabel_->setText(QStringLiteral("暂无转录字幕"));
    return;
  }

  QStringList lines;
  const int firstIndex = std::max(0, segments.size() - 8);
  for (int index = firstIndex; index < segments.size(); ++index) {
    const SubtitleSegment& segment = segments[index];
    const QString text = segment.translatedText.isEmpty()
        ? segment.sourceText
        : segment.translatedText;
    lines.push_back(QStringLiteral("#%1  %2 -> %3\n%4")
        .arg(index + 1)
        .arg(formatDuration(segment.startMs))
        .arg(formatDuration(segment.endMs))
        .arg(text));
  }

  const QString currentText = subtitleTrack_.textAt(positionMs);
  transcriptListLabel_->setText(currentText.isEmpty()
      ? lines.join(QStringLiteral("\n\n"))
      : QStringLiteral("当前：%1\n\n%2").arg(currentText, lines.join(QStringLiteral("\n\n"))));
}
