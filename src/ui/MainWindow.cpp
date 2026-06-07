#include "ui/MainWindow.h"

#include "core/DependencyReport.h"
#include "media/FFmpegMediaProbe.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFont>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMetaObject>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSizePolicy>
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

QString baiduSourceLanguageForWhisperLanguage(const QString& languageCode) {
  if (languageCode == QStringLiteral("ja")) {
    return QStringLiteral("jp");
  }
  if (languageCode == QStringLiteral("ko")) {
    return QStringLiteral("kor");
  }
  if (languageCode == QStringLiteral("fr")) {
    return QStringLiteral("fra");
  }
  return languageCode.trimmed().isEmpty()
      ? QStringLiteral("auto")
      : languageCode;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      openButton_(new QPushButton(QStringLiteral("打开媒体文件"), this)),
      pauseButton_(new QPushButton(QStringLiteral("暂停"), this)),
      cancelTaskButton_(new QPushButton(QStringLiteral("取消任务"), this)),
      startTaskButton_(new QPushButton(QStringLiteral("开始任务"), this)),
      importWhisperModelButton_(new QPushButton(QStringLiteral("导入模型"), this)),
      statusLabel_(new QLabel(this)),
      liveInterpretationDescription_(new QLabel(this)),
      videoLabel_(new QLabel(this)),
      subtitleLabel_(new QLabel(this)),
      subtitleTimeLabel_(new QLabel(this)),
      transcriptListLabel_(new QLabel(this)),
      mediaInfoLabel_(new QLabel(this)),
      timeLabel_(new QLabel(this)),
      seekSlider_(new SeekSlider(this)),
      useTranslationCheckBox_(new QCheckBox(QStringLiteral("使用翻译"), this)),
      taskTypeComboBox_(new QComboBox(this)),
      transcriptionModelComboBox_(new QComboBox(this)),
      transcriptionLanguageComboBox_(new QComboBox(this)),
      transcriptionThreadSpinBox_(new QSpinBox(this)),
      transcriptionPromptEdit_(new QLineEdit(this)),
      baiduAppIdEdit_(new QLineEdit(this)),
      baiduSecretKeyEdit_(new QLineEdit(this)),
      saveBaiduSettingsButton_(new QPushButton(QStringLiteral("保存翻译设置"), this)),
      statusProgressBar_(new QProgressBar(this)),
      mediaProbeWatcher_(new QFutureWatcher<MediaProbeResult>(this)),
      subtitlePreparationWatcher_(new QFutureWatcher<MediaSubtitlePreparationResult>(this)),
      playbackStatusTimer_(new QTimer(this)) {
  setWindowTitle(QStringLiteral("EchoTrans"));
  resize(1440, 820);
  QFont windowFont = font();
  windowFont.setPointSize(std::max(10, windowFont.pointSize() + 1));
  setFont(windowFont);

  auto* central = new QWidget(this);
  central->setObjectName(QStringLiteral("centralWidget"));
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(16, 16, 16, 12);
  layout->setSpacing(12);
  applyLightToolbenchStyle();

  const DependencyReport report = DependencyReport::fromConfiguredPaths();

  statusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  QFont statusFont = statusLabel_->font();
  statusFont.setPointSize(std::max(13, statusFont.pointSize() + 2));
  statusLabel_->setFont(statusFont);
  statusLabel_->setText(report.isReady()
      ? QStringLiteral("依赖正常")
      : QStringLiteral("依赖异常"));
  statusLabel_->setObjectName(report.isReady()
      ? QStringLiteral("statusPillReady")
      : QStringLiteral("statusPillError"));

  QFont statusBarFont = statusBar()->font();
  statusBarFont.setPointSize(std::max(12, statusBarFont.pointSize() + 2));
  statusBar()->setFont(statusBarFont);
  statusProgressBar_->setObjectName(QStringLiteral("statusProgressBar"));
  statusProgressBar_->setFixedWidth(160);
  statusProgressBar_->setRange(0, 100);
  statusProgressBar_->setValue(0);
  statusProgressBar_->hide();
  statusBar()->addPermanentWidget(statusProgressBar_);

  mediaInfoLabel_->setAlignment(Qt::AlignLeft);
  mediaInfoLabel_->setWordWrap(true);
  mediaInfoLabel_->setText(QStringLiteral("未打开文件"));

  videoLabel_->setAlignment(Qt::AlignCenter);
  videoLabel_->setMinimumSize(640, 360);
  videoLabel_->setStyleSheet(QStringLiteral("background-color: #101010; color: white;"));
  videoLabel_->setText(QStringLiteral("打开媒体文件后显示画面"));

  subtitleLabel_->setAlignment(Qt::AlignCenter);
  subtitleLabel_->setWordWrap(true);
  subtitleLabel_->setObjectName(QStringLiteral("subtitleLabel"));
  QFont subtitleFont = subtitleLabel_->font();
  subtitleFont.setPointSize(15);
  subtitleLabel_->setFont(subtitleFont);
  subtitleLabel_->setMinimumHeight(52);
  subtitleLabel_->setMaximumHeight(96);
  subtitleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  subtitleLabel_->setContentsMargins(16, 8, 16, 8);
  subtitleLabel_->setStyleSheet(QStringLiteral(
      "color: white;"
      "background-color: rgba(0, 0, 0, 150);"
      "border-radius: 4px;"));

  subtitleTimeLabel_->setObjectName(QStringLiteral("subtitleTimeLabel"));
  subtitleTimeLabel_->setText(QStringLiteral("00:00:00"));
  subtitleTimeLabel_->setStyleSheet(QStringLiteral("color: #667085;"));
  subtitleTimeLabel_->hide();

  transcriptListLabel_->setObjectName(QStringLiteral("transcriptListLabel"));
  transcriptListLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  transcriptListLabel_->setWordWrap(true);
  transcriptListLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  transcriptListLabel_->setContentsMargins(10, 10, 10, 10);
  transcriptListLabel_->setText(QStringLiteral("暂无转录字幕"));
  transcriptListLabel_->setStyleSheet(QStringLiteral("color: #475467;"));

  pauseButton_->setEnabled(false);
  cancelTaskButton_->setObjectName(QStringLiteral("cancelTaskButton"));
  startTaskButton_->setObjectName(QStringLiteral("startTaskButton"));
  startTaskButton_->setEnabled(false);
  setTaskButtonsEnabled(false);
  cancelTaskButton_->setEnabled(false);
  openButton_->setObjectName(QStringLiteral("openMediaButton"));
  mediaInfoLabel_->setObjectName(QStringLiteral("mediaInfoLabel"));
  pauseButton_->setObjectName(QStringLiteral("pauseButton"));
  useTranslationCheckBox_->setObjectName(QStringLiteral("useTranslationCheckBox"));
  seekSlider_->setObjectName(QStringLiteral("seekSlider"));
  seekSlider_->setRange(0, 0);
  timeLabel_->setObjectName(QStringLiteral("timeLabel"));
  timeLabel_->setText(QStringLiteral("00:00:00 / 00:00:00"));

  auto* topToolbar = new QWidget(central);
  topToolbar->setObjectName(QStringLiteral("topToolbar"));
  auto* topToolbarLayout = new QHBoxLayout(topToolbar);
  topToolbarLayout->setContentsMargins(14, 10, 14, 10);
  auto* titleLabel = new QLabel(QStringLiteral("EchoTrans"), topToolbar);
  titleLabel->setObjectName(QStringLiteral("appTitleLabel"));
  topToolbarLayout->addWidget(titleLabel);
  topToolbarLayout->addStretch(1);
  topToolbarLayout->addWidget(statusLabel_);

  auto* seekLayout = new QHBoxLayout();
  seekLayout->addWidget(seekSlider_, 1);
  seekLayout->addWidget(timeLabel_);

  connect(openButton_, &QPushButton::clicked, this, &MainWindow::openMediaFile);
  connect(startTaskButton_, &QPushButton::clicked, this, &MainWindow::startSelectedTask);
  connect(taskTypeComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
      this, &MainWindow::updateTaskSettingsVisibility);
  connect(useTranslationCheckBox_, &QCheckBox::toggled,
      this, &MainWindow::updateTaskSettingsVisibility);
  connect(importWhisperModelButton_, &QPushButton::clicked, this, &MainWindow::importWhisperModel);
  connect(cancelTaskButton_, &QPushButton::clicked, this, &MainWindow::cancelSubtitlePreparation);
  connect(saveBaiduSettingsButton_, &QPushButton::clicked, this, &MainWindow::saveBaiduTranslationSettings);
  connect(baiduAppIdEdit_, &QLineEdit::textChanged, this, &MainWindow::updateTaskSettingsVisibility);
  connect(baiduSecretKeyEdit_, &QLineEdit::textChanged, this, &MainWindow::updateTaskSettingsVisibility);
  connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::togglePauseResume);
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
  leftPane->setObjectName(QStringLiteral("leftPane"));
  auto* leftLayout = new QVBoxLayout(leftPane);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(10);
  auto* videoContainer = new QWidget(leftPane);
  videoContainer->setObjectName(QStringLiteral("videoContainer"));
  auto* videoLayout = new QGridLayout(videoContainer);
  videoLayout->setContentsMargins(12, 12, 12, 12);
  videoLayout->setSpacing(0);
  videoLayout->addWidget(videoLabel_, 0, 0);
  videoLayout->addWidget(subtitleLabel_, 0, 0, Qt::AlignBottom);
  leftLayout->addWidget(videoContainer, 1);

  auto* playbackControls = new QWidget(leftPane);
  playbackControls->setObjectName(QStringLiteral("playbackControls"));
  auto* playbackControlsLayout = new QHBoxLayout(playbackControls);
  playbackControlsLayout->setContentsMargins(12, 8, 12, 8);
  playbackControlsLayout->setSpacing(12);
  playbackControlsLayout->addWidget(pauseButton_);
  playbackControlsLayout->addLayout(seekLayout, 1);
  leftLayout->addWidget(playbackControls);

  auto* rightPane = new QWidget(mainWorkspace);
  rightPane->setObjectName(QStringLiteral("rightPane"));
  rightPane->setMinimumWidth(420);
  auto* rightLayout = new QVBoxLayout(rightPane);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(10);
  setupTaskOptions(rightLayout, rightPane);
  setupTranscriptionOptions(rightLayout, rightPane);
  setupTranslationOptions(rightLayout, rightPane);

  auto* transcriptPanel = new QGroupBox(QStringLiteral("转录字幕"), rightPane);
  transcriptPanel->setObjectName(QStringLiteral("transcriptPanel"));
  auto* transcriptLayout = new QVBoxLayout(transcriptPanel);
  transcriptLayout->setContentsMargins(12, 14, 12, 12);
  auto* transcriptScrollArea = new QScrollArea(transcriptPanel);
  transcriptScrollArea->setObjectName(QStringLiteral("transcriptScrollArea"));
  transcriptScrollArea->setWidgetResizable(true);
  transcriptScrollArea->setFrameShape(QFrame::NoFrame);
  transcriptScrollArea->setWidget(transcriptListLabel_);
  transcriptLayout->addWidget(transcriptScrollArea, 1);
  rightLayout->addWidget(transcriptPanel, 1);

  workspaceLayout->addWidget(leftPane, 3);
  workspaceLayout->addWidget(rightPane, 2);

  layout->addWidget(topToolbar);
  layout->addWidget(mainWorkspace, 1);
  setCentralWidget(central);

  statusBar()->showMessage(report.isReady()
      ? QStringLiteral("启动检查通过")
      : QStringLiteral("启动检查失败"));
  loadBaiduTranslationSettings();
  updateTaskSettingsVisibility();
}

MainWindow::~MainWindow() {
  ++subtitlePreparationGeneration_;
  if (subtitlePreparationCancelRequested_) {
    subtitlePreparationCancelRequested_->store(true);
  }
  if (subtitlePreparationWatcher_->isRunning()) {
    subtitlePreparationWatcher_->waitForFinished();
  }
  player_.setVideoFrameCallback(nullptr);
  player_.stop();
}

void MainWindow::applyLightToolbenchStyle() {
  setStyleSheet(QStringLiteral(
      "QWidget#centralWidget {"
      "  background-color: #f4f7fb;"
      "  color: #0f172a;"
      "}"
      "QWidget#topToolbar {"
      "  background-color: #ffffff;"
      "  border: 1px solid #dce5f2;"
      "  border-radius: 8px;"
      "}"
      "QLabel#appTitleLabel {"
      "  color: #0f172a;"
      "  font-size: 20px;"
      "  font-weight: 700;"
      "}"
      "QLabel#statusPillReady {"
      "  color: #027a48;"
      "  background-color: #ecfdf3;"
      "  border: 1px solid #abefc6;"
      "  border-radius: 7px;"
      "  padding: 6px 12px;"
      "  font-weight: 600;"
      "}"
      "QLabel#statusPillError {"
      "  color: #b42318;"
      "  background-color: #fef3f2;"
      "  border: 1px solid #fecdca;"
      "  border-radius: 7px;"
      "  padding: 6px 12px;"
      "  font-weight: 600;"
      "}"
      "QWidget#videoContainer, QWidget#playbackControls {"
      "  background-color: #ffffff;"
      "  border: 1px solid #dce5f2;"
      "  border-radius: 8px;"
      "}"
      "QWidget#videoContainer QLabel {"
      "  border-radius: 6px;"
      "}"
      "QGroupBox {"
      "  background-color: #ffffff;"
      "  border: 1px solid #dce5f2;"
      "  border-radius: 8px;"
      "  margin-top: 18px;"
      "  padding-top: 8px;"
      "  font-weight: 700;"
      "  color: #0f172a;"
      "}"
      "QGroupBox::title {"
      "  subcontrol-origin: margin;"
      "  left: 12px;"
      "  padding: 0 4px;"
      "}"
      "QPushButton {"
      "  min-height: 34px;"
      "  border-radius: 7px;"
      "  padding: 6px 12px;"
      "  border: 1px solid #cbd7e8;"
      "  background-color: #ffffff;"
      "  color: #0f172a;"
      "  font-weight: 600;"
      "}"
      "QPushButton:hover {"
      "  border-color: #2457d6;"
      "  background-color: #f5f8ff;"
      "}"
      "QPushButton:disabled {"
      "  color: #98a2b3;"
      "  background-color: #f2f4f7;"
      "  border-color: #e4e7ec;"
      "}"
      "QPushButton#openMediaButton, QPushButton#startTaskButton {"
      "  color: #ffffff;"
      "  background-color: #2457d6;"
      "  border-color: #2457d6;"
      "}"
      "QPushButton#openMediaButton:hover, QPushButton#startTaskButton:hover {"
      "  background-color: #1740a8;"
      "}"
      "QPushButton#cancelTaskButton {"
      "  color: #b42318;"
      "  background-color: #fff7f6;"
      "  border-color: #fecdca;"
      "}"
      "QPushButton#pauseButton {"
      "  color: #1740a8;"
      "  background-color: #eef4ff;"
      "  border-color: #bfd0ff;"
      "}"
      "QPushButton[taskMode=\"true\"] {"
      "  min-height: 74px;"
      "  text-align: left;"
      "  padding: 10px;"
      "  background-color: #f8fbff;"
      "  border: 1px solid #ccd8ea;"
      "  color: #334155;"
      "}"
      "QPushButton[taskMode=\"true\"][selected=\"true\"] {"
      "  color: #1740a8;"
      "  background-color: #eaf2ff;"
      "  border: 1px solid #2457d6;"
      "}"
      "QComboBox, QLineEdit, QSpinBox {"
      "  min-height: 34px;"
      "  border: 1px solid #cbd7e8;"
      "  border-radius: 7px;"
      "  padding: 4px 8px;"
      "  background-color: #ffffff;"
      "  color: #0f172a;"
      "}"
      "QComboBox:focus, QLineEdit:focus, QSpinBox:focus {"
      "  border-color: #2457d6;"
      "}"
      "QCheckBox {"
      "  color: #0f172a;"
      "  font-weight: 600;"
      "  spacing: 8px;"
      "}"
      "QScrollArea#transcriptScrollArea {"
      "  background-color: #fbfdff;"
      "  border: 1px solid #dde6f3;"
      "  border-radius: 8px;"
      "}"
      "QLabel#transcriptListLabel {"
      "  background-color: #fbfdff;"
      "  color: #475467;"
      "}"
      "QLabel#mediaInfoLabel {"
      "  color: #475467;"
      "  background-color: #f8fbff;"
      "  border: 1px solid #e3e9f2;"
      "  border-radius: 7px;"
      "  padding: 8px;"
      "}"
      "QLabel#timeLabel {"
      "  color: #64748b;"
      "  font-weight: 600;"
      "}"
      "QSlider::groove:horizontal {"
      "  height: 7px;"
      "  border-radius: 3px;"
      "  background-color: #d8e2f0;"
      "}"
      "QSlider::sub-page:horizontal {"
      "  border-radius: 3px;"
      "  background-color: #2457d6;"
      "}"
      "QSlider::handle:horizontal {"
      "  width: 14px;"
      "  height: 14px;"
      "  margin: -5px 0;"
      "  border-radius: 7px;"
      "  background-color: #ffffff;"
      "  border: 2px solid #2457d6;"
      "}"
      "QProgressBar#statusProgressBar {"
      "  height: 8px;"
      "  border-radius: 4px;"
      "  border: 1px solid #cbd7e8;"
      "  background-color: #e7edf6;"
      "  text-align: center;"
      "}"
      "QProgressBar#statusProgressBar::chunk {"
      "  border-radius: 4px;"
      "  background-color: #2457d6;"
      "}"
      "QStatusBar {"
      "  color: #334155;"
      "  background-color: #ffffff;"
      "  border-top: 1px solid #dce5f2;"
      "  font-weight: 600;"
      "}"
  ));
}

QLabel* MainWindow::createOptionDescription(const QString& objectName, const QString& text, QWidget* parent) {
  auto* description = new QLabel(text, parent);
  description->setObjectName(objectName);
  description->setWordWrap(true);
  QFont descriptionFont = description->font();
  descriptionFont.setPointSize(std::max(8, descriptionFont.pointSize() - 1));
  description->setFont(descriptionFont);
  description->setStyleSheet(QStringLiteral("color: #64748b;"));
  return description;
}

QPushButton* MainWindow::createTaskModeButton(
    const QString& objectName,
    const QString& title,
    const QString& description,
    QWidget* parent) {
  auto* button = new QPushButton(QStringLiteral("%1\n%2").arg(title, description), parent);
  button->setObjectName(objectName);
  button->setProperty("taskMode", true);
  button->setProperty("selected", false);
  button->setCheckable(true);
  button->setCursor(Qt::PointingHandCursor);
  button->setMinimumHeight(78);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  return button;
}

void MainWindow::setupTaskOptions(QVBoxLayout* layout, QWidget* parent) {
  auto* group = new QGroupBox(QStringLiteral("任务设置"), parent);
  group->setObjectName(QStringLiteral("taskOptionsPanel"));
  auto* groupLayout = new QVBoxLayout(group);
  groupLayout->setContentsMargins(12, 14, 12, 12);
  groupLayout->setSpacing(10);

  auto* fileSectionLabel = new QLabel(QStringLiteral("打开文件"), group);
  fileSectionLabel->setObjectName(QStringLiteral("fileSectionLabel"));
  QFont sectionFont = fileSectionLabel->font();
  sectionFont.setBold(true);
  fileSectionLabel->setFont(sectionFont);
  groupLayout->addWidget(fileSectionLabel);
  groupLayout->addWidget(openButton_);
  groupLayout->addWidget(mediaInfoLabel_);

  auto* taskSectionLabel = new QLabel(QStringLiteral("任务选择"), group);
  taskSectionLabel->setObjectName(QStringLiteral("taskSectionLabel"));
  taskSectionLabel->setFont(sectionFont);
  groupLayout->addWidget(taskSectionLabel);
  taskTypeComboBox_->setObjectName(QStringLiteral("taskTypeComboBox"));
  taskTypeComboBox_->addItem(QStringLiteral("直接播放"), QStringLiteral("direct_play"));
  taskTypeComboBox_->addItem(QStringLiteral("预处理字幕"), QStringLiteral("preprocess_subtitle"));
  taskTypeComboBox_->addItem(QStringLiteral("实时字幕"), QStringLiteral("live_subtitle"));
  taskTypeComboBox_->hide();
  groupLayout->addWidget(taskTypeComboBox_);

  directPlayTaskButton_ = createTaskModeButton(
      QStringLiteral("directPlayTaskButton"),
      QStringLiteral("直接播放"),
      QStringLiteral("只播放媒体文件"),
      group);
  preprocessSubtitleTaskButton_ = createTaskModeButton(
      QStringLiteral("preprocessSubtitleTaskButton"),
      QStringLiteral("预处理字幕"),
      QStringLiteral("先生成字幕再播放"),
      group);
  liveSubtitleTaskButton_ = createTaskModeButton(
      QStringLiteral("liveSubtitleTaskButton"),
      QStringLiteral("实时字幕"),
      QStringLiteral("边播放边生成"),
      group);

  auto* taskModeLayout = new QHBoxLayout();
  taskModeLayout->setContentsMargins(0, 0, 0, 0);
  taskModeLayout->setSpacing(8);
  taskModeLayout->addWidget(directPlayTaskButton_);
  taskModeLayout->addWidget(preprocessSubtitleTaskButton_);
  taskModeLayout->addWidget(liveSubtitleTaskButton_);
  groupLayout->addLayout(taskModeLayout);

  connect(directPlayTaskButton_, &QPushButton::clicked, this, [this]() {
    selectTaskType(QStringLiteral("direct_play"));
  });
  connect(preprocessSubtitleTaskButton_, &QPushButton::clicked, this, [this]() {
    selectTaskType(QStringLiteral("preprocess_subtitle"));
  });
  connect(liveSubtitleTaskButton_, &QPushButton::clicked, this, [this]() {
    selectTaskType(QStringLiteral("live_subtitle"));
  });

  groupLayout->addWidget(createOptionDescription(
      QStringLiteral("taskTypeDescription"),
      QStringLiteral("预处理字幕会先生成字幕再播放；实时字幕会边播放边生成字幕。"),
      group));
  groupLayout->addWidget(useTranslationCheckBox_);
  liveInterpretationDescription_->setObjectName(QStringLiteral("liveInterpretationDescription"));
  liveInterpretationDescription_->setWordWrap(true);
  liveInterpretationDescription_->setText(QStringLiteral(
      "实时字幕会边播放边进行转录，响应更快，但准确度和时间对齐可能不如预处理字幕。"));
  QFont liveDescriptionFont = liveInterpretationDescription_->font();
  liveDescriptionFont.setPointSize(std::max(8, liveDescriptionFont.pointSize() - 1));
  liveInterpretationDescription_->setFont(liveDescriptionFont);
  liveInterpretationDescription_->setStyleSheet(QStringLiteral("color: #8a5a00;"));
  groupLayout->addWidget(liveInterpretationDescription_);
  groupLayout->addWidget(startTaskButton_);
  groupLayout->addWidget(cancelTaskButton_);

  layout->addWidget(group);
}

void MainWindow::selectTaskType(const QString& taskType) {
  if (!taskTypeComboBox_) {
    return;
  }

  const int index = taskTypeComboBox_->findData(taskType);
  if (index >= 0) {
    taskTypeComboBox_->setCurrentIndex(index);
  }
}

void MainWindow::updateTaskModeButtons() {
  if (!taskTypeComboBox_) {
    return;
  }

  const QString taskType = taskTypeComboBox_->currentData().toString();
  const auto updateButton = [](QPushButton* button, bool selected) {
    if (!button) {
      return;
    }

    button->setChecked(selected);
    button->setProperty("selected", selected);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
  };

  updateButton(directPlayTaskButton_, taskType == QStringLiteral("direct_play"));
  updateButton(preprocessSubtitleTaskButton_, taskType == QStringLiteral("preprocess_subtitle"));
  updateButton(liveSubtitleTaskButton_, taskType == QStringLiteral("live_subtitle"));
}

void MainWindow::setupTranscriptionOptions(QVBoxLayout* layout, QWidget* parent) {
  transcriptionOptionsPanel_ = new QGroupBox(QStringLiteral("源字幕设置"), parent);
  transcriptionOptionsPanel_->setObjectName(QStringLiteral("transcriptionOptionsPanel"));
  auto* groupLayout = new QFormLayout(transcriptionOptionsPanel_);
  groupLayout->setContentsMargins(12, 14, 12, 12);
  groupLayout->setSpacing(10);
  groupLayout->setLabelAlignment(Qt::AlignRight);
  groupLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  transcriptionModelComboBox_->setObjectName(QStringLiteral("transcriptionModelComboBox"));
  populateTranscriptionModels();
  importWhisperModelButton_->setObjectName(QStringLiteral("importWhisperModelButton"));
  auto* modelContainer = new QWidget(transcriptionOptionsPanel_);
  auto* modelLayout = new QVBoxLayout(modelContainer);
  modelLayout->setContentsMargins(0, 0, 0, 0);
  auto* modelControlLayout = new QHBoxLayout();
  modelControlLayout->setContentsMargins(0, 0, 0, 0);
  modelControlLayout->addWidget(transcriptionModelComboBox_, 1);
  modelControlLayout->addWidget(importWhisperModelButton_);
  modelLayout->addLayout(modelControlLayout);
  modelLayout->addWidget(createOptionDescription(
      QStringLiteral("transcriptionModelDescription"),
      QStringLiteral("选择或导入 whisper.cpp 的 ggml .bin 模型，Release 运行时会加载当前选中的模型。"),
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
  auto* languageContainer = new QWidget(transcriptionOptionsPanel_);
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
  auto* threadContainer = new QWidget(transcriptionOptionsPanel_);
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
  auto* promptContainer = new QWidget(transcriptionOptionsPanel_);
  auto* promptLayout = new QVBoxLayout(promptContainer);
  promptLayout->setContentsMargins(0, 0, 0, 0);
  promptLayout->addWidget(transcriptionPromptEdit_);
  promptLayout->addWidget(createOptionDescription(
      QStringLiteral("transcriptionPromptDescription"),
      QStringLiteral("作为上下文提示词传给 whisper.cpp，提高专有名词识别稳定性。"),
      promptContainer));
  groupLayout->addRow(QStringLiteral("Prompt"), promptContainer);

  layout->addWidget(transcriptionOptionsPanel_);
}

void MainWindow::setupTranslationOptions(QVBoxLayout* layout, QWidget* parent) {
  translationSettingsPanel_ = new QGroupBox(QStringLiteral("翻译设置"), parent);
  translationSettingsPanel_->setObjectName(QStringLiteral("translationSettingsPanel"));
  auto* groupLayout = new QFormLayout(translationSettingsPanel_);
  groupLayout->setContentsMargins(12, 14, 12, 12);
  groupLayout->setSpacing(10);
  groupLayout->setLabelAlignment(Qt::AlignRight);
  groupLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  baiduAppIdEdit_->setObjectName(QStringLiteral("baiduAppIdEdit"));
  baiduAppIdEdit_->setPlaceholderText(QStringLiteral("百度翻译 APP ID"));
  groupLayout->addRow(QStringLiteral("APP ID"), baiduAppIdEdit_);

  baiduSecretKeyEdit_->setObjectName(QStringLiteral("baiduSecretKeyEdit"));
  baiduSecretKeyEdit_->setPlaceholderText(QStringLiteral("百度翻译密钥"));
  baiduSecretKeyEdit_->setEchoMode(QLineEdit::Password);
  groupLayout->addRow(QStringLiteral("密钥"), baiduSecretKeyEdit_);

  saveBaiduSettingsButton_->setObjectName(QStringLiteral("saveBaiduSettingsButton"));
  groupLayout->addRow(QString(), saveBaiduSettingsButton_);

  groupLayout->addRow(QString(), createOptionDescription(
      QStringLiteral("baiduTranslationDescription"),
      QStringLiteral("使用用户自己的百度翻译开放平台 API 信息，保存后用于生成翻译字幕。"),
      translationSettingsPanel_));

  layout->addWidget(translationSettingsPanel_);
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

void MainWindow::updateTaskSettingsVisibility() {
  const QString taskType = taskTypeComboBox_
      ? taskTypeComboBox_->currentData().toString()
      : QStringLiteral("direct_play");
  updateTaskModeButtons();
  const bool subtitleTask = taskType == QStringLiteral("preprocess_subtitle")
      || taskType == QStringLiteral("live_subtitle");
  const bool liveTask = taskType == QStringLiteral("live_subtitle");
  const bool translateTask = subtitleTask && useTranslationForSubtitleTask();
  if (transcriptionOptionsPanel_) {
    transcriptionOptionsPanel_->setHidden(!subtitleTask);
  }
  if (useTranslationCheckBox_) {
    useTranslationCheckBox_->setHidden(!subtitleTask);
    useTranslationCheckBox_->setEnabled(subtitleTask);
  }
  if (translationSettingsPanel_) {
    translationSettingsPanel_->setHidden(!translateTask);
    translationSettingsPanel_->setEnabled(translateTask);
  }
  if (liveInterpretationDescription_) {
    liveInterpretationDescription_->setHidden(!liveTask);
  }
  setTaskButtonsEnabled(canStartSelectedTask());
}

void MainWindow::importWhisperModel() {
  const QString sourcePath = QFileDialog::getOpenFileName(
      this,
      QStringLiteral("导入 whisper.cpp 模型"),
      QString(),
      QStringLiteral("whisper.cpp 模型 (*.bin);;所有文件 (*.*)"));

  if (sourcePath.isEmpty()) {
    return;
  }

  importWhisperModelFromPath(sourcePath, true);
}

bool MainWindow::importWhisperModelFromPath(const QString& sourcePath, bool askBeforeOverwrite) {
  const QFileInfo sourceInfo(sourcePath);
  if (!sourceInfo.exists() || !sourceInfo.isFile()) {
    statusBar()->showMessage(QStringLiteral("模型导入失败：文件不存在"));
    return false;
  }

  if (sourceInfo.suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0) {
    statusBar()->showMessage(QStringLiteral("模型导入失败：请选择 .bin 模型文件"));
    return false;
  }

  QDir whisperModelDir(modelsRootPath() + QStringLiteral("/whisper"));
  if (!whisperModelDir.exists() && !QDir().mkpath(whisperModelDir.absolutePath())) {
    statusBar()->showMessage(QStringLiteral("模型导入失败：无法创建模型目录"));
    return false;
  }

  const QString destinationPath = whisperModelDir.absoluteFilePath(sourceInfo.fileName());
  const QFileInfo destinationInfo(destinationPath);
  if (destinationInfo.exists()
      && destinationInfo.absoluteFilePath() != sourceInfo.absoluteFilePath()) {
    if (askBeforeOverwrite) {
      const QMessageBox::StandardButton choice = QMessageBox::question(
          this,
          QStringLiteral("覆盖模型"),
          QStringLiteral("models/whisper 中已存在同名模型，是否覆盖？"));
      if (choice != QMessageBox::Yes) {
        statusBar()->showMessage(QStringLiteral("已取消导入模型"));
        return false;
      }
    }
    if (!QFile::remove(destinationPath)) {
      statusBar()->showMessage(QStringLiteral("模型导入失败：无法覆盖同名文件"));
      return false;
    }
  }

  bool copied = true;
  if (destinationInfo.absoluteFilePath() != sourceInfo.absoluteFilePath()) {
    copied = QFile::copy(sourceInfo.absoluteFilePath(), destinationPath);
  }
  if (!copied) {
    statusBar()->showMessage(QStringLiteral("模型导入失败：复制文件失败"));
    return false;
  }

  populateTranscriptionModels();
  const int modelIndex = transcriptionModelComboBox_->findData(QFileInfo(destinationPath).absoluteFilePath());
  if (modelIndex >= 0) {
    transcriptionModelComboBox_->setCurrentIndex(modelIndex);
  }
  statusBar()->showMessage(QStringLiteral("模型导入成功：%1").arg(sourceInfo.fileName()));
  return true;
}

void MainWindow::openMediaFile() {
  if (subtitlePreparationWatcher_->isRunning()) {
    statusBar()->showMessage(QStringLiteral("正在准备字幕，可先取消当前任务"));
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
  latestLiveSubtitleText_.clear();
  updateSubtitle(0);
  updateTranscriptPanel(0);
  setTaskButtonsEnabled(false);
  cancelTaskButton_->setEnabled(false);
  openButton_->setEnabled(false);
  mediaInfoLabel_->setText(QStringLiteral("读取中..."));
  showStatusProgress(-1);
  statusBar()->showMessage(QStringLiteral("正在打开媒体文件"));

  mediaProbeWatcher_->setFuture(QtConcurrent::run([filePath]() {
    return FFmpegMediaProbe::probe(filePath);
  }));
}

void MainWindow::onMediaProbeFinished() {
  hideStatusProgress();
  showMediaInfo(mediaProbeWatcher_->result());
}

void MainWindow::loadBaiduTranslationSettings() {
  QSettings settings;
  baiduAppIdEdit_->setText(settings.value(QStringLiteral("baiduTranslator/appId")).toString());
  baiduSecretKeyEdit_->setText(settings.value(QStringLiteral("baiduTranslator/secretKey")).toString());
}

void MainWindow::saveBaiduTranslationSettings() {
  QSettings settings;
  settings.setValue(QStringLiteral("baiduTranslator/appId"), baiduAppIdEdit_->text().trimmed());
  settings.setValue(QStringLiteral("baiduTranslator/secretKey"), baiduSecretKeyEdit_->text().trimmed());
  settings.sync();
  statusBar()->showMessage(QStringLiteral("百度翻译 API 信息已保存"));
}

void MainWindow::onSubtitlePreparationFinished() {
  openButton_->setEnabled(true);
  cancelTaskButton_->setEnabled(false);
  hideStatusProgress();
  const bool updateDuringPlayback = subtitlePreparationUpdatesPlayback_;
  subtitlePreparationUpdatesPlayback_ = false;

  const MediaSubtitlePreparationResult result = subtitlePreparationWatcher_->result();
  if (result.canceled) {
    setTaskButtonsEnabled(!pendingPlaybackInfo_.filePath.trimmed().isEmpty());
    statusBar()->showMessage(QStringLiteral("任务已取消"));
    transcriptListLabel_->setText(QStringLiteral("任务已取消"));
    return;
  }

  if (!result.success) {
    setTaskButtonsEnabled(!pendingPlaybackInfo_.filePath.trimmed().isEmpty());
    statusBar()->showMessage(QStringLiteral("字幕准备失败：%1").arg(result.errorMessage));
    transcriptListLabel_->setText(QStringLiteral("字幕准备失败"));
    return;
  }

  subtitleTrack_ = result.subtitleTrack;
  const qint64 currentPositionMs = updateDuringPlayback
      ? player_.audioClockMs()
      : 0;
  updateSubtitle(currentPositionMs);
  updateTranscriptPanel(currentPositionMs);
  if (updateDuringPlayback) {
    statusBar()->showMessage(QStringLiteral("实时字幕已生成，已应用到当前播放"));
    setTaskButtonsEnabled(false);
    return;
  }

  statusBar()->showMessage(QStringLiteral("字幕准备完成，开始播放"));
  setTaskButtonsEnabled(false);
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

  pendingPlaybackInfo_ = info;
  openButton_->setEnabled(true);
  setTaskButtonsEnabled(true);
  statusBar()->showMessage(QStringLiteral("请选择任务：直接播放、预处理字幕或实时字幕"));
}

void MainWindow::startSubtitlePreparation(
    const MediaInfo& info,
    bool translateAfterTranscription,
    bool updateDuringPlayback) {
  pendingPlaybackInfo_ = info;
  const int generation = ++subtitlePreparationGeneration_;
  const TranscriptionOptions options = transcriptionOptions();
  const BaiduTranslationSettings translationSettings = baiduTranslationSettings();
  subtitlePreparationUpdatesPlayback_ = updateDuringPlayback;
  if (options.modelPath.trimmed().isEmpty()) {
    openButton_->setEnabled(true);
    subtitlePreparationUpdatesPlayback_ = false;
    statusBar()->showMessage(QStringLiteral("未选择转录模型，无法准备字幕"));
    transcriptListLabel_->setText(QStringLiteral("未选择转录模型"));
    return;
  }
  if (translateAfterTranscription) {
    const BaiduTranslationSettings settings = baiduTranslationSettings();
    if (settings.appId.trimmed().isEmpty() || settings.secretKey.trimmed().isEmpty()) {
      openButton_->setEnabled(true);
      subtitlePreparationUpdatesPlayback_ = false;
      statusBar()->showMessage(QStringLiteral("启用翻译后需要填写百度翻译 APP ID 和密钥"));
      transcriptListLabel_->setText(QStringLiteral("翻译设置不完整"));
      return;
    }
  }

  const QString taskMessage = updateDuringPlayback
      ? (translateAfterTranscription
      ? QStringLiteral("正在生成实时翻译字幕")
      : QStringLiteral("正在生成实时转录字幕"))
      : (translateAfterTranscription
      ? QStringLiteral("正在准备翻译字幕")
      : QStringLiteral("正在准备转录字幕"));
  transcriptListLabel_->setText(taskMessage);
  statusBar()->showMessage(taskMessage);
  showStatusProgress(-1);
  setTaskButtonsEnabled(false);
  cancelTaskButton_->setEnabled(true);
  subtitlePreparationCancelRequested_ = std::make_shared<std::atomic_bool>(false);
  const std::shared_ptr<std::atomic_bool> cancelRequested = subtitlePreparationCancelRequested_;
  subtitlePreparationWatcher_->setFuture(QtConcurrent::run(
      [this, info, options, translationSettings, generation, cancelRequested, translateAfterTranscription]() {
    MediaSubtitlePreparer preparer;
    MediaSubtitlePreparationRequest request;
    request.mediaPath = info.filePath;
    request.options = options;
    request.cancelRequested = cancelRequested;
    request.progressCallback = [this, generation](const MediaSubtitlePreparationProgress& progress) {
      QMetaObject::invokeMethod(this, [this, generation, progress]() {
        updateSubtitlePreparationProgress(generation, progress);
      }, Qt::QueuedConnection);
    };
    MediaSubtitlePreparationResult result = preparer.prepare(request);
    if (!translateAfterTranscription || !result.success || result.canceled) {
      return result;
    }

    if (cancelRequested && cancelRequested->load()) {
      result.success = false;
      result.canceled = true;
      result.errorMessage = QStringLiteral("翻译已取消");
      return result;
    }

    BaiduTranslator translator;
    BaiduTranslationRequest translationRequest;
    translationRequest.sourceTrack = result.subtitleTrack;
    translationRequest.settings = translationSettings;
    translationRequest.sourceLanguage = baiduSourceLanguageForWhisperLanguage(options.languageCode);
    translationRequest.targetLanguage = QStringLiteral("zh");
    translationRequest.cancelRequested = cancelRequested;
    translationRequest.progressCallback = [this, generation](const BaiduTranslationProgress& progress) {
      QMetaObject::invokeMethod(this, [this, generation, progress]() {
        updateSubtitlePreparationProgress(generation, MediaSubtitlePreparationProgress{
            MediaSubtitlePreparationStage::Translating,
            progress.percent,
            progress.message});
      }, Qt::QueuedConnection);
    };

    const BaiduTranslationResult translationResult = translator.translate(translationRequest);
    if (translationResult.canceled) {
      result.success = false;
      result.canceled = true;
      result.errorMessage = translationResult.errorMessage;
      return result;
    }
    if (!translationResult.success) {
      result.success = false;
      result.errorMessage = translationResult.errorMessage;
      return result;
    }

    result.subtitleTrack = translationResult.subtitleTrack;
    return result;
  }));
}

void MainWindow::startPendingPlayback() {
  setTaskButtonsEnabled(false);
  if (startPlayback(pendingPlaybackInfo_)) {
    cancelTaskButton_->setEnabled(true);
  }
}

void MainWindow::startPendingTranscription() {
  if (!pendingPlaybackInfo_.hasAudio) {
    statusBar()->showMessage(QStringLiteral("当前媒体没有音频，直接播放"));
    startPendingPlayback();
    return;
  }

  startSubtitlePreparation(pendingPlaybackInfo_, useTranslationForSubtitleTask());
}

void MainWindow::startPendingLiveSubtitle() {
  if (!pendingPlaybackInfo_.hasAudio) {
    statusBar()->showMessage(QStringLiteral("当前媒体没有音频，无法生成实时字幕"));
    return;
  }

  if (transcriptionOptions().modelPath.trimmed().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("未选择转录模型，无法生成实时字幕"));
    transcriptListLabel_->setText(QStringLiteral("未选择转录模型"));
    return;
  }

  const bool translateSegments = useTranslationForSubtitleTask();
  if (translateSegments) {
    const BaiduTranslationSettings settings = baiduTranslationSettings();
    if (settings.appId.trimmed().isEmpty() || settings.secretKey.trimmed().isEmpty()) {
      statusBar()->showMessage(QStringLiteral("启用翻译后需要填写百度翻译 APP ID 和密钥"));
      transcriptListLabel_->setText(QStringLiteral("翻译设置不完整"));
      return;
    }
  }
  subtitleTrack_.setSegments({});
  latestLiveSubtitleText_.clear();
  updateSubtitle(0);
  updateTranscriptPanel(0);
  statusBar()->showMessage(translateSegments
      ? QStringLiteral("实时字幕已开始，字幕会显示译文并持续修正")
      : QStringLiteral("实时字幕已开始，字幕会显示转录原文并持续修正"));
  if (!startPlayback(pendingPlaybackInfo_)) {
    return;
  }

  const int generation = ++subtitlePreparationGeneration_;
  subtitlePreparationUpdatesPlayback_ = true;
  setTaskButtonsEnabled(false);
  cancelTaskButton_->setEnabled(true);
  showStatusProgress(-1);
  subtitlePreparationCancelRequested_ = std::make_shared<std::atomic_bool>(false);
  const std::shared_ptr<std::atomic_bool> cancelRequested = subtitlePreparationCancelRequested_;
  const TranscriptionOptions options = transcriptionOptions();
  const BaiduTranslationSettings translationSettings = baiduTranslationSettings();
  const QString sourceLanguage = baiduSourceLanguageForWhisperLanguage(options.languageCode);

  subtitlePreparationWatcher_->setFuture(QtConcurrent::run(
      [this, generation, options, translationSettings, sourceLanguage, translateSegments, cancelRequested]() {
    LiveSubtitleInterpreter interpreter;
    LiveSubtitleInterpreterRequest request;
    request.options = options;
    request.translationSettings = translationSettings;
    request.sourceLanguage = sourceLanguage;
    request.targetLanguage = QStringLiteral("zh");
    request.streamStepMs = 500;
    request.streamLengthMs = 4000;
    request.streamKeepMs = 200;
    request.translationIntervalMs = 100;
    request.translateSegments = translateSegments;
    request.cancelRequested = cancelRequested;
    request.takeAudioFrame = [this](TranscriptionAudioFrame* frame) {
      return player_.takeTranscriptionAudioFrame(frame);
    };
    request.isPlaybackActive = [this]() {
      return player_.activeWorkerCount() > 0
          || player_.state() == PlaybackState::Playing
          || player_.state() == PlaybackState::Paused;
    };
    request.progressCallback = [this, generation](const LiveSubtitleInterpreterProgress& progress) {
      QMetaObject::invokeMethod(this, [this, generation, progress]() {
        updateSubtitlePreparationProgress(generation, MediaSubtitlePreparationProgress{
            MediaSubtitlePreparationStage::Translating,
            0,
            progress.message});
      }, Qt::QueuedConnection);
    };
    request.segmentCallback = [this, generation](const SubtitleSegment& segment) {
      QMetaObject::invokeMethod(this, [this, generation, segment]() {
        appendLiveSubtitleSegment(generation, segment);
      }, Qt::QueuedConnection);
    };

    const LiveSubtitleInterpreterResult liveResult = interpreter.run(request);
    MediaSubtitlePreparationResult result;
    result.success = liveResult.success;
    result.canceled = liveResult.canceled;
    result.errorMessage = liveResult.errorMessage;
    result.subtitleTrack = liveResult.subtitleTrack;
    return result;
  }));
}

void MainWindow::startSelectedTask() {
  if (!canStartSelectedTask()) {
    statusBar()->showMessage(useTranslationForSubtitleTask()
        ? QStringLiteral("启用翻译后需要填写百度翻译 APP ID 和密钥")
        : QStringLiteral("请先打开媒体文件"));
    return;
  }

  const QString taskType = taskTypeComboBox_
      ? taskTypeComboBox_->currentData().toString()
      : QStringLiteral("direct_play");
  if (taskType == QStringLiteral("direct_play")) {
    startPendingPlayback();
    return;
  }
  if (taskType == QStringLiteral("live_subtitle")) {
    startPendingLiveSubtitle();
    return;
  }

  startPendingTranscription();
}

void MainWindow::appendLiveSubtitleSegment(int generation, const SubtitleSegment& segment) {
  if (generation != subtitlePreparationGeneration_) {
    return;
  }

  subtitleTrack_.upsertSegment(segment);
  latestLiveSubtitleText_ = segment.translatedText.isEmpty()
      ? segment.sourceText
      : segment.translatedText;
  const qint64 positionMs = player_.audioClockMs();
  updateSubtitle(positionMs);
  updateTranscriptPanel(positionMs);
}

void MainWindow::cancelSubtitlePreparation() {
  const bool hasSubtitleTask = subtitlePreparationWatcher_->isRunning();

  if (hasSubtitleTask && subtitlePreparationCancelRequested_) {
    subtitlePreparationCancelRequested_->store(true);
  }

  stopPlayback();
  cancelTaskButton_->setEnabled(false);
  hideStatusProgress();
  if (!hasSubtitleTask) {
    statusBar()->showMessage(QStringLiteral("播放已取消"));
    transcriptListLabel_->setText(QStringLiteral("播放已取消"));
    return;
  }

  statusBar()->showMessage(QStringLiteral("正在取消任务..."));
  transcriptListLabel_->setText(QStringLiteral("正在取消任务..."));
}

void MainWindow::setTaskButtonsEnabled(bool enabled) {
  if (startTaskButton_) {
    startTaskButton_->setEnabled(enabled && canStartSelectedTask());
  }
}

bool MainWindow::canStartSelectedTask() const {
  if (pendingPlaybackInfo_.filePath.trimmed().isEmpty()) {
    return false;
  }

  const QString taskType = taskTypeComboBox_
      ? taskTypeComboBox_->currentData().toString()
      : QStringLiteral("direct_play");
  const bool subtitleTask = taskType == QStringLiteral("preprocess_subtitle")
      || taskType == QStringLiteral("live_subtitle");
  if (!subtitleTask || !useTranslationForSubtitleTask()) {
    return true;
  }

  const BaiduTranslationSettings settings = baiduTranslationSettings();
  return !settings.appId.trimmed().isEmpty()
      && !settings.secretKey.trimmed().isEmpty();
}

bool MainWindow::useTranslationForSubtitleTask() const {
  return useTranslationCheckBox_ && useTranslationCheckBox_->isChecked();
}

void MainWindow::showStatusProgress(int percent) {
  if (!statusProgressBar_) {
    return;
  }

  if (percent < 0) {
    statusProgressBar_->setRange(0, 0);
  } else {
    statusProgressBar_->setRange(0, 100);
    statusProgressBar_->setValue(std::max(0, std::min(100, percent)));
  }
  statusProgressBar_->show();
}

void MainWindow::hideStatusProgress() {
  if (!statusProgressBar_) {
    return;
  }

  statusProgressBar_->setRange(0, 100);
  statusProgressBar_->setValue(0);
  statusProgressBar_->hide();
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
  showStatusProgress(progress.percent > 0 ? progress.percent : -1);
  statusBar()->showMessage(message);
  if (subtitlePreparationUpdatesPlayback_ && !subtitleTrack_.isEmpty()) {
    return;
  }
  transcriptListLabel_->setText(message);
}

bool MainWindow::startPlayback(const MediaInfo& info) {
  if (!info.filePath.trimmed().isEmpty()) {
    pendingPlaybackInfo_ = info;
  }
  player_.stop();
  playbackStatusTimer_->stop();
  pauseButton_->setEnabled(false);
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

  cancelTaskButton_->setEnabled(true);
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
  hideStatusProgress();
  pendingSeekPositionMs_ = -1;
  currentHasAudio_ = false;
  currentHasVideo_ = false;
  latestLiveSubtitleText_.clear();
  pauseButton_->setEnabled(false);
  pauseButton_->setText(QStringLiteral("暂停"));
  cancelTaskButton_->setEnabled(false);
  seekSlider_->setValue(0);
  updateTimeLabel(0);
  updateSubtitle(0);
  updateTranscriptPanel(0);
  videoLabel_->setText(QStringLiteral("打开媒体文件后显示画面"));
  if (pendingPlaybackInfo_.filePath.trimmed().isEmpty()) {
    mediaInfoLabel_->setText(QStringLiteral("未打开文件"));
    setTaskButtonsEnabled(false);
    statusBar()->showMessage(QStringLiteral("播放已停止"));
    return;
  }

  mediaInfoLabel_->setText(QStringLiteral("%1  %2")
      .arg(QFileInfo(pendingPlaybackInfo_.filePath).fileName())
      .arg(formatDuration(pendingPlaybackInfo_.durationMs)));
  setTaskButtonsEnabled(true);
  statusBar()->showMessage(QStringLiteral("播放已停止，可继续使用上一次媒体文件"));
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
  latestLiveSubtitleText_.clear();
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

BaiduTranslationSettings MainWindow::baiduTranslationSettings() const {
  BaiduTranslationSettings settings;
  settings.appId = baiduAppIdEdit_ ? baiduAppIdEdit_->text().trimmed() : QString();
  settings.secretKey = baiduSecretKeyEdit_ ? baiduSecretKeyEdit_->text().trimmed() : QString();
  return settings;
}

#ifdef ECHOTRANS_TESTING
void MainWindow::setPendingPlaybackInfoForTest(const MediaInfo& info) {
  pendingPlaybackInfo_ = info;
}

bool MainWindow::importWhisperModelForTest(const QString& sourcePath) {
  return importWhisperModelFromPath(sourcePath, false);
}

void MainWindow::displayLiveSubtitleSegmentForTest(
    const SubtitleSegment& segment,
    qint64 playbackPositionMs) {
  subtitlePreparationUpdatesPlayback_ = true;
  subtitleTrack_.upsertSegment(segment);
  latestLiveSubtitleText_ = segment.translatedText.isEmpty()
      ? segment.sourceText
      : segment.translatedText;
  updateSubtitle(playbackPositionMs);
  updateTranscriptPanel(playbackPositionMs);
}
#endif

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
      cancelTaskButton_->setEnabled(false);
      videoLabel_->setText(QStringLiteral("播放异常"));
      statusBar()->showMessage(fatalError);
    }
    return;
  }

  if (player_.state() == PlaybackState::Stopped) {
    playbackStatusTimer_->stop();
    pauseButton_->setEnabled(false);
    cancelTaskButton_->setEnabled(false);
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

  QString text = subtitleTrack_.textAt(positionMs);
  if (text.isEmpty() && subtitlePreparationUpdatesPlayback_) {
    text = latestLiveSubtitleText_;
  }
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
