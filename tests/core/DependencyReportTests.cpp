#include <QtTest/QtTest>

#include "core/DependencyReport.h"

class DependencyReportTests : public QObject {
  Q_OBJECT

private slots:
  void detectsConfiguredDependencies();
  void reportsMissingPath();
};

void DependencyReportTests::detectsConfiguredDependencies() {
  const DependencyReport report = DependencyReport::fromConfiguredPaths();

  QVERIFY2(report.ffmpegAvailable, qPrintable(report.ffmpegPath));
  QVERIFY2(report.whisperAvailable, qPrintable(report.whisperPath));
  QVERIFY2(report.ctranslate2Available, qPrintable(report.ctranslate2Path));
  QVERIFY2(report.whisperModelAvailable, qPrintable(report.whisperModelPath));
  QVERIFY2(report.translationModelAvailable, qPrintable(report.translationModelPath));
  QVERIFY2(report.tokenizerAvailable, qPrintable(report.tokenizerPath));
}

void DependencyReportTests::reportsMissingPath() {
  const DependencyStatus status = DependencyReport::checkPath("Z:/definitely/missing/path");

  QVERIFY(!status.available);
  QCOMPARE(status.path, QStringLiteral("Z:/definitely/missing/path"));
}

QTEST_MAIN(DependencyReportTests)
#include "DependencyReportTests.moc"
