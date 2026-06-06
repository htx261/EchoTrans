#pragma once

#include "media/MediaInfo.h"
#include "player/MediaPlayerCore.h"
#include "subtitle/SubtitleTrack.h"
#include "transcription/MediaSubtitlePreparer.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QVBoxLayout;

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
  void setSubtitleTrack(const SubtitleTrack& track);
  TranscriptionOptions transcriptionOptions() const;

private:
  void openMediaFile();
  void onMediaProbeFinished();
  void onSubtitlePreparationFinished();
  void showMediaInfo(const MediaProbeResult& result);
  void startSubtitlePreparation(const MediaInfo& info);
  void updateSubtitlePreparationProgress(
      int generation,
      const MediaSubtitlePreparationProgress& progress);
  void updatePlaybackStatus();
  void displayVideoFrame(const QImage& image);
  void togglePauseResume();
  void seekToSliderValue();
  void updateTimeLabel(qint64 positionMs);
  void updateSubtitle(qint64 positionMs);
  void updateTranscriptPanel(qint64 positionMs);
  QLabel* createOptionDescription(const QString& objectName, const QString& text, QWidget* parent);
  void setupTranscriptionOptions(QVBoxLayout* layout, QWidget* parent);
  void populateTranscriptionModels();

  QPushButton* openButton_ = nullptr;
  QPushButton* pauseButton_ = nullptr;
  QPushButton* stopButton_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* videoLabel_ = nullptr;
  QLabel* subtitleLabel_ = nullptr;
  QLabel* subtitleTimeLabel_ = nullptr;
  QLabel* transcriptListLabel_ = nullptr;
  QLabel* mediaInfoLabel_ = nullptr;
  QLabel* timeLabel_ = nullptr;
  QSlider* seekSlider_ = nullptr;
  QComboBox* transcriptionModelComboBox_ = nullptr;
  QComboBox* transcriptionLanguageComboBox_ = nullptr;
  QSpinBox* transcriptionThreadSpinBox_ = nullptr;
  QLineEdit* transcriptionPromptEdit_ = nullptr;
  QFutureWatcher<MediaProbeResult>* mediaProbeWatcher_ = nullptr;
  QFutureWatcher<MediaSubtitlePreparationResult>* subtitlePreparationWatcher_ = nullptr;
  QTimer* playbackStatusTimer_ = nullptr;
  MediaPlayerCore player_;
  MediaInfo pendingPlaybackInfo_;
  std::size_t displayedVideoFrameCount_ = 0;
  qint64 durationMs_ = 0;
  qint64 pendingSeekPositionMs_ = -1;
  bool currentHasAudio_ = false;
  bool currentHasVideo_ = false;
  int subtitlePreparationGeneration_ = 0;
  SubtitleTrack subtitleTrack_;
};
