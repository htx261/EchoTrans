#include "ui/MainWindow.h"

#include "core/DependencyReport.h"

#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      statusLabel_(new QLabel(this)) {
  setWindowTitle(QStringLiteral("EchoTrans"));
  resize(1280, 720);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);

  auto* title = new QLabel(QStringLiteral("AI 同声传译助手"), central);
  title->setAlignment(Qt::AlignCenter);

  const DependencyReport report = DependencyReport::fromConfiguredPaths();
  const bool ready = report.ffmpegAvailable
      && report.whisperAvailable
      && report.ctranslate2Available
      && report.whisperModelAvailable
      && report.translationModelAvailable
      && report.tokenizerAvailable;

  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setText(ready
      ? QStringLiteral("本地依赖与模型已就绪")
      : QStringLiteral("本地依赖或模型未配置完整"));

  layout->addWidget(title);
  layout->addWidget(statusLabel_);
  setCentralWidget(central);

  statusBar()->showMessage(QStringLiteral("Ready"));
}
