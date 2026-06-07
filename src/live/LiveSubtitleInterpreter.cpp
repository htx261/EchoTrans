#include "live/LiveSubtitleInterpreter.h"

#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace {
struct LiveAudioChunk {
  qint64 startPtsMs = 0;
  qint64 displayStartPtsMs = 0;
  qint64 displayEndPtsMs = 0;
  int sampleRate = 16000;
  int channelCount = 1;
  QVector<float> samples;
  bool stable = false;
  bool endOfStream = false;
};

bool isCanceled(const LiveSubtitleInterpreterRequest& request) {
  return request.cancelRequested && request.cancelRequested->load();
}

LiveSubtitleInterpreterResult canceledResult() {
  LiveSubtitleInterpreterResult result;
  result.canceled = true;
  result.errorMessage = QStringLiteral("同声传译已取消");
  return result;
}

void reportProgress(
    const LiveSubtitleInterpreterRequest& request,
    LiveSubtitleInterpreterStage stage,
    const QString& message) {
  if (!request.progressCallback) {
    return;
  }

  request.progressCallback(LiveSubtitleInterpreterProgress{stage, message});
}

SubtitleTrack trackFromSegments(const QVector<SubtitleSegment>& segments) {
  SubtitleTrack track;
  track.setSegments(segments);
  return track;
}

bool hasMatchingWrapper(const QString& value, QChar left, QChar right) {
  return value.size() >= 2 && value.front() == left && value.back() == right;
}

QString normalizedCueLabel(QString value) {
  value = value.trimmed().toLower();
  value.replace(QStringLiteral("♪"), QString());
  value.replace(QStringLiteral("♫"), QString());
  value.replace(QStringLiteral("♬"), QString());
  value.replace(QStringLiteral("♩"), QString());

  bool changed = true;
  while (changed) {
    const QString previous = value;
    value = value.trimmed();
    if (hasMatchingWrapper(value, QLatin1Char('['), QLatin1Char(']'))
        || hasMatchingWrapper(value, QLatin1Char('('), QLatin1Char(')'))
        || hasMatchingWrapper(value, QLatin1Char('{'), QLatin1Char('}'))
        || hasMatchingWrapper(value, QStringLiteral("（").front(), QStringLiteral("）").front())
        || hasMatchingWrapper(value, QStringLiteral("【").front(), QStringLiteral("】").front())) {
      value = value.mid(1, value.size() - 2);
    }

    while (!value.isEmpty()) {
      const QChar first = value.front();
      if (first.isSpace() || QStringLiteral("\"'`.,:;!?-_*").contains(first)
          || QStringLiteral("。，“”‘’：；！？、").contains(first)) {
        value.remove(0, 1);
        continue;
      }
      break;
    }

    while (!value.isEmpty()) {
      const QChar last = value.back();
      if (last.isSpace() || QStringLiteral("\"'`.,:;!?-_*").contains(last)
          || QStringLiteral("。，“”‘’：；！？、").contains(last)) {
        value.chop(1);
        continue;
      }
      break;
    }
    value = value.simplified();
    changed = value != previous;
  }

  return value;
}

bool isNonSpeechCue(const QString& text) {
  const QString cue = normalizedCueLabel(text);
  if (cue.isEmpty()) {
    return false;
  }

  const QStringList labels = {
      QStringLiteral("music"),
      QStringLiteral("background music"),
      QStringLiteral("instrumental music"),
      QStringLiteral("soft music"),
      QStringLiteral("upbeat music"),
      QStringLiteral("dramatic music"),
      QStringLiteral("applause"),
      QStringLiteral("clapping"),
      QStringLiteral("laughter"),
      QStringLiteral("laughing"),
      QStringLiteral("noise"),
      QStringLiteral("silence"),
      QStringLiteral("inaudible"),
      QStringLiteral("音乐"),
      QStringLiteral("背景音乐"),
      QStringLiteral("音乐声"),
      QStringLiteral("掌声"),
      QStringLiteral("鼓掌"),
      QStringLiteral("笑声"),
      QStringLiteral("噪音"),
      QStringLiteral("杂音"),
      QStringLiteral("无声"),
      QStringLiteral("静音"),
      QStringLiteral("听不清")
  };
  return labels.contains(cue);
}

QString combinedText(const QVector<TranscriptionTextSegment>& segments) {
  QString text;
  for (const TranscriptionTextSegment& segment : segments) {
    const QString trimmed = segment.text.trimmed();
    if (trimmed.isEmpty() || isNonSpeechCue(trimmed)) {
      continue;
    }
    if (!text.isEmpty()) {
      text += QLatin1Char(' ');
    }
    text += trimmed;
  }
  text = text.trimmed();
  return isNonSpeechCue(text) ? QString() : text;
}

class LiveAudioStreamWindow {
public:
  LiveAudioStreamWindow(int stepMs, int lengthMs, int keepMs)
      : stepMs_(std::max(100, stepMs)),
        lengthMs_(std::max(stepMs_, lengthMs)),
        keepMs_(std::max(0, std::min(keepMs, stepMs_))) {
    stepSamples_ = std::max<qint64>(1, sampleRate_ * stepMs_ / 1000);
    windowSamples_ = std::max<qint64>(stepSamples_, sampleRate_ * lengthMs_ / 1000);
    keepSamples_ = std::max<qint64>(0, sampleRate_ * keepMs_ / 1000);
    chunksPerBlock_ = std::max(1, lengthMs_ / stepMs_ - 1);
    nextEmitEndSample_ = stepSamples_;
  }

  void appendFrame(
      qint64 ptsMs,
      int sampleRate,
      int channelCount,
      const QVector<float>& interleavedSamples) {
    if (sampleRate <= 0 || channelCount <= 0 || interleavedSamples.isEmpty()) {
      return;
    }

    const QVector<float> normalized = normalizeToMono16k(sampleRate, channelCount, interleavedSamples);
    if (normalized.isEmpty()) {
      return;
    }

    if (!hasStartPts_) {
      startPtsMs_ = ptsMs;
      hasStartPts_ = true;
    }

    samples_.reserve(samples_.size() + normalized.size());
    for (float sample : normalized) {
      samples_.push_back(sample);
    }
    totalSamplesSeen_ += normalized.size();
    emitReadyWindows();
  }

  void appendEndOfStream() {
    if (totalSamplesSeen_ > 0 && totalSamplesSeen_ != lastEmittedEndSample_) {
      emitWindow(totalSamplesSeen_, true);
    }

    LiveAudioChunk endChunk;
    endChunk.endOfStream = true;
    readyChunks_.push_back(endChunk);
  }

  QVector<LiveAudioChunk> takeReadyChunks() {
    QVector<LiveAudioChunk> chunks = readyChunks_;
    readyChunks_.clear();
    return chunks;
  }

private:
  QVector<float> normalizeToMono16k(
      int sampleRate,
      int channelCount,
      const QVector<float>& interleavedSamples) const {
    const int sourceFrameCount = interleavedSamples.size() / channelCount;
    if (sourceFrameCount <= 0) {
      return {};
    }

    QVector<float> mono;
    mono.reserve(sourceFrameCount);
    for (int frameIndex = 0; frameIndex < sourceFrameCount; ++frameIndex) {
      float sum = 0.0f;
      const int baseIndex = frameIndex * channelCount;
      for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
        sum += interleavedSamples[baseIndex + channelIndex];
      }
      mono.push_back(sum / static_cast<float>(channelCount));
    }

    const int targetFrameCount = static_cast<int>(
        std::floor(static_cast<double>(sourceFrameCount) * sampleRate_ / sampleRate));
    if (targetFrameCount <= 0) {
      return {};
    }

    QVector<float> resampled;
    resampled.reserve(targetFrameCount);
    for (int targetIndex = 0; targetIndex < targetFrameCount; ++targetIndex) {
      const double sourcePosition =
          static_cast<double>(targetIndex) * sampleRate / sampleRate_;
      const int leftIndex = static_cast<int>(std::floor(sourcePosition));
      const int rightIndex = std::min(leftIndex + 1, sourceFrameCount - 1);
      const float ratio = static_cast<float>(sourcePosition - leftIndex);
      resampled.push_back(mono[leftIndex] * (1.0f - ratio) + mono[rightIndex] * ratio);
    }
    return resampled;
  }

  void emitReadyWindows() {
    while (totalSamplesSeen_ >= nextEmitEndSample_) {
      emitWindow(nextEmitEndSample_, false);
      nextEmitEndSample_ += stepSamples_;
    }
  }

  void emitWindow(qint64 windowEndSample, bool forceStable) {
    const qint64 windowStartSample = bufferStartSample_;
    const qint64 relativeStart = windowStartSample - bufferStartSample_;
    const qint64 relativeEnd = windowEndSample - bufferStartSample_;
    if (relativeStart < 0 || relativeEnd <= relativeStart || relativeEnd > samples_.size()) {
      return;
    }

    LiveAudioChunk chunk;
    chunk.startPtsMs = startPtsMs_ + windowStartSample * 1000 / sampleRate_;
    chunk.displayStartPtsMs = startPtsMs_ + blockDisplayStartSample_ * 1000 / sampleRate_;
    chunk.displayEndPtsMs = startPtsMs_ + windowEndSample * 1000 / sampleRate_;
    chunk.sampleRate = sampleRate_;
    chunk.channelCount = 1;
    chunk.samples = samples_.mid(
        static_cast<int>(relativeStart),
        static_cast<int>(relativeEnd - relativeStart));

    const bool stable = forceStable || chunksInBlock_ + 1 >= chunksPerBlock_;
    chunk.stable = stable;
    readyChunks_.push_back(std::move(chunk));
    lastEmittedEndSample_ = windowEndSample;

    ++chunksInBlock_;
    if (stable) {
      const qint64 pruneBeforeSample = std::max<qint64>(0, windowEndSample - keepSamples_);
      pruneBefore(pruneBeforeSample);
      blockDisplayStartSample_ = windowEndSample;
      chunksInBlock_ = 0;
      return;
    }

    const qint64 maxWindowSamples = windowSamples_ + keepSamples_;
    const qint64 pruneBeforeSample = std::max<qint64>(0, windowEndSample - maxWindowSamples);
    pruneBefore(pruneBeforeSample);
  }

  void pruneBefore(qint64 pruneBeforeSample) {
    const qint64 pruneCount = pruneBeforeSample - bufferStartSample_;
    if (pruneCount > 0) {
      samples_.erase(samples_.begin(), samples_.begin() + static_cast<int>(pruneCount));
      bufferStartSample_ += pruneCount;
    }
  }

  static constexpr int sampleRate_ = 16000;
  int stepMs_ = 500;
  int lengthMs_ = 4000;
  int keepMs_ = 200;
  int chunksPerBlock_ = 7;
  int chunksInBlock_ = 0;
  qint64 stepSamples_ = 8000;
  qint64 windowSamples_ = 64000;
  qint64 keepSamples_ = 3200;
  qint64 nextEmitEndSample_ = 8000;
  qint64 lastEmittedEndSample_ = -1;
  qint64 totalSamplesSeen_ = 0;
  qint64 bufferStartSample_ = 0;
  qint64 blockDisplayStartSample_ = 0;
  qint64 startPtsMs_ = 0;
  bool hasStartPts_ = false;
  QVector<float> samples_;
  QVector<LiveAudioChunk> readyChunks_;
};

class LiveTranslationDispatcher {
public:
  using TranslateTrack = std::function<BaiduTranslationResult(const BaiduTranslationRequest&)>;
  using EmitSegment = std::function<void(const SubtitleSegment&)>;

  LiveTranslationDispatcher(
      const LiveSubtitleInterpreterRequest& request,
      TranslateTrack translateTrack,
      EmitSegment emitSegment)
      : request_(request),
        translateTrack_(std::move(translateTrack)),
        emitSegment_(std::move(emitSegment)) {
    worker_ = std::thread([this]() {
      run();
    });
  }

  ~LiveTranslationDispatcher() {
    cancelPending();
  }

  LiveTranslationDispatcher(const LiveTranslationDispatcher&) = delete;
  LiveTranslationDispatcher& operator=(const LiveTranslationDispatcher&) = delete;

  void queueLatest(const SubtitleSegment& segment) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (failed_ || canceled_) {
        return;
      }
      pendingSegment_ = segment;
      hasPendingSegment_ = true;
    }
    condition_.notify_one();
  }

  bool finish() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      drainPendingOnStop_ = true;
    }
    condition_.notify_one();
    joinWorker();
    return !hasTerminalResult();
  }

  void cancelPending() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      drainPendingOnStop_ = false;
      hasPendingSegment_ = false;
    }
    condition_.notify_one();
    joinWorker();
  }

  bool hasTerminalResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_ || canceled_;
  }

  bool canceled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return canceled_;
  }

  QString errorMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return errorMessage_;
  }

private:
  void joinWorker() {
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  void run() {
    QElapsedTimer timer;
    timer.start();
    qint64 lastTranslationStartMs = -1;

    while (true) {
      SubtitleSegment sourceSegment;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() {
          return hasPendingSegment_ || stopping_ || isCanceled(request_);
        });

        if (isCanceled(request_)) {
          canceled_ = true;
          hasPendingSegment_ = false;
          return;
        }

        if (stopping_ && !drainPendingOnStop_) {
          hasPendingSegment_ = false;
          return;
        }

        if (stopping_ && !hasPendingSegment_) {
          return;
        }

        while (!(stopping_ && drainPendingOnStop_)
            && lastTranslationStartMs >= 0
            && request_.translationIntervalMs > 0
            && timer.elapsed() - lastTranslationStartMs < request_.translationIntervalMs) {
          const qint64 waitMs = request_.translationIntervalMs
              - (timer.elapsed() - lastTranslationStartMs);
          condition_.wait_for(lock, std::chrono::milliseconds(std::max<qint64>(1, waitMs)));

          if (isCanceled(request_)) {
            canceled_ = true;
            hasPendingSegment_ = false;
            return;
          }
          if (stopping_ && !drainPendingOnStop_) {
            hasPendingSegment_ = false;
            return;
          }
          if (stopping_ && !hasPendingSegment_) {
            return;
          }
        }

        if (!hasPendingSegment_) {
          continue;
        }

        sourceSegment = pendingSegment_;
        hasPendingSegment_ = false;
        lastTranslationStartMs = timer.elapsed();
      }

      reportProgress(
          request_,
          LiveSubtitleInterpreterStage::Translating,
          QStringLiteral("正在翻译同声传译字幕"));

      QVector<SubtitleSegment> sourceSegments;
      sourceSegments.push_back(sourceSegment);
      BaiduTranslationRequest translationRequest;
      translationRequest.sourceTrack = trackFromSegments(sourceSegments);
      translationRequest.settings = request_.translationSettings;
      translationRequest.sourceLanguage = request_.sourceLanguage;
      translationRequest.targetLanguage = request_.targetLanguage;
      translationRequest.cancelRequested = request_.cancelRequested;

      const BaiduTranslationResult translationResult = translateTrack_(translationRequest);
      if (translationResult.canceled) {
        std::lock_guard<std::mutex> lock(mutex_);
        canceled_ = true;
        hasPendingSegment_ = false;
        return;
      }
      if (!translationResult.success) {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
        errorMessage_ = translationResult.errorMessage;
        hasPendingSegment_ = false;
        return;
      }

      const QVector<SubtitleSegment> translatedSegments = translationResult.subtitleTrack.segments();
      for (const SubtitleSegment& segment : translatedSegments) {
        emitSegment_(segment);
      }
    }
  }

  const LiveSubtitleInterpreterRequest& request_;
  TranslateTrack translateTrack_;
  EmitSegment emitSegment_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::thread worker_;
  SubtitleSegment pendingSegment_;
  bool hasPendingSegment_ = false;
  bool stopping_ = false;
  bool drainPendingOnStop_ = false;
  bool failed_ = false;
  bool canceled_ = false;
  QString errorMessage_;
};
}

LiveSubtitleInterpreterResult LiveSubtitleInterpreter::run(
    const LiveSubtitleInterpreterRequest& request) {
  LiveSubtitleInterpreterResult result;

  if (!request.takeAudioFrame) {
    result.errorMessage = QStringLiteral("同声传译缺少音频输入");
    return result;
  }

  if (isCanceled(request)) {
    return canceledResult();
  }

  WhisperTranscriber transcriber;
  std::function<TranscriptionResult(const TranscriptionAudioInput&)> transcribeChunk =
      request.transcribeChunk;
  if (!transcribeChunk) {
    reportProgress(request, LiveSubtitleInterpreterStage::LoadingModel, QStringLiteral("正在加载同声传译模型"));
    const TranscriptionLoadResult loadResult = transcriber.loadModel(request.options);
    if (!loadResult.success) {
      result.errorMessage = loadResult.errorMessage;
      return result;
    }

    transcribeChunk = [&transcriber](const TranscriptionAudioInput& audio) {
      return transcriber.transcribe(audio, TranscriptionRuntimeOptions::streaming());
    };
  }

  std::function<BaiduTranslationResult(const BaiduTranslationRequest&)> translateTrack =
      request.translateTrack;
  if (!translateTrack) {
    translateTrack = [](const BaiduTranslationRequest& translationRequest) {
      BaiduTranslator translator;
      return translator.translate(translationRequest);
    };
  }

  LiveAudioStreamWindow audioWindow(
      request.streamStepMs,
      request.streamLengthMs,
      request.streamKeepMs);
  std::mutex resultMutex;
  qint64 lastEmittedEndMs = -1;

  auto emitSegment = [&](const SubtitleSegment& segment) {
    {
      std::lock_guard<std::mutex> lock(resultMutex);
      result.subtitleTrack.upsertSegment(segment);
      ++result.emittedSegmentCount;
      lastEmittedEndMs = std::max(lastEmittedEndMs, segment.endMs);
    }
    if (request.segmentCallback) {
      request.segmentCallback(segment);
    }
  };

  std::unique_ptr<LiveTranslationDispatcher> translationDispatcher;
  if (request.translateSegments) {
    translationDispatcher.reset(new LiveTranslationDispatcher(request, translateTrack, emitSegment));
  }

  auto applyTranslationTerminalResult = [&]() {
    if (!translationDispatcher) {
      return;
    }
    if (translationDispatcher->canceled()) {
      result.canceled = true;
      result.errorMessage = QStringLiteral("同声传译已取消");
      return;
    }
    result.errorMessage = translationDispatcher->errorMessage();
  };

  auto finishTranslations = [&]() -> bool {
    if (!translationDispatcher) {
      return true;
    }
    if (!translationDispatcher->finish()) {
      applyTranslationTerminalResult();
      return false;
    }
    return true;
  };

  auto processReadyChunks = [&]() -> bool {
    const QVector<LiveAudioChunk> chunks = audioWindow.takeReadyChunks();
    for (const LiveAudioChunk& chunk : chunks) {
      if (isCanceled(request)) {
        result = canceledResult();
        return false;
      }
      if (chunk.endOfStream || chunk.samples.isEmpty()) {
        continue;
      }

      reportProgress(
          request,
          LiveSubtitleInterpreterStage::Transcribing,
          QStringLiteral("正在转录同声传译切片 %1").arg(chunk.startPtsMs));

      TranscriptionAudioInput audio;
      audio.startPtsMs = chunk.startPtsMs;
      audio.sampleRate = chunk.sampleRate;
      audio.channelCount = chunk.channelCount;
      audio.samples = chunk.samples;
      const TranscriptionResult transcriptionResult = transcribeChunk(audio);
      if (!transcriptionResult.success) {
        result.errorMessage = transcriptionResult.errorMessage;
        return false;
      }

      const QString text = combinedText(transcriptionResult.segments);
      if (text.isEmpty()) {
        continue;
      }

      SubtitleSegment segment{
          chunk.displayStartPtsMs,
          std::max(chunk.displayStartPtsMs + 1, chunk.displayEndPtsMs),
          text,
          QString()};

      if (!request.translateSegments) {
        emitSegment(segment);
        continue;
      }

      translationDispatcher->queueLatest(segment);
      if (translationDispatcher->hasTerminalResult()) {
        applyTranslationTerminalResult();
        return false;
      }
    }
    return true;
  };

  reportProgress(request, LiveSubtitleInterpreterStage::WaitingAudio, QStringLiteral("正在等待同声传译音频"));
  while (!isCanceled(request)) {
    if (translationDispatcher && translationDispatcher->hasTerminalResult()) {
      applyTranslationTerminalResult();
      return result;
    }

    TranscriptionAudioFrame frame;
    if (request.takeAudioFrame(&frame)) {
      if (frame.endOfStream) {
        audioWindow.appendEndOfStream();
        if (!processReadyChunks() || !finishTranslations()) {
          return result;
        }
        break;
      }

      audioWindow.appendFrame(
          frame.ptsMs,
          frame.sampleRate,
          frame.channelCount,
          frame.samples);
      if (!processReadyChunks()) {
        return result;
      }
      continue;
    }

    if (request.isPlaybackActive && !request.isPlaybackActive()) {
      audioWindow.appendEndOfStream();
      if (!processReadyChunks() || !finishTranslations()) {
        return result;
      }
      break;
    }
    QThread::msleep(10);
  }

  if (isCanceled(request)) {
    return canceledResult();
  }

  reportProgress(request, LiveSubtitleInterpreterStage::Finished, QStringLiteral("同声传译完成"));
  result.success = result.errorMessage.isEmpty();
  return result;
}
