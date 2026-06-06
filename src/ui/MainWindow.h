#pragma once

#include "media/MediaInfo.h"
#include "player/MediaPlayerCore.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  bool startPlayback(const MediaInfo& info);
  void stopPlayback();
  PlaybackState playbackState() const;
  std::size_t displayedVideoFrameCount() const;
  bool seekInProgress() const;

private:
  void openMediaFile();
  void onMediaProbeFinished();
  void showMediaInfo(const MediaProbeResult& result);
  void updatePlaybackStatus();
  void displayVideoFrame(const QImage& image);
  void togglePauseResume();
  void seekToSliderValue();
  void updateTimeLabel(qint64 positionMs);

  QPushButton* openButton_ = nullptr;
  QPushButton* pauseButton_ = nullptr;
  QPushButton* stopButton_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* videoLabel_ = nullptr;
  QLabel* mediaInfoLabel_ = nullptr;
  QLabel* timeLabel_ = nullptr;
  QSlider* seekSlider_ = nullptr;
  QFutureWatcher<MediaProbeResult>* mediaProbeWatcher_ = nullptr;
  QTimer* playbackStatusTimer_ = nullptr;
  MediaPlayerCore player_;
  std::size_t displayedVideoFrameCount_ = 0;
  qint64 durationMs_ = 0;
};
