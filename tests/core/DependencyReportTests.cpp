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

void DependencyReportTests::summarizesReadyState() {
  DependencyReport report;
  report.ffmpegAvailable = true;
  report.whisperAvailable = true;
  report.ctranslate2Available = true;
  report.whisperModelAvailable = true;
  report.translationModelAvailable = true;
  report.tokenizerAvailable = true;

  QVERIFY(report.isReady());
  QVERIFY(report.missingItems().isEmpty());
  QCOMPARE(report.startupMessage(), QStringLiteral("启动检查通过：本地依赖与模型已就绪"));
}

void DependencyReportTests::listsMissingResources() {
  DependencyReport report;
  report.ffmpegAvailable = true;
  report.whisperAvailable = false;
  report.ctranslate2Available = true;
  report.whisperModelAvailable = false;
  report.translationModelAvailable = true;
  report.tokenizerAvailable = false;

  const QStringList missing = report.missingItems();

  QVERIFY(!report.isReady());
  QCOMPARE(missing.size(), 3);
  QVERIFY(missing.contains(QStringLiteral("whisper.cpp")));
  QVERIFY(missing.contains(QStringLiteral("Whisper 模型")));
  QVERIFY(missing.contains(QStringLiteral("NLLB Tokenizer")));
  QCOMPARE(report.startupMessage(),
      QStringLiteral("启动检查失败：缺少 whisper.cpp、Whisper 模型、NLLB Tokenizer"));
}

QTEST_MAIN(DependencyReportTests)
#include "DependencyReportTests.moc"
