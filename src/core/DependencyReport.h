#pragma once

#include <QString>
#include <QStringList>

struct DependencyStatus {
  QString path;
  bool available = false;
};

struct DependencyReport {
  QString ffmpegPath;
  QString whisperPath;
  QString whisperModelPath;

  bool ffmpegAvailable = false;
  bool whisperAvailable = false;
  bool whisperModelAvailable = false;

  bool isReady() const;
  QStringList missingItems() const;
  QString startupMessage() const;

  static DependencyStatus checkPath(const QString& path);
  static DependencyReport fromConfiguredPaths();
};
