#include "subtitle/SubtitleTrack.h"

#include <algorithm>
#include <utility>

void SubtitleTrack::setSegments(QVector<SubtitleSegment> segments) {
  std::sort(segments.begin(), segments.end(), [](const SubtitleSegment& left, const SubtitleSegment& right) {
    return left.startMs < right.startMs;
  });
  segments_ = std::move(segments);
}

void SubtitleTrack::appendSegment(const SubtitleSegment& segment) {
  segments_.push_back(segment);
  std::sort(segments_.begin(), segments_.end(), [](const SubtitleSegment& left, const SubtitleSegment& right) {
    return left.startMs < right.startMs;
  });
}

QVector<SubtitleSegment> SubtitleTrack::segments() const {
  return segments_;
}

QString SubtitleTrack::textAt(qint64 positionMs) const {
  for (const SubtitleSegment& segment : segments_) {
    if (positionMs >= segment.startMs && positionMs < segment.endMs) {
      return segment.translatedText.isEmpty()
          ? segment.sourceText
          : segment.translatedText;
    }
  }

  return QString();
}

bool SubtitleTrack::isEmpty() const {
  return segments_.isEmpty();
}
