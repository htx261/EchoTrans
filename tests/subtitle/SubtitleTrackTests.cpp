#include <QtTest/QtTest>

#include "subtitle/SubtitleTrack.h"

class SubtitleTrackTests : public QObject {
  Q_OBJECT

private slots:
  void returnsTranslatedTextForActiveSegment();
  void fallsBackToSourceTextWhenTranslationIsEmpty();
  void returnsEmptyTextWhenNoSegmentMatches();
  void appendsLiveSegmentsInTimeOrder();
  void replacesOverlappingLiveSegments();
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

void SubtitleTrackTests::replacesOverlappingLiveSegments() {
  SubtitleTrack track;
  track.setSegments({
      SubtitleSegment{0, 800, QStringLiteral("uncertain"), QStringLiteral("不准确")},
      SubtitleSegment{1200, 1800, QStringLiteral("later"), QStringLiteral("稍后")}
  });

  track.upsertSegment(SubtitleSegment{
      0,
      1200,
      QStringLiteral("confirmed"),
      QStringLiteral("已确认")});

  QCOMPARE(track.segments().size(), 2);
  QCOMPARE(track.segments()[0].startMs, 0);
  QCOMPARE(track.segments()[0].endMs, 1200);
  QCOMPARE(track.segments()[0].translatedText, QStringLiteral("已确认"));
  QCOMPARE(track.segments()[1].sourceText, QStringLiteral("later"));
}

QTEST_MAIN(SubtitleTrackTests)
#include "SubtitleTrackTests.moc"
