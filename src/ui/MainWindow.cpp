#include "ui/MainWindow.h"

#include "core/DependencyReport.h"
#include "media/FFmpegMediaProbe.h"

#include <QFileDialog>
#include <QLabel>
#include <QFont>
#include <QHBoxLayout>
#include <QImage>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QVBoxLayout>
#include <QWidget>

namespace {
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
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      openButton_(new QPushButton(QStringLiteral("打开媒体文件"), this)),
      stopButton_(new QPushButton(QStringLiteral("停止播放"), this)),
      statusLabel_(new QLabel(this)),
      videoLabel_(new QLabel(this)),
      mediaInfoLabel_(new QLabel(this)),
      mediaProbeWatcher_(new QFutureWatcher<MediaProbeResult>(this)),
      playbackStatusTimer_(new QTimer(this)) {
  setWindowTitle(QStringLiteral("EchoTrans"));
  resize(1280, 720);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);

  auto* title = new QLabel(QStringLiteral("AI 同声传译助手"), central);
  title->setAlignment(Qt::AlignCenter);
  QFont titleFont = title->font();
  titleFont.setPointSize(22);
  titleFont.setBold(true);
  title->setFont(titleFont);

  const DependencyReport report = DependencyReport::fromConfiguredPaths();

  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  QFont statusFont = statusLabel_->font();
  statusFont.setPointSize(16);
  statusLabel_->setFont(statusFont);
  statusLabel_->setText(report.startupMessage());

  mediaInfoLabel_->setAlignment(Qt::AlignLeft);
  mediaInfoLabel_->setWordWrap(true);
  mediaInfoLabel_->setText(QStringLiteral("尚未打开媒体文件"));

  videoLabel_->setAlignment(Qt::AlignCenter);
  videoLabel_->setMinimumSize(640, 360);
  videoLabel_->setStyleSheet(QStringLiteral("background-color: #101010; color: white;"));
  videoLabel_->setText(QStringLiteral("尚未显示视频画面"));

  stopButton_->setEnabled(false);

  auto* buttonLayout = new QHBoxLayout();
  buttonLayout->addWidget(openButton_);
  buttonLayout->addWidget(stopButton_);

  connect(openButton_, &QPushButton::clicked, this, &MainWindow::openMediaFile);
  connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopPlayback);
  connect(mediaProbeWatcher_, &QFutureWatcher<MediaProbeResult>::finished,
      this, &MainWindow::onMediaProbeFinished);
  connect(playbackStatusTimer_, &QTimer::timeout, this, &MainWindow::updatePlaybackStatus);

  player_.setVideoFrameCallback([this](const QImage& image, qint64) {
    QMetaObject::invokeMethod(this, [this, image]() {
      displayVideoFrame(image);
    }, Qt::QueuedConnection);
  });

  layout->addWidget(title);
  layout->addWidget(statusLabel_);
  layout->addLayout(buttonLayout);
  layout->addWidget(videoLabel_, 1);
  layout->addWidget(mediaInfoLabel_);
  setCentralWidget(central);

  statusBar()->showMessage(report.isReady()
      ? QStringLiteral("启动检查通过")
      : QStringLiteral("启动检查失败"));
}

MainWindow::~MainWindow() {
  player_.setVideoFrameCallback(nullptr);
  player_.stop();
}

void MainWindow::openMediaFile() {
  const QString filePath = QFileDialog::getOpenFileName(
      this,
      QStringLiteral("打开媒体文件"),
      QString(),
      QStringLiteral("媒体文件 (*.mp4 *.mkv *.avi *.mov *.mp3 *.wav);;所有文件 (*.*)"));

  if (filePath.isEmpty()) {
    return;
  }

  stopPlayback();
  openButton_->setEnabled(false);
  mediaInfoLabel_->setText(QStringLiteral("正在读取媒体信息..."));
  statusBar()->showMessage(QStringLiteral("正在打开媒体文件"));

  mediaProbeWatcher_->setFuture(QtConcurrent::run([filePath]() {
    return FFmpegMediaProbe::probe(filePath);
  }));
}

void MainWindow::onMediaProbeFinished() {
  openButton_->setEnabled(true);
  showMediaInfo(mediaProbeWatcher_->result());
}

void MainWindow::showMediaInfo(const MediaProbeResult& result) {
  if (!result.success) {
    mediaInfoLabel_->setText(QStringLiteral("媒体打开失败：%1").arg(result.errorMessage));
    statusBar()->showMessage(QStringLiteral("媒体打开失败"));
    return;
  }

  const MediaInfo& info = result.info;
  mediaInfoLabel_->setText(QStringLiteral(
      "文件：%1\n"
      "时长：%2\n"
      "视频流：%3\n"
      "音频流：%4")
      .arg(info.filePath)
      .arg(formatDuration(info.durationMs))
      .arg(info.hasVideo
          ? QStringLiteral("存在，流索引 %1").arg(info.videoStreamIndex)
          : QStringLiteral("无"))
      .arg(info.hasAudio
          ? QStringLiteral("存在，流索引 %1").arg(info.audioStreamIndex)
          : QStringLiteral("无")));

  statusBar()->showMessage(QStringLiteral("媒体信息读取完成"));
  startPlayback(info);
}

bool MainWindow::startPlayback(const MediaInfo& info) {
  player_.stop();
  playbackStatusTimer_->stop();
  stopButton_->setEnabled(false);
  displayedVideoFrameCount_ = 0;

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
  stopButton_->setEnabled(false);
  videoLabel_->setText(QStringLiteral("播放已停止"));
  statusBar()->showMessage(QStringLiteral("播放已停止"));
}

PlaybackState MainWindow::playbackState() const {
  return player_.state();
}

std::size_t MainWindow::displayedVideoFrameCount() const {
  return displayedVideoFrameCount_;
}

void MainWindow::updatePlaybackStatus() {
  if (player_.state() == PlaybackState::Stopped) {
    playbackStatusTimer_->stop();
    stopButton_->setEnabled(false);
    return;
  }

  if (!player_.lastDemuxError().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("解封装失败：%1").arg(player_.lastDemuxError()));
    return;
  }

  if (!player_.lastAudioDecodeError().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("音频解码失败：%1").arg(player_.lastAudioDecodeError()));
    return;
  }

  if (!player_.lastAudioOutputError().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("音频输出失败：%1").arg(player_.lastAudioOutputError()));
    return;
  }

  if (!player_.lastVideoDecodeError().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("视频解码失败：%1").arg(player_.lastVideoDecodeError()));
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
