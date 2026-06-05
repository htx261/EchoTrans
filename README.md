# EchoTrans

EchoTrans 是一款基于 C++ 和 Qt 的 AI 同声传译助手，面向外语演讲、技术分享、国际会议和网课场景。项目通过语音识别和本地翻译模型，将单向音频流实时转换为中文字幕，帮助用户降低语言门槛并跟上内容节奏。

第一版以字幕为主，不包含中文语音播报。系统会区分临时字幕和确认字幕，使最近的识别或翻译结果可以被自动修正。

## 功能目标

- 播放本地音视频文件。
- 基于 FFmpeg 解码媒体并抽取音频。
- 基于 whisper.cpp 自动识别音频语言并转录原文。
- 基于 CTranslate2 + NLLB 将原文翻译为简体中文。
- 在播放器中显示当前中文字幕。
- 在字幕历史区展示原文和译文。
- 支持临时字幕和确认字幕。
- 支持识别或翻译结果自动修正。
- 支持导出双语 SRT 字幕。

## 技术栈

- C++
- Qt Widgets
- CMake
- Visual Studio
- FFmpeg
- whisper.cpp
- CTranslate2
- NLLB 翻译模型

## 第三方库和模型获取

第三方库和模型文件体积较大，不提交到 Git 仓库。请从项目的 GitHub Releases 页面下载资源包。

推荐资源包名称：

```text
EchoTrans-Resources.zip
```

下载后将资源包解压到项目根目录，解压后的目录结构应为：

```text
EchoTrans/
  third_party/
    ffmpeg/
    whisper.cpp/
    ctranslate2/

  models/
    whisper/
    translation/
    tokenizers/
```

如果资源包超过 GitHub Releases 单个文件大小限制，可以拆分为：

```text
EchoTrans-ThirdParty.zip
EchoTrans-Models.zip
```

解压后仍需保证 `third_party/` 和 `models/` 位于项目根目录。

## 从源码构建

### 1. 拉取代码

```powershell
git clone <repository-url>
cd EchoTrans
```

### 2. 解压资源包

从 GitHub Releases 下载资源包，并解压到项目根目录。解压后应能看到：

```text
third_party/
models/
```

### 3. 配置 CMake

将 `<Qt-MSVC-Prefix-Path>` 替换为本机 Qt MSVC 安装路径，例如 Qt 安装目录下包含 `lib/cmake/Qt5` 的那一级目录。

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt-MSVC-Prefix-Path>
```

### 4. 构建

```powershell
cmake --build build --config Debug
```

### 5. 运行测试

```powershell
.\build\Debug\DependencyReportTests.exe
```

如果运行测试或程序时提示找不到 Qt DLL，请将本机 Qt `bin` 目录加入当前 PowerShell 会话的 `PATH`：

```powershell
$env:PATH="<Qt-Bin-Path>;" + $env:PATH
```

### 6. 运行程序

```powershell
.\build\Debug\EchoTrans.exe
```

## 当前进度

当前版本完成了项目基础骨架：

- CMake + Qt Widgets 工程结构
- 最小主窗口
- 本地依赖路径检测
- Qt Test 依赖检测测试

播放器、音频抽取、语音识别、翻译、字幕修正和字幕导出功能会在后续 PR 中分步实现。

## 第三方许可

本项目使用 FFmpeg、whisper.cpp、CTranslate2 和 NLLB 模型。第三方库和模型需遵守各自许可证。NLLB-200 distilled 600M 使用非商业许可，如果后续用于商业化场景，需要重新确认授权或更换合适模型。
