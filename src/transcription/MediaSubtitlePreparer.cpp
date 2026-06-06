#include "transcription/MediaSubtitlePreparer.h"

#include <QFileInfo>
#include <QElapsedTimer>
#include <QVector>

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace {
constexpr int TargetSampleRate = 16000;
constexpr int TargetChannelCount = 1;
constexpr int TranscriptionChunkDurationMs = 30000;
constexpr AVSampleFormat TargetSampleFormat = AV_SAMPLE_FMT_FLT;

struct SpeechRange {
  int startSample = 0;
  int endSample = 0;
};

QString avErrorString(int error) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error, buffer, sizeof(buffer));
  return QString::fromUtf8(buffer);
}

void reportProgress(
    const MediaSubtitlePreparationRequest& request,
    MediaSubtitlePreparationStage stage,
    int percent,
    const QString& message) {
  if (!request.progressCallback) {
    return;
  }

  request.progressCallback(MediaSubtitlePreparationProgress{
      stage,
      std::max(0, std::min(100, percent)),
      message});
}

bool isCanceled(const MediaSubtitlePreparationRequest& request) {
  return request.cancelRequested && request.cancelRequested->load();
}

MediaSubtitlePreparationResult canceledResult() {
  MediaSubtitlePreparationResult result;
  result.canceled = true;
  result.errorMessage = QStringLiteral("转录已取消");
  return result;
}

void appendSubtitleSegments(SubtitleTrack* track, const QVector<TranscriptionTextSegment>& segments) {
  for (const TranscriptionTextSegment& segment : segments) {
    track->appendSegment(SubtitleSegment{
        segment.startMs,
        segment.endMs,
        segment.text,
        QString()});
  }
}

QVector<SpeechRange> fixedRanges(const QVector<float>& samples) {
  QVector<SpeechRange> ranges;
  const int maxSamples = TargetSampleRate * TranscriptionChunkDurationMs / 1000;
  for (int start = 0; start < samples.size(); start += maxSamples) {
    ranges.push_back(SpeechRange{start, std::min(start + maxSamples, samples.size())});
  }
  return ranges;
}

int receiveAudioFrames(
    AVFormatContext* formatContext,
    AVStream* audioStream,
    AVCodecContext* codecContext,
    SwrContext* resampler,
    AVFrame* frame,
    QVector<float>* audioSamples,
    qint64* decodedOutputSamples) {
  int result = 0;
  while ((result = avcodec_receive_frame(codecContext, frame)) >= 0) {
    const int maxOutputSamples = static_cast<int>(av_rescale_rnd(
        swr_get_delay(resampler, codecContext->sample_rate) + frame->nb_samples,
        TargetSampleRate,
        codecContext->sample_rate,
        AV_ROUND_UP));
    if (maxOutputSamples <= 0) {
      av_frame_unref(frame);
      continue;
    }

    QVector<float> samples(maxOutputSamples * TargetChannelCount);
    uint8_t* outputData = reinterpret_cast<uint8_t*>(samples.data());
    const int convertedSamples = swr_convert(
        resampler,
        &outputData,
        maxOutputSamples,
        const_cast<const uint8_t**>(frame->extended_data),
        frame->nb_samples);
    if (convertedSamples < 0) {
      av_frame_unref(frame);
      return convertedSamples;
    }

    samples.resize(convertedSamples * TargetChannelCount);
    const int oldSize = audioSamples->size();
    audioSamples->resize(oldSize + samples.size());
    std::memcpy(
        audioSamples->data() + oldSize,
        samples.constData(),
        static_cast<std::size_t>(samples.size()) * sizeof(float));

    *decodedOutputSamples += convertedSamples;
    av_frame_unref(frame);
  }

  return result == AVERROR(EAGAIN) || result == AVERROR_EOF ? 0 : result;
}
}

MediaSubtitlePreparationResult MediaSubtitlePreparer::prepare(
    const MediaSubtitlePreparationRequest& request) {
  MediaSubtitlePreparationResult preparationResult;
  QElapsedTimer totalTimer;
  totalTimer.start();
  QElapsedTimer stageTimer;

  stageTimer.start();
  if (isCanceled(request)) {
    return canceledResult();
  }

  reportProgress(request, MediaSubtitlePreparationStage::LoadingModel, 0, QStringLiteral("正在加载转录模型"));
  WhisperTranscriber transcriber;
  const TranscriptionLoadResult loadResult = transcriber.loadModel(request.options);
  if (!loadResult.success) {
    preparationResult.errorMessage = loadResult.errorMessage;
    return preparationResult;
  }

  const QFileInfo mediaInfo(request.mediaPath);
  if (!mediaInfo.exists() || !mediaInfo.isFile()) {
    preparationResult.errorMessage = QStringLiteral("媒体文件不存在：%1").arg(request.mediaPath);
    return preparationResult;
  }

  AVFormatContext* formatContext = nullptr;
  int result = avformat_open_input(&formatContext, mediaInfo.absoluteFilePath().toUtf8().constData(), nullptr, nullptr);
  if (result < 0) {
    preparationResult.errorMessage = QStringLiteral("打开媒体文件失败：%1").arg(avErrorString(result));
    return preparationResult;
  }

  result = avformat_find_stream_info(formatContext, nullptr);
  if (result < 0) {
    preparationResult.errorMessage = QStringLiteral("读取媒体流信息失败：%1").arg(avErrorString(result));
    avformat_close_input(&formatContext);
    return preparationResult;
  }

  int audioStreamIndex = -1;
  for (unsigned int index = 0; index < formatContext->nb_streams; ++index) {
    if (formatContext->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioStreamIndex = static_cast<int>(index);
      break;
    }
  }

  if (audioStreamIndex < 0) {
    preparationResult.errorMessage = QStringLiteral("媒体文件没有音频流");
    avformat_close_input(&formatContext);
    return preparationResult;
  }

  AVStream* audioStream = formatContext->streams[audioStreamIndex];
  const AVCodec* decoder = avcodec_find_decoder(audioStream->codecpar->codec_id);
  if (!decoder) {
    preparationResult.errorMessage = QStringLiteral("找不到音频解码器");
    avformat_close_input(&formatContext);
    return preparationResult;
  }

  AVCodecContext* codecContext = avcodec_alloc_context3(decoder);
  if (!codecContext) {
    preparationResult.errorMessage = QStringLiteral("创建音频解码器失败");
    avformat_close_input(&formatContext);
    return preparationResult;
  }

  result = avcodec_parameters_to_context(codecContext, audioStream->codecpar);
  if (result < 0) {
    preparationResult.errorMessage = QStringLiteral("读取音频解码参数失败：%1").arg(avErrorString(result));
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    return preparationResult;
  }

  result = avcodec_open2(codecContext, decoder, nullptr);
  if (result < 0) {
    preparationResult.errorMessage = QStringLiteral("打开音频解码器失败：%1").arg(avErrorString(result));
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    return preparationResult;
  }

  AVChannelLayout outputLayout;
  av_channel_layout_default(&outputLayout, TargetChannelCount);

  AVChannelLayout inputLayout;
  if (codecContext->ch_layout.nb_channels > 0) {
    av_channel_layout_copy(&inputLayout, &codecContext->ch_layout);
  } else {
    av_channel_layout_default(&inputLayout, std::max(1, codecContext->ch_layout.nb_channels));
  }

  SwrContext* resampler = nullptr;
  result = swr_alloc_set_opts2(
      &resampler,
      &outputLayout,
      TargetSampleFormat,
      TargetSampleRate,
      &inputLayout,
      codecContext->sample_fmt,
      codecContext->sample_rate,
      0,
      nullptr);
  if (result < 0 || !resampler) {
    preparationResult.errorMessage = QStringLiteral("创建音频重采样器失败");
    av_channel_layout_uninit(&inputLayout);
    av_channel_layout_uninit(&outputLayout);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    return preparationResult;
  }

  result = swr_init(resampler);
  if (result < 0) {
    preparationResult.errorMessage = QStringLiteral("初始化音频重采样器失败：%1").arg(avErrorString(result));
    swr_free(&resampler);
    av_channel_layout_uninit(&inputLayout);
    av_channel_layout_uninit(&outputLayout);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    return preparationResult;
  }

  stageTimer.restart();
  reportProgress(request, MediaSubtitlePreparationStage::ExtractingAudio, 0, QStringLiteral("正在提取音频"));

  QVector<float> audioSamples;
  qint64 decodedOutputSamples = 0;
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  qint64 lastProgressPositionMs = 0;
  const qint64 durationMs = formatContext->duration > 0
      ? formatContext->duration / 1000
      : 0;
  if (durationMs > 0) {
    const qint64 estimatedSamples = durationMs * TargetSampleRate / 1000 + TargetSampleRate;
    audioSamples.reserve(static_cast<int>(std::min<qint64>(estimatedSamples, std::numeric_limits<int>::max())));
  }

  while ((result = av_read_frame(formatContext, packet)) >= 0) {
    if (isCanceled(request)) {
      preparationResult = canceledResult();
      av_packet_unref(packet);
      break;
    }

    if (packet->stream_index == audioStreamIndex) {
      result = avcodec_send_packet(codecContext, packet);
      if (result >= 0) {
        result = receiveAudioFrames(
            formatContext,
            audioStream,
            codecContext,
            resampler,
            frame,
            &audioSamples,
            &decodedOutputSamples);
      }

      if (result < 0) {
        preparationResult.errorMessage = QStringLiteral("音频解码失败：%1").arg(avErrorString(result));
        av_packet_unref(packet);
        break;
      }

      const qint64 positionMs = decodedOutputSamples * 1000 / TargetSampleRate;
      if (durationMs > 0 && positionMs - lastProgressPositionMs >= 500) {
        lastProgressPositionMs = positionMs;
        reportProgress(
            request,
            MediaSubtitlePreparationStage::ExtractingAudio,
            static_cast<int>(std::min<qint64>(99, positionMs * 100 / durationMs)),
            QStringLiteral("正在提取音频，用时 %1 秒").arg(stageTimer.elapsed() / 1000.0, 0, 'f', 1));
      }
    }

    av_packet_unref(packet);
  }

  if (preparationResult.errorMessage.isEmpty() && !isCanceled(request)) {
    avcodec_send_packet(codecContext, nullptr);
    result = receiveAudioFrames(
        formatContext,
        audioStream,
        codecContext,
        resampler,
        frame,
        &audioSamples,
        &decodedOutputSamples);
    if (result < 0) {
      preparationResult.errorMessage = QStringLiteral("刷新音频解码器失败：%1").arg(avErrorString(result));
    }
  }

  av_frame_free(&frame);
  av_packet_free(&packet);
  swr_free(&resampler);
  av_channel_layout_uninit(&inputLayout);
  av_channel_layout_uninit(&outputLayout);
  avcodec_free_context(&codecContext);
  avformat_close_input(&formatContext);

  if (!preparationResult.errorMessage.isEmpty()) {
    return preparationResult;
  }

  if (isCanceled(request)) {
    return canceledResult();
  }

  reportProgress(
      request,
      MediaSubtitlePreparationStage::ExtractingAudio,
      100,
      QStringLiteral("音频提取完成，用时 %1 秒").arg(stageTimer.elapsed() / 1000.0, 0, 'f', 1));
  if (audioSamples.isEmpty()) {
    preparationResult.errorMessage = QStringLiteral("没有可转录的音频数据");
    return preparationResult;
  }

  SubtitleTrack track;
  const QVector<SpeechRange> speechRanges = fixedRanges(audioSamples);
  stageTimer.restart();
  for (int chunkIndex = 0; chunkIndex < speechRanges.size(); ++chunkIndex) {
    if (isCanceled(request)) {
      return canceledResult();
    }

    const SpeechRange& range = speechRanges[chunkIndex];
    const int offset = range.startSample;
    const int count = range.endSample - range.startSample;
    if (count <= 0) {
      continue;
    }

    reportProgress(
        request,
        MediaSubtitlePreparationStage::Transcribing,
        static_cast<int>(chunkIndex * 100 / std::max(1, speechRanges.size())),
        QStringLiteral("正在生成字幕 %1/%2，用时 %3 秒")
            .arg(chunkIndex + 1)
            .arg(speechRanges.size())
            .arg(stageTimer.elapsed() / 1000.0, 0, 'f', 1));

    const qint64 chunkStartMs =
        static_cast<qint64>(offset) * 1000 / TargetSampleRate;
    const TranscriptionResult transcriptionResult = transcriber.transcribe(
        chunkStartMs,
        audioSamples.constData() + offset,
        count);
    if (!transcriptionResult.success) {
      preparationResult.errorMessage = transcriptionResult.errorMessage;
      return preparationResult;
    }

    if (isCanceled(request)) {
      return canceledResult();
    }

    appendSubtitleSegments(&track, transcriptionResult.segments);
  }

  reportProgress(
      request,
      MediaSubtitlePreparationStage::Finished,
      100,
      QStringLiteral("字幕准备完成，总用时 %1 秒").arg(totalTimer.elapsed() / 1000.0, 0, 'f', 1));
  preparationResult.success = true;
  preparationResult.subtitleTrack = track;
  return preparationResult;
}
