#pragma once

#include <QString>
#include <QVector>

struct SubtitleSegment {
  qint64 startMs = 0;
  qint64 endMs = 0;
  QString sourceText;
  QString translatedText;
};

class SubtitleTrack {
public:
  void setSegments(QVector<SubtitleSegment> segments);
  void appendSegment(const SubtitleSegment& segment);
  void upsertSegment(const SubtitleSegment& segment);
  QVector<SubtitleSegment> segments() const;
  QString textAt(qint64 positionMs) const;
  bool isEmpty() const;

private:
  QVector<SubtitleSegment> segments_;
};
