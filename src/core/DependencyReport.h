#pragma once

#include <QString>

struct DependencyStatus {
  QString path;
  bool available = false;
};

struct DependencyReport {
  QString ffmpegPath;
  QString whisperPath;
  QString ctranslate2Path;
  QString whisperModelPath;
  QString translationModelPath;
  QString tokenizerPath;

  bool ffmpegAvailable = false;
  bool whisperAvailable = false;
  bool ctranslate2Available = false;
  bool whisperModelAvailable = false;
  bool translationModelAvailable = false;
  bool tokenizerAvailable = false;

  static DependencyStatus checkPath(const QString& path);
  static DependencyReport fromConfiguredPaths();
};
