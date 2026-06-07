#include "subtitle/SubtitleTrack.h"

#include <algorithm>
#include <utility>

namespace {
bool startsBefore(const SubtitleSegment& left, const SubtitleSegment& right) {
  return left.startMs < right.startMs;
}

bool overlaps(const SubtitleSegment& left, const SubtitleSegment& right) {
  return left.startMs < right.endMs && right.startMs < left.endMs;
}
}

void SubtitleTrack::setSegments(QVector<SubtitleSegment> segments) {
  std::sort(segments.begin(), segments.end(), startsBefore);
  segments_ = std::move(segments);
}

void SubtitleTrack::appendSegment(const SubtitleSegment& segment) {
  segments_.push_back(segment);
  std::sort(segments_.begin(), segments_.end(), startsBefore);
}

void SubtitleTrack::upsertSegment(const SubtitleSegment& segment) {
  segments_.erase(
      std::remove_if(segments_.begin(), segments_.end(), [&](const SubtitleSegment& existing) {
        return overlaps(existing, segment);
      }),
      segments_.end());
  appendSegment(segment);
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
