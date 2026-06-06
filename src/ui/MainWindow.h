#pragma once

#include "media/MediaInfo.h"
#include "player/MediaPlayerCore.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QLabel;
class QPushButton;
class QTimer;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

  bool startPlayback(const MediaInfo& info);
  void stopPlayback();
  PlaybackState playbackState() const;

private:
  void openMediaFile();
  void onMediaProbeFinished();
  void showMediaInfo(const MediaProbeResult& result);
  void updatePlaybackStatus();

  QPushButton* openButton_ = nullptr;
  QPushButton* stopButton_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* mediaInfoLabel_ = nullptr;
  QFutureWatcher<MediaProbeResult>* mediaProbeWatcher_ = nullptr;
  QTimer* playbackStatusTimer_ = nullptr;
  MediaPlayerCore player_;
};
