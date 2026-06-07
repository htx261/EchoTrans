#pragma once

#include "live/LiveSubtitleInterpreter.h"
#include "media/MediaInfo.h"
#include "player/MediaPlayerCore.h"
#include "subtitle/SubtitleTrack.h"
#include "transcription/MediaSubtitlePreparer.h"
#include "translation/BaiduTranslator.h"

#include <QFutureWatcher>
#include <QMainWindow>

#include <atomic>
#include <memory>

class QLabel;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QProgressBar;
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
  BaiduTranslationSettings baiduTranslationSettings() const;
#ifdef ECHOTRANS_TESTING
  void setPendingPlaybackInfoForTest(const MediaInfo& info);
  bool importWhisperModelForTest(const QString& sourcePath);
  void displayLiveSubtitleSegmentForTest(const SubtitleSegment& segment, qint64 playbackPositionMs);
#endif

private:
  void openMediaFile();
  void onMediaProbeFinished();
  void onSubtitlePreparationFinished();
  void showMediaInfo(const MediaProbeResult& result);
  void startSubtitlePreparation(
      const MediaInfo& info,
      bool translateAfterTranscription,
      bool updateDuringPlayback = false);
  void startPendingPlayback();
  void startPendingTranscription();
  void startPendingLiveSubtitle();
  void startSelectedTask();
  void appendLiveSubtitleSegment(int generation, const SubtitleSegment& segment);
  void cancelSubtitlePreparation();
  void setTaskButtonsEnabled(bool enabled);
  bool canStartSelectedTask() const;
  bool useTranslationForSubtitleTask() const;
  void showStatusProgress(int percent);
  void hideStatusProgress();
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
  void setupTaskOptions(QVBoxLayout* layout, QWidget* parent);
  void setupTranscriptionOptions(QVBoxLayout* layout, QWidget* parent);
  void setupTranslationOptions(QVBoxLayout* layout, QWidget* parent);
  void updateTaskSettingsVisibility();
  void importWhisperModel();
  bool importWhisperModelFromPath(const QString& sourcePath, bool askBeforeOverwrite);
  void populateTranscriptionModels();
  void loadBaiduTranslationSettings();
  void saveBaiduTranslationSettings();

  QPushButton* openButton_ = nullptr;
  QPushButton* pauseButton_ = nullptr;
  QPushButton* cancelTaskButton_ = nullptr;
  QPushButton* startTaskButton_ = nullptr;
  QPushButton* importWhisperModelButton_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* liveInterpretationDescription_ = nullptr;
  QLabel* videoLabel_ = nullptr;
  QLabel* subtitleLabel_ = nullptr;
  QLabel* subtitleTimeLabel_ = nullptr;
  QLabel* transcriptListLabel_ = nullptr;
  QLabel* mediaInfoLabel_ = nullptr;
  QLabel* timeLabel_ = nullptr;
  QSlider* seekSlider_ = nullptr;
  QCheckBox* useTranslationCheckBox_ = nullptr;
  QComboBox* taskTypeComboBox_ = nullptr;
  QComboBox* transcriptionModelComboBox_ = nullptr;
  QComboBox* transcriptionLanguageComboBox_ = nullptr;
  QSpinBox* transcriptionThreadSpinBox_ = nullptr;
  QLineEdit* transcriptionPromptEdit_ = nullptr;
  QGroupBox* transcriptionOptionsPanel_ = nullptr;
  QGroupBox* translationSettingsPanel_ = nullptr;
  QLineEdit* baiduAppIdEdit_ = nullptr;
  QLineEdit* baiduSecretKeyEdit_ = nullptr;
  QPushButton* saveBaiduSettingsButton_ = nullptr;
  QProgressBar* statusProgressBar_ = nullptr;
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
  bool subtitlePreparationUpdatesPlayback_ = false;
  int subtitlePreparationGeneration_ = 0;
  std::shared_ptr<std::atomic_bool> subtitlePreparationCancelRequested_;
  SubtitleTrack subtitleTrack_;
  QString latestLiveSubtitleText_;
};
