# EchoTrans

EchoTrans 是一款基于 C++ 和 Qt 的 AI 同声传译助手，面向外语演讲、技术分享、国际会议和网课场景。项目使用 FFmpeg 播放和解码本地媒体文件，使用 whisper.cpp 对音频进行本地转录，并通过百度翻译 API 生成中文字幕。

当前版本以字幕呈现为主，不包含中文语音播报。

## 功能目标

- 打开并播放本地音视频文件。
- 基于 FFmpeg 实现解封装、音频解码、视频解码和播放。
- 基于 whisper.cpp 将音频转录为原文字幕。
- 可选通过百度翻译 API 将原文字幕翻译为中文字幕。
- 在播放器中显示当前字幕。
- 在字幕区域展示转录和翻译结果。
- 支持直接播放、预处理字幕、实时字幕三种任务模式。
- 支持“使用翻译”开关：关闭时输出原文转录字幕，开启时输出翻译字幕。
- 支持播放控制：暂停、继续、取消任务、点击跳转和拖拽跳转。
- 支持用户自行配置百度翻译 APP ID 和密钥。
- 支持在程序中导入 whisper.cpp 的 ggml `.bin` 模型文件。

## 技术栈

- C++17
- Qt 5.12.12，MSVC 2017 64-bit Kit
- CMake
- Visual Studio
- FFmpeg
- whisper.cpp
- 百度翻译开放平台 API

## 第三方库和模型

第三方库和模型文件体积较大，不提交到 Git 仓库。请从项目的 GitHub Releases 页面下载资源包，或按相同目录结构自行准备。

资源包解压后，项目根目录应包含：

```text
EchoTrans/
  third_party/
    ffmpeg/
    openssl/
    whisper.cpp/

  models/
    whisper/
```

其中：

- `third_party/ffmpeg`：FFmpeg 开发包，需包含 `include/`、`lib/`、`bin/`。
- `third_party/openssl`：OpenSSL 1.1 运行时，需包含 `bin/libssl-1_1-x64.dll` 和 `bin/libcrypto-1_1-x64.dll`。
- `third_party/whisper.cpp`：whisper.cpp 源码目录。
- `models/whisper`：whisper.cpp 使用的 `.bin` 模型文件，例如 `ggml-small.bin`。

## whisper 模型

EchoTrans 使用 whisper.cpp 的 ggml `.bin` 模型进行本地转录。模型可以通过两种方式准备：

1. 将模型文件直接放入项目根目录下的 `models/whisper/`。
2. 启动程序后，在“源字幕设置”中点击“导入模型”，选择本地 `.bin` 文件。程序会将该模型复制到 `models/whisper/` 并自动选中。

whisper.cpp 官方 ggml 模型下载源：

```text
https://huggingface.co/ggerganov/whisper.cpp/tree/main
```

常用模型文件示例：

```text
ggml-tiny.bin
ggml-tiny.en.bin
ggml-base.bin
ggml-base.en.bin
ggml-small.bin
ggml-medium.bin
```

模型越大，通常识别效果越好，但转录速度和内存占用也会更高。资源包提供几个比较轻量的模型：

```text
ggml-base.bin
ggml-base.en.bin 
ggml-small.bin
ggml-tiny.bin
ggml-tiny.en.bin

// en代表针对英文做优化
```

## Qt 开发环境

项目使用 Qt Widgets 开发，推荐使用以下 Qt 版本和构建 Kit：

```text
Qt 版本：Qt 5.12.12
构建 Kit：msvc2017_64
编译器：Visual Studio MSVC x64
```

Windows 上的 Qt 路径示例：

```text
D:\SoftWare\Qt\Qt5.12.12\5.12.12\msvc2017_64
```

该目录下应包含：

```text
bin/
include/
lib/
lib/cmake/Qt5/
```

Qt 官方归档下载入口：

```text
https://download.qt.io/archive/qt/5.12/5.12.12/
```

也可以使用 Qt 在线安装器，在安装组件时选择：

```text
Qt 5.12.12 -> MSVC 2017 64-bit
```

## 百度翻译 API

翻译功能使用用户自己的百度翻译开放平台账号。选择“预处理字幕”或“实时字幕”后，打开“使用翻译”开关，界面会显示“翻译设置”。在其中填写：

```text
APP ID
密钥
```

点击“保存翻译设置”后即可开始带翻译的字幕任务。未开启“使用翻译”时，程序只生成原文转录字幕，不会调用百度翻译 API。

如果百度返回 `54003 访问频率受限`，程序会等待 3 秒后自动重试一次。程序会将 8 条字幕合并为一批请求，并按百度普通版 `10 QPS` 控制请求节奏：相邻请求的发送间隔至少约 100ms，请求本身耗时超过 100ms 时不会额外等待。

## 字幕任务模式

打开媒体文件后，可以选择不同的字幕任务：

- `直接播放`：只播放本地媒体文件，不显示模型设置和翻译设置。
- `预处理字幕`：先使用 whisper.cpp 生成字幕，再开始播放。关闭“使用翻译”时输出原文转录字幕；开启后会先翻译为中文字幕。该模式时间轴更稳定，适合正式演示和需要较高准确度的场景。
- `实时字幕`：先开始播放，同时在后台进行实时转录。关闭“使用翻译”时显示实时转录原文；开启后显示实时译文并持续修正。该模式响应更快，但准确度、翻译稳定性和时间对齐效果可能不如预处理字幕。

实时字幕任务对延迟更敏感，推荐优先使用 `ggml-tiny.bin` 或 `ggml-tiny.en.bin`，尤其是英文视频可以优先选择 `tiny.en`。`base`、`small` 等模型更适合预处理字幕任务，实时体验可能出现明显延迟。

## 从源码构建

### 1. 拉取代码

```powershell
git clone https://github.com/htx261/EchoTrans.git
cd EchoTrans
```

### 2. 准备资源目录

下载资源包并解压到项目根目录，或手动准备：

```text
third_party/
  ffmpeg/
  openssl/
  whisper.cpp/

models/
  whisper/
```

### 3. 配置 CMake

将 `<Qt-MSVC-Prefix-Path>` 替换为 Qt 5.12.12 MSVC 2017 64-bit Kit 的安装路径，也就是包含 `lib/cmake/Qt5` 的目录。

路径示例：

```text
D:\SoftWare\Qt\Qt5.12.12\5.12.12\msvc2017_64
```

配置命令：

```powershell
cmake -S . -B build-qt5 -DCMAKE_PREFIX_PATH=<Qt-MSVC-Prefix-Path>
```

例如：

```powershell
cmake -S . -B build-qt5 -DCMAKE_PREFIX_PATH=D:\SoftWare\Qt\Qt5.12.12\5.12.12\msvc2017_64
```

### 4. 构建 Debug

Debug 版本适合开发和调试：

```powershell
cmake --build build-qt5 --config Debug --target EchoTrans
```

运行：

```powershell
start .\build-qt5\Debug\EchoTrans.exe
```

### 5. 构建 Release

Release 版本适合实际演示和转录任务：

```powershell
cmake --build build-qt5 --config Release --target EchoTrans
```

运行：

```powershell
start .\build-qt5\Release\EchoTrans.exe
```

## Debug 与 Release 的区别

whisper.cpp 和 ggml 在 Debug 构建下没有 Release 级别的编译优化，转录速度会明显变慢，尤其是较长音视频文件。Debug 版本主要用于调试 UI、播放流程和单元测试。

实际体验、演示和字幕生成建议使用 Release 版本：

```powershell
cmake --build build-qt5 --config Release --target EchoTrans
start .\build-qt5\Release\EchoTrans.exe
```

## HTTPS 和 OpenSSL

百度翻译 API 使用 HTTPS。Qt 5.12 在 Windows 下需要 OpenSSL 1.1 运行时 DLL：

```text
libssl-1_1-x64.dll
libcrypto-1_1-x64.dll
```

CMake 会优先从以下位置查找并复制到程序目录：

```text
third_party/openssl/bin
QtCreator/bin
```

资源包应提供 `third_party/openssl/bin`。如果运行时出现 `TLS initialization failed`，请确认上述两个 DLL 与 `EchoTrans.exe` 位于同一目录。

## 第三方许可

本项目使用 FFmpeg、whisper.cpp、Qt 和百度翻译开放平台 API。第三方库、模型和在线服务需遵守各自许可证、服务条款和计费规则。
