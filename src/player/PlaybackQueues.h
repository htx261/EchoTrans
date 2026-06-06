#pragma once

#include "player/BlockingQueue.h"

#include <QtGlobal>

struct PlaybackPacket {
  int streamIndex = -1;
  qint64 ptsMs = 0;
};

struct PlaybackFrame {
  qint64 ptsMs = 0;
};

class PlaybackQueues {
public:
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
