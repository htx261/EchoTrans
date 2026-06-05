#include "ui/MainWindow.h"

#include "core/DependencyReport.h"

#include <QLabel>
#include <QFont>
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

  layout->addWidget(title);
  layout->addWidget(statusLabel_);
  setCentralWidget(central);

  statusBar()->showMessage(report.isReady()
      ? QStringLiteral("启动检查通过")
      : QStringLiteral("启动检查失败"));
}
