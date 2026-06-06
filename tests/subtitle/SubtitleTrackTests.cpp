#include <QtTest/QtTest>

#include "subtitle/SubtitleTrack.h"

class SubtitleTrackTests : public QObject {
  Q_OBJECT

private slots:
  void returnsTranslatedTextForActiveSegment();
  void fallsBackToSourceTextWhenTranslationIsEmpty();
  void returnsEmptyTextWhenNoSegmentMatches();
  void appendsLiveSegmentsInTimeOrder();
};

void SubtitleTrackTests::returnsTranslatedTextForActiveSegment() {
  SubtitleTrack track;
  track.setSegments({
      SubtitleSegment{0, 1000, QStringLiteral("Hello"), QStringLiteral("你好")},
      SubtitleSegment{1000, 2000, QStringLiteral("Distributed systems"), QStringLiteral("分布式系统")}
  });

  QCOMPARE(track.textAt(1500), QStringLiteral("分布式系统"));
}

void SubtitleTrackTests::fallsBackToSourceTextWhenTranslationIsEmpty() {
  SubtitleTrack track;
  track.setSegments({
      SubtitleSegment{0, 1000, QStringLiteral("Original only"), QString()}
  });

  QCOMPARE(track.textAt(500), QStringLiteral("Original only"));
}

void SubtitleTrackTests::returnsEmptyTextWhenNoSegmentMatches() {
  SubtitleTrack track;
  track.setSegments({
      SubtitleSegment{1000, 2000, QStringLiteral("Later"), QStringLiteral("稍后")}
  });

  QVERIFY(track.textAt(500).isEmpty());
  QVERIFY(track.textAt(2000).isEmpty());
}

void SubtitleTrackTests::appendsLiveSegmentsInTimeOrder() {
  SubtitleTrack track;

  track.appendSegment(SubtitleSegment{1000, 2000, QStringLiteral("Second"), QString()});
  track.appendSegment(SubtitleSegment{0, 1000, QStringLiteral("First"), QString()});

  QCOMPARE(track.textAt(500), QStringLiteral("First"));
  QCOMPARE(track.textAt(1500), QStringLiteral("Second"));
  QCOMPARE(track.segments().size(), 2);
  QCOMPARE(track.segments()[0].sourceText, QStringLiteral("First"));
}

QTEST_MAIN(SubtitleTrackTests)
#include "SubtitleTrackTests.moc"
