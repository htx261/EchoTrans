# Project Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first PR: a CMake + Qt Widgets Windows project foundation that starts a main window and verifies local third-party dependency paths.

**Architecture:** This PR does not implement media playback or model inference. It creates the project shell, dependency path configuration, a small dependency report utility, and Qt Test smoke tests so later PRs can add FFmpeg playback, Whisper ASR, and CTranslate2 translation behind stable paths.

**Tech Stack:** C++17, CMake, Qt 5.12.12 Widgets, Qt Test, Visual Studio, local FFmpeg, local whisper.cpp, local CTranslate2.

---

## File Structure

Create:

- `CMakeLists.txt`: top-level project configuration, Qt discovery, dependency path cache variables, targets.
- `cmake/EchoTransDependencies.cmake`: validates local dependency directories and exposes imported FFmpeg/CTranslate2 paths.
- `src/app/main.cpp`: application entry point.
- `src/ui/MainWindow.h`: Qt Widgets main window declaration.
- `src/ui/MainWindow.cpp`: Qt Widgets main window implementation.
- `src/core/DependencyReport.h`: value type describing dependency availability.
- `src/core/DependencyReport.cpp`: checks local paths for FFmpeg, whisper.cpp, CTranslate2, and models.
- `tests/core/DependencyReportTests.cpp`: Qt Test smoke tests for dependency detection.
- `README.md`: local setup notes and first build command.

Modify:

- `.gitignore`: keep current ignores and add CTranslate2 source/build ignore rules if needed.

Do not commit:

- `models/`
- `.venv-model/`
- `build-ctranslate2/`
- `third_party/ffmpeg/bin/`

## PR 1 Review Draft

Title:

```text
feat: add Qt/CMake project foundation
```

Function description:

```text
Adds the initial Windows Qt Widgets application skeleton and local dependency checks for FFmpeg, whisper.cpp, CTranslate2, Whisper models, and NLLB model assets.
```

Implementation idea:

```text
Use CMake cache variables for local dependency paths, create a minimal Qt MainWindow, and add a Qt Test target that verifies required directories/files exist before later feature PRs depend on them.
```

Test method:

```text
Configure with CMake using Qt 5.12.12, build EchoTrans and DependencyReportTests, run the test executable, and launch EchoTrans to verify the main window opens.
```

## Task 1: Add Dependency Path Validation

**Files:**

- Create: `cmake/EchoTransDependencies.cmake`

- [ ] **Step 1: Write dependency CMake helper**

Create `cmake/EchoTransDependencies.cmake`:

```cmake
set(ECHOTRANS_FFMPEG_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg"
    CACHE PATH "Path to the FFmpeg development package")

set(ECHOTRANS_WHISPER_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/whisper.cpp"
    CACHE PATH "Path to whisper.cpp source tree")

set(ECHOTRANS_CTRANSLATE2_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/ctranslate2"
    CACHE PATH "Path to installed CTranslate2 C++ package")

set(ECHOTRANS_MODELS_ROOT
    "${CMAKE_SOURCE_DIR}/models"
    CACHE PATH "Path to local model assets")

function(echotrans_require_path variable description)
  if(NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${description} not found: ${${variable}}")
  endif()
endfunction()

echotrans_require_path(ECHOTRANS_FFMPEG_ROOT "FFmpeg root")
echotrans_require_path(ECHOTRANS_WHISPER_ROOT "whisper.cpp root")
echotrans_require_path(ECHOTRANS_CTRANSLATE2_ROOT "CTranslate2 root")

set(ECHOTRANS_FFMPEG_INCLUDE_DIR "${ECHOTRANS_FFMPEG_ROOT}/include")
set(ECHOTRANS_FFMPEG_LIB_DIR "${ECHOTRANS_FFMPEG_ROOT}/lib")
set(ECHOTRANS_FFMPEG_BIN_DIR "${ECHOTRANS_FFMPEG_ROOT}/bin")

set(ECHOTRANS_CTRANSLATE2_INCLUDE_DIR "${ECHOTRANS_CTRANSLATE2_ROOT}/include")
set(ECHOTRANS_CTRANSLATE2_LIB_DIR "${ECHOTRANS_CTRANSLATE2_ROOT}/lib")
set(ECHOTRANS_CTRANSLATE2_BIN_DIR "${ECHOTRANS_CTRANSLATE2_ROOT}/bin")

set(ECHOTRANS_WHISPER_INCLUDE_DIR "${ECHOTRANS_WHISPER_ROOT}/include")

foreach(required_path
    "${ECHOTRANS_FFMPEG_INCLUDE_DIR}"
    "${ECHOTRANS_FFMPEG_LIB_DIR}"
    "${ECHOTRANS_CTRANSLATE2_INCLUDE_DIR}"
    "${ECHOTRANS_CTRANSLATE2_LIB_DIR}"
    "${ECHOTRANS_WHISPER_INCLUDE_DIR}")
  if(NOT EXISTS "${required_path}")
    message(FATAL_ERROR "Required dependency path missing: ${required_path}")
  endif()
endforeach()
```

- [ ] **Step 2: Verify helper is syntactically loadable**

Run:

```powershell
cmake -P cmake\EchoTransDependencies.cmake
```

Expected:

```text
No output and exit code 0
```

If CMake reports a missing path, fix the corresponding dependency location before continuing.

## Task 2: Add Top-Level CMake Project

**Files:**

- Create: `CMakeLists.txt`

- [ ] **Step 1: Write top-level CMake configuration**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)

project(EchoTrans VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(cmake/EchoTransDependencies.cmake)

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Test)

add_library(EchoTransCore
  src/core/DependencyReport.cpp
)

target_include_directories(EchoTransCore
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(EchoTransCore
  PUBLIC
    ECHOTRANS_FFMPEG_ROOT="${ECHOTRANS_FFMPEG_ROOT}"
    ECHOTRANS_WHISPER_ROOT="${ECHOTRANS_WHISPER_ROOT}"
    ECHOTRANS_CTRANSLATE2_ROOT="${ECHOTRANS_CTRANSLATE2_ROOT}"
    ECHOTRANS_MODELS_ROOT="${ECHOTRANS_MODELS_ROOT}"
)

add_executable(EchoTrans
  src/app/main.cpp
  src/ui/MainWindow.cpp
)

target_link_libraries(EchoTrans
  PRIVATE
    EchoTransCore
    Qt5::Widgets
)

add_executable(DependencyReportTests
  tests/core/DependencyReportTests.cpp
)

target_link_libraries(DependencyReportTests
  PRIVATE
    EchoTransCore
    Qt5::Test
)

enable_testing()
add_test(NAME DependencyReportTests COMMAND DependencyReportTests)
```

- [ ] **Step 2: Run configure and expect missing source errors**

Run:

```powershell
cmake -S . -B build-qt5 -DCMAKE_PREFIX_PATH=D:\SoftWare\Qt\Qt5.12.12\5.12.12\msvc2017_64
```

Expected:

```text
CMake fails because src/core/DependencyReport.cpp and other source files do not exist yet.
```

This verifies CMake reaches the target definition stage.

## Task 3: Add Dependency Report Core Type

**Files:**

- Create: `src/core/DependencyReport.h`
- Create: `src/core/DependencyReport.cpp`
- Test: `tests/core/DependencyReportTests.cpp`

- [ ] **Step 1: Write failing tests**

Create `tests/core/DependencyReportTests.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test build to verify RED**

Run:

```powershell
cmake --build build-qt5 --config Debug --target DependencyReportTests
```

Expected:

```text
Build fails because core/DependencyReport.h is missing.
```

- [ ] **Step 3: Implement dependency report**

Create `src/core/DependencyReport.h`:

```cpp
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
```

Create `src/core/DependencyReport.cpp`:

```cpp
#include "core/DependencyReport.h"

#include <QDir>
#include <QFileInfo>

namespace {
QString normalizedPath(const QString& path) {
  return QDir::fromNativeSeparators(path);
}

bool fileExists(const QString& path) {
  return QFileInfo::exists(path);
}
}

DependencyStatus DependencyReport::checkPath(const QString& path) {
  DependencyStatus status;
  status.path = path;
  status.available = QFileInfo::exists(path);
  return status;
}

DependencyReport DependencyReport::fromConfiguredPaths() {
  DependencyReport report;

  report.ffmpegPath = normalizedPath(QStringLiteral(ECHOTRANS_FFMPEG_ROOT));
  report.whisperPath = normalizedPath(QStringLiteral(ECHOTRANS_WHISPER_ROOT));
  report.ctranslate2Path = normalizedPath(QStringLiteral(ECHOTRANS_CTRANSLATE2_ROOT));
  const QString modelsRoot = normalizedPath(QStringLiteral(ECHOTRANS_MODELS_ROOT));

  report.whisperModelPath = modelsRoot + QStringLiteral("/whisper/ggml-small.bin");
  report.translationModelPath = modelsRoot + QStringLiteral("/translation/nllb-200-distilled-600m-ct2-int8/model.bin");
  report.tokenizerPath = modelsRoot + QStringLiteral("/tokenizers/nllb-200-distilled-600m/tokenizer.json");

  report.ffmpegAvailable = fileExists(report.ffmpegPath + QStringLiteral("/include/libavformat/avformat.h"))
      && fileExists(report.ffmpegPath + QStringLiteral("/lib/avformat.lib"));
  report.whisperAvailable = fileExists(report.whisperPath + QStringLiteral("/include/whisper.h"));
  report.ctranslate2Available = fileExists(report.ctranslate2Path + QStringLiteral("/include/ctranslate2/translator.h"));
  report.whisperModelAvailable = fileExists(report.whisperModelPath);
  report.translationModelAvailable = fileExists(report.translationModelPath);
  report.tokenizerAvailable = fileExists(report.tokenizerPath);

  return report;
}
```

- [ ] **Step 4: Run tests to verify GREEN**

Run:

```powershell
cmake --build build-qt5 --config Debug --target DependencyReportTests
.\build-qt5\Debug\DependencyReportTests.exe
```

Expected:

```text
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted
```

## Task 4: Add Minimal Qt Widgets Application

**Files:**

- Create: `src/app/main.cpp`
- Create: `src/ui/MainWindow.h`
- Create: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Add MainWindow declaration**

Create `src/ui/MainWindow.h`:

```cpp
#pragma once

#include <QMainWindow>

class QLabel;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

private:
  QLabel* statusLabel_ = nullptr;
};
```

- [ ] **Step 2: Add MainWindow implementation**

Create `src/ui/MainWindow.cpp`:

```cpp
#include "ui/MainWindow.h"

#include "core/DependencyReport.h"

#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      statusLabel_(new QLabel(this)) {
  setWindowTitle(QStringLiteral("EchoTrans"));
  resize(1280, 720);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);

  auto* title = new QLabel(QStringLiteral("AI 同声传译助手"), central);
  title->setAlignment(Qt::AlignCenter);

  const DependencyReport report = DependencyReport::fromConfiguredPaths();
  const bool ready = report.ffmpegAvailable
      && report.whisperAvailable
      && report.ctranslate2Available
      && report.whisperModelAvailable
      && report.translationModelAvailable
      && report.tokenizerAvailable;

  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setText(ready
      ? QStringLiteral("本地依赖与模型已就绪")
      : QStringLiteral("本地依赖或模型未配置完整"));

  layout->addWidget(title);
  layout->addWidget(statusLabel_);
  setCentralWidget(central);

  statusBar()->showMessage(QStringLiteral("Ready"));
}
```

- [ ] **Step 3: Add application entry point**

Create `src/app/main.cpp`:

```cpp
#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  MainWindow window;
  window.show();

  return app.exec();
}
```

- [ ] **Step 4: Build EchoTrans**

Run:

```powershell
cmake --build build-qt5 --config Debug --target EchoTrans
```

Expected:

```text
Build succeeds and produces build-qt5\Debug\EchoTrans.exe
```

## Task 5: Add README Setup Notes

**Files:**

- Create: `README.md`

- [ ] **Step 1: Write README**

Create `README.md`:

```markdown
# EchoTrans

EchoTrans is a Windows desktop AI simultaneous translation assistant built with C++ and Qt Widgets.

## First Version Scope

- Local media playback through FFmpeg
- Audio transcription through whisper.cpp
- Local Chinese translation through CTranslate2 + NLLB
- Real-time subtitle display with temporary and confirmed subtitle states
- Bilingual SRT export

## Local Dependencies

Expected dependency layout:

```text
third_party/
  ffmpeg/
  whisper.cpp/
  ctranslate2/

models/
  whisper/
  translation/
  tokenizers/
```

`models/` is ignored by Git because model files are large.

## Configure

```powershell
cmake -S . -B build-qt5 -DCMAKE_PREFIX_PATH=D:\SoftWare\Qt\Qt5.12.12\5.12.12\msvc2017_64
```

## Build

```powershell
cmake --build build-qt5 --config Debug
```

## Test

```powershell
.\build-qt5\Debug\DependencyReportTests.exe
```
```

- [ ] **Step 2: Confirm README is plain text and does not include secrets**

Run:

```powershell
Select-String -Path README.md -Pattern "API_KEY","SECRET","TOKEN"
```

Expected:

```text
No output
```

## Task 6: Verification and Commit Review

**Files:**

- Review all files created in this plan.

- [ ] **Step 1: Run configure**

Run:

```powershell
cmake -S . -B build-qt5 -DCMAKE_PREFIX_PATH=D:\SoftWare\Qt\Qt5.12.12\5.12.12\msvc2017_64
```

Expected:

```text
Configuring done
Generating done
Build files have been written to: D:/QiNiuYun/EchoTrans/build-qt5
```

- [ ] **Step 2: Run full build**

Run:

```powershell
cmake --build build-qt5 --config Debug
```

Expected:

```text
Build succeeds for EchoTrans and DependencyReportTests
```

- [ ] **Step 3: Run dependency tests**

Run:

```powershell
.\build-qt5\Debug\DependencyReportTests.exe
```

Expected:

```text
0 failed
```

- [ ] **Step 4: Inspect Git status**

Run:

```powershell
git status --short
```

Expected:

```text
Shows only planned files and dependency-source changes.
```

- [ ] **Step 5: Present commit information for user approval**

Do not commit before user approval. Present:

```text
Title: feat: add Qt/CMake project foundation

Function description:
Adds the initial Windows Qt Widgets application skeleton and local dependency checks for FFmpeg, whisper.cpp, CTranslate2, Whisper models, and NLLB model assets.

Implementation idea:
Uses CMake cache variables for local dependency paths, creates a minimal Qt MainWindow, and adds a Qt Test target that verifies required directories/files exist before later feature PRs depend on them.

Test method:
Configure with Qt 5.12.12, build EchoTrans and DependencyReportTests, run DependencyReportTests, and launch EchoTrans to verify the main window opens.
```

- [ ] **Step 6: Commit only after explicit user approval**

After approval, run:

```powershell
git add .gitignore README.md CMakeLists.txt cmake src tests docs/superpowers/plans/2026-06-05-project-foundation-plan.md
git commit -m "feat: add Qt/CMake project foundation"
```

Do not add `models/`, `.venv-model/`, `build-qt5/`, or `build-ctranslate2/`.

## Self-Review

- Spec coverage: This plan covers project foundation, Git-friendly layout, Qt/CMake setup, local dependency paths, and dependency smoke tests. It intentionally does not implement player, ASR, translation, subtitle correction, or export; those require separate PR plans.
- Placeholder scan: All sections contain concrete paths, commands, and expected results.
- Type consistency: `DependencyReport`, `DependencyStatus`, `fromConfiguredPaths`, and `checkPath` are consistently named across header, implementation, and tests.
