#pragma once

#include "player/PlaybackState.h"
#include "player/PlaybackQueues.h"

#include <QImage>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class MediaPlayerCore {
public:
  MediaPlayerCore() = default;
  ~MediaPlayerCore();

  MediaPlayerCore(const MediaPlayerCore&) = delete;
  MediaPlayerCore& operator=(const MediaPlayerCore&) = delete;

  PlaybackState state() const;
  QString mediaPath() const;
  std::size_t activeWorkerCount() const;
  bool playbackQueuesClosed() const;
  bool demuxFinished() const;
  std::size_t demuxedAudioPacketCount() const;
  std::size_t demuxedVideoPacketCount() const;
  QString lastDemuxError() const;
  std::size_t audioPacketQueueSize() const;
  std::size_t videoPacketQueueSize() const;
  std::size_t audioFrameQueueSize() const;
  std::size_t decodedAudioFrameCount() const;
  std::size_t decodedAudioByteCount() const;
  std::size_t audioOutputByteCount() const;
  qint64 audioClockMs() const;
  std::size_t decodedVideoFrameCount() const;
  qint64 lastPublishedVideoPtsMs() const;
  bool takeVideoFrame(QImage* image);
  bool takeTranscriptionAudioFrame(TranscriptionAudioFrame* frame);
  void setVideoFrameCallback(std::function<void(const QImage&, qint64)> callback);
  QString lastAudioDecodeError() const;
  QString lastAudioOutputError() const;
  QString lastVideoDecodeError() const;

  bool open(const QString& filePath);
  bool play();
  bool pause();
  bool resume();
  bool seekTo(qint64 positionMs);
  bool seekInProgress() const;
  void stop();

private:
  void resetPlaybackCounters(qint64 clockMs);
  void startWorkersLocked();
  void stopWorkers(bool clearMediaPath);
  void performSeek(qint64 positionMs);
  void demuxLoop();
  void videoDecodeLoop();
  void audioDecodeLoop();
  void audioOutputLoop();
  void publishVideoFramesForClock(qint64 audioClockMs, PlaybackFrame* pendingVideoFrame);
  void waitWhilePaused();
  void waitUntilStopRequested();
  void finishWorker();
  void setLastDemuxError(const QString& message);
  void setLastAudioDecodeError(const QString& message);
  void setLastAudioOutputError(const QString& message);
  void setLastVideoDecodeError(const QString& message);

  mutable std::mutex mutex_;
  PlaybackState state_ = PlaybackState::Stopped;
  QString mediaPath_;
  QString lastDemuxError_;
  QString lastAudioDecodeError_;
  QString lastAudioOutputError_;
  QString lastVideoDecodeError_;
  std::unique_ptr<PlaybackQueues> playbackQueues_;
  std::function<void(const QImage&, qint64)> videoFrameCallback_;

  std::atomic_bool stopRequested_{false};
  std::atomic_bool paused_{false};
  std::atomic_bool pauseAcknowledged_{false};
  std::atomic_bool seekInProgress_{false};
  std::atomic<qint64> startPositionMs_{0};
  std::atomic_bool demuxFinished_{false};
  std::atomic_size_t demuxedAudioPacketCount_{0};
  std::atomic_size_t demuxedVideoPacketCount_{0};
  std::atomic_size_t decodedAudioFrameCount_{0};
  std::atomic_size_t decodedAudioByteCount_{0};
  std::atomic_size_t audioOutputByteCount_{0};
  std::atomic<qint64> audioClockMs_{0};
  std::atomic_size_t decodedVideoFrameCount_{0};
  std::atomic<qint64> lastPublishedVideoPtsMs_{-1};
  std::atomic_size_t activeWorkerCount_{0};
  std::mutex workerMutex_;
  std::condition_variable workerCondition_;
  std::mutex pauseMutex_;
  std::condition_variable pauseCondition_;
  std::mutex pauseAcknowledgedMutex_;
  std::condition_variable pauseAcknowledgedCondition_;
  std::mutex controlMutex_;
  std::mutex seekThreadMutex_;
  std::vector<std::thread> workerThreads_;
  std::thread seekThread_;
};
