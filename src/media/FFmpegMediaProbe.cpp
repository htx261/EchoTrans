#include "media/FFmpegMediaProbe.h"

#include <QDir>
#include <QFileInfo>

#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

namespace {
QString ffmpegError(int errorCode) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(errorCode, buffer, sizeof(buffer));
  return QString::fromLocal8Bit(buffer);
}

qint64 durationToMs(int64_t duration) {
  if (duration <= 0) {
    return 0;
  }
  return static_cast<qint64>(duration * 1000 / AV_TIME_BASE);
}

struct FormatContextDeleter {
  void operator()(AVFormatContext* context) const {
    avformat_close_input(&context);
  }
};
}

MediaProbeResult FFmpegMediaProbe::probe(const QString& filePath) {
  MediaProbeResult result;
  result.info.filePath = QDir::fromNativeSeparators(filePath);

  if (!QFileInfo::exists(filePath)) {
    result.errorMessage = QStringLiteral("媒体文件不存在：%1").arg(filePath);
    return result;
  }

  AVFormatContext* formatContext = nullptr;
  const QString absolutePath = QDir::fromNativeSeparators(QFileInfo(filePath).absoluteFilePath());
  const QByteArray nativePath = absolutePath.toUtf8();

  int openResult = avformat_open_input(&formatContext, nativePath.constData(), nullptr, nullptr);
  if (openResult < 0) {
    result.errorMessage = QStringLiteral("打开媒体文件失败：%1").arg(ffmpegError(openResult));
    return result;
  }

  std::unique_ptr<AVFormatContext, FormatContextDeleter> input(formatContext);

  const int streamInfoResult = avformat_find_stream_info(formatContext, nullptr);
  if (streamInfoResult < 0) {
    result.errorMessage = QStringLiteral("读取媒体流信息失败：%1").arg(ffmpegError(streamInfoResult));
    return result;
  }

  result.info.durationMs = durationToMs(formatContext->duration);

  for (unsigned int i = 0; i < formatContext->nb_streams; ++i) {
    const AVStream* stream = formatContext->streams[i];
    if (!stream || !stream->codecpar) {
      continue;
    }

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && result.info.videoStreamIndex < 0) {
      result.info.videoStreamIndex = static_cast<int>(i);
      result.info.hasVideo = true;
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && result.info.audioStreamIndex < 0) {
      result.info.audioStreamIndex = static_cast<int>(i);
      result.info.hasAudio = true;
    }
  }

  result.success = true;
  return result;
}
