#pragma once

#include "player/BlockingQueue.h"

#include <QByteArray>
#include <QtGlobal>

#include <memory>

struct AVPacket;

struct AvPacketDeleter {
  void operator()(AVPacket* packet) const;
};

using AvPacketPtr = std::shared_ptr<AVPacket>;

struct PlaybackPacket {
  int streamIndex = -1;
  qint64 ptsMs = 0;
  qint64 durationMs = 0;
  AvPacketPtr packet;
  bool endOfStream = false;
};

struct PlaybackFrame {
  qint64 ptsMs = 0;
  QByteArray pcmData;
  int sampleRate = 0;
  int channelCount = 0;
  int bytesPerSample = 0;
  bool endOfStream = false;
};

class PlaybackQueues {
public:
  static constexpr std::size_t VideoPacketQueueCapacity = 128;
  static constexpr std::size_t AudioPacketQueueCapacity = 256;
  static constexpr std::size_t VideoFrameQueueCapacity = 8;
  static constexpr std::size_t AudioFrameQueueCapacity = 32;

  PlaybackQueues()
      : videoPackets(VideoPacketQueueCapacity),
        audioPackets(AudioPacketQueueCapacity),
        videoFrames(VideoFrameQueueCapacity),
        audioFrames(AudioFrameQueueCapacity) {
  }

  void closeAll() {
    videoPackets.close();
    audioPackets.close();
    videoFrames.close();
    audioFrames.close();
  }

  void clearAll() {
    videoPackets.clear();
    audioPackets.clear();
    videoFrames.clear();
    audioFrames.clear();
  }

  bool isClosed() const {
    return videoPackets.isClosed()
        && audioPackets.isClosed()
        && videoFrames.isClosed()
        && audioFrames.isClosed();
  }

  BlockingQueue<PlaybackPacket> videoPackets;
  BlockingQueue<PlaybackPacket> audioPackets;
  BlockingQueue<PlaybackFrame> videoFrames;
  BlockingQueue<PlaybackFrame> audioFrames;
};
