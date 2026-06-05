#include "ui/MainWindow.h"

#include "core/DependencyReport.h"
#include "media/FFmpegMediaProbe.h"

#include <QFileDialog>
#include <QLabel>
#include <QFont>
#include <QPushButton>
#include <QStatusBar>
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
      statusLabel_(new QLabel(this)),
      mediaInfoLabel_(new QLabel(this)),
      mediaProbeWatcher_(new QFutureWatcher<MediaProbeResult>(this)) {
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

  connect(openButton_, &QPushButton::clicked, this, &MainWindow::openMediaFile);
  connect(mediaProbeWatcher_, &QFutureWatcher<MediaProbeResult>::finished,
      this, &MainWindow::onMediaProbeFinished);

  layout->addWidget(title);
  layout->addWidget(statusLabel_);
  layout->addWidget(openButton_);
  layout->addWidget(mediaInfoLabel_);
  setCentralWidget(central);

  statusBar()->showMessage(report.isReady()
      ? QStringLiteral("启动检查通过")
      : QStringLiteral("启动检查失败"));
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
}
