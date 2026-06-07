#include <QtTest/QtTest>

#include "core/DependencyReport.h"

class DependencyReportTests : public QObject {
  Q_OBJECT

private slots:
  void detectsConfiguredDependencies();
  void reportsMissingPath();
  void summarizesReadyState();
  void listsMissingResources();
};

void DependencyReportTests::detectsConfiguredDependencies() {
  const DependencyReport report = DependencyReport::fromConfiguredPaths();

  QVERIFY2(report.ffmpegAvailable, qPrintable(report.ffmpegPath));
  QVERIFY2(report.whisperAvailable, qPrintable(report.whisperPath));
  QVERIFY2(report.whisperModelAvailable, qPrintable(report.whisperModelPath));
}

void DependencyReportTests::reportsMissingPath() {
  const DependencyStatus status = DependencyReport::checkPath("Z:/definitely/missing/path");

  QVERIFY(!status.available);
  QCOMPARE(status.path, QStringLiteral("Z:/definitely/missing/path"));
}

void DependencyReportTests::summarizesReadyState() {
  DependencyReport report;
  report.ffmpegAvailable = true;
  report.whisperAvailable = true;
  report.whisperModelAvailable = true;

  QVERIFY(report.isReady());
  QVERIFY(report.missingItems().isEmpty());
  QCOMPARE(report.startupMessage(), QStringLiteral("启动检查通过：本地依赖与模型已就绪"));
}

void DependencyReportTests::listsMissingResources() {
  DependencyReport report;
  report.ffmpegAvailable = true;
  report.whisperAvailable = false;
  report.whisperModelAvailable = false;

  const QStringList missing = report.missingItems();

  QVERIFY(!report.isReady());
  QCOMPARE(missing.size(), 2);
  QVERIFY(missing.contains(QStringLiteral("whisper.cpp")));
  QVERIFY(missing.contains(QStringLiteral("Whisper 模型")));
  QCOMPARE(report.startupMessage(),
      QStringLiteral("启动检查失败：缺少 whisper.cpp、Whisper 模型"));
}

QTEST_MAIN(DependencyReportTests)
#include "DependencyReportTests.moc"
