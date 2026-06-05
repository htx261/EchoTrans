#pragma once

#include "media/MediaInfo.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QLabel;
class QPushButton;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

private:
  void openMediaFile();
  void onMediaProbeFinished();
  void showMediaInfo(const MediaProbeResult& result);

  QPushButton* openButton_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* mediaInfoLabel_ = nullptr;
  QFutureWatcher<MediaProbeResult>* mediaProbeWatcher_ = nullptr;
};
