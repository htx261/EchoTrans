cmake_minimum_required(VERSION 3.16)

project(EchoTrans VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(cmake/EchoTransDependencies.cmake)

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Concurrent Test Multimedia Network)

set(ECHOTRANS_OPENSSL_RUNTIME_DIR
    ""
    CACHE PATH "Directory containing OpenSSL 1.1 runtime DLLs for Qt HTTPS")

if(NOT ECHOTRANS_OPENSSL_RUNTIME_DIR)
  foreach(openssl_candidate
      "${CMAKE_SOURCE_DIR}/third_party/openssl/bin"
      "${CMAKE_PREFIX_PATH}/../../Tools/QtCreator/bin")
    if(EXISTS "${openssl_candidate}/libssl-1_1-x64.dll"
        AND EXISTS "${openssl_candidate}/libcrypto-1_1-x64.dll")
      set(ECHOTRANS_OPENSSL_RUNTIME_DIR "${openssl_candidate}" CACHE PATH
          "Directory containing OpenSSL 1.1 runtime DLLs for Qt HTTPS" FORCE)
      break()
    endif()
  endforeach()
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "Build whisper.cpp tests" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "Build whisper.cpp examples" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "Build whisper.cpp server" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "Use curl in whisper.cpp" FORCE)
set(WHISPER_SDL2 OFF CACHE BOOL "Use SDL2 in whisper.cpp" FORCE)
add_subdirectory("${ECHOTRANS_WHISPER_ROOT}" "${CMAKE_BINARY_DIR}/whisper.cpp" EXCLUDE_FROM_ALL)

set(ECHOTRANS_FFMPEG_LIBRARIES
  "${ECHOTRANS_FFMPEG_LIB_DIR}/avformat.lib"
  "${ECHOTRANS_FFMPEG_LIB_DIR}/avcodec.lib"
  "${ECHOTRANS_FFMPEG_LIB_DIR}/avutil.lib"
  "${ECHOTRANS_FFMPEG_LIB_DIR}/swresample.lib"
  "${ECHOTRANS_FFMPEG_LIB_DIR}/swscale.lib"
)

add_library(EchoTransCore
  src/core/DependencyReport.cpp
  src/live/LiveSubtitleInterpreter.cpp
  src/subtitle/SubtitleTrack.cpp
  src/transcription/MediaSubtitlePreparer.cpp
  src/transcription/TranscriptionAudioPreprocessor.cpp
  src/transcription/WhisperTranscriber.cpp
  src/translation/BaiduTranslator.cpp
)

target_include_directories(EchoTransCore
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  PRIVATE
    ${ECHOTRANS_FFMPEG_INCLUDE_DIR}
)

target_compile_definitions(EchoTransCore
  PUBLIC
    ECHOTRANS_FFMPEG_ROOT="${ECHOTRANS_FFMPEG_ROOT}"
    ECHOTRANS_WHISPER_ROOT="${ECHOTRANS_WHISPER_ROOT}"
    ECHOTRANS_MODELS_ROOT="${ECHOTRANS_MODELS_ROOT}"
)

target_link_libraries(EchoTransCore
  PUBLIC
    Qt5::Core
    Qt5::Network
  PRIVATE
    ${ECHOTRANS_FFMPEG_LIBRARIES}
    whisper
)

add_library(EchoTransMedia
  src/media/FFmpegMediaProbe.cpp
)

target_include_directories(EchoTransMedia
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ECHOTRANS_FFMPEG_INCLUDE_DIR}
)

target_link_libraries(EchoTransMedia
  PUBLIC
    Qt5::Core
  PRIVATE
    ${ECHOTRANS_FFMPEG_LIBRARIES}
)

add_library(EchoTransPlayer
  src/player/MediaPlayerCore.cpp
)

target_include_directories(EchoTransPlayer
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  PRIVATE
    ${ECHOTRANS_FFMPEG_INCLUDE_DIR}
)

target_link_libraries(EchoTransPlayer
  PUBLIC
    Qt5::Core
    Qt5::Multimedia
  PRIVATE
    ${ECHOTRANS_FFMPEG_LIBRARIES}
)

function(echotrans_copy_ffmpeg_runtime target_name)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ECHOTRANS_FFMPEG_BIN_DIR}"
        "$<TARGET_FILE_DIR:${target_name}>"
  )
endfunction()

function(echotrans_copy_openssl_runtime target_name)
  if(ECHOTRANS_OPENSSL_RUNTIME_DIR
      AND EXISTS "${ECHOTRANS_OPENSSL_RUNTIME_DIR}/libssl-1_1-x64.dll"
      AND EXISTS "${ECHOTRANS_OPENSSL_RUNTIME_DIR}/libcrypto-1_1-x64.dll")
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${ECHOTRANS_OPENSSL_RUNTIME_DIR}/libssl-1_1-x64.dll"
          "$<TARGET_FILE_DIR:${target_name}>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${ECHOTRANS_OPENSSL_RUNTIME_DIR}/libcrypto-1_1-x64.dll"
          "$<TARGET_FILE_DIR:${target_name}>"
    )
  else()
    message(WARNING "OpenSSL 1.1 runtime DLLs were not found. HTTPS translation requests may fail.")
  endif()
endfunction()

add_executable(EchoTrans
  src/app/main.cpp
  src/ui/MainWindow.cpp
)

target_link_libraries(EchoTrans
  PRIVATE
    EchoTransCore
    EchoTransMedia
    EchoTransPlayer
    Qt5::Concurrent
    Qt5::Widgets
)

echotrans_copy_ffmpeg_runtime(EchoTrans)
echotrans_copy_openssl_runtime(EchoTrans)

add_executable(DependencyReportTests
  tests/core/DependencyReportTests.cpp
)

target_link_libraries(DependencyReportTests
  PRIVATE
    EchoTransCore
    Qt5::Test
)

add_executable(FFmpegMediaProbeTests
  tests/media/FFmpegMediaProbeTests.cpp
)

target_include_directories(FFmpegMediaProbeTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(FFmpegMediaProbeTests
  PRIVATE
    EchoTransMedia
    Qt5::Test
)

echotrans_copy_ffmpeg_runtime(FFmpegMediaProbeTests)

add_executable(BlockingQueueTests
  tests/player/BlockingQueueTests.cpp
)

target_include_directories(BlockingQueueTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BlockingQueueTests
  PRIVATE
    Qt5::Test
)

add_executable(SubtitleTrackTests
  tests/subtitle/SubtitleTrackTests.cpp
)

target_include_directories(SubtitleTrackTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(SubtitleTrackTests
  PRIVATE
    EchoTransCore
    Qt5::Test
)

add_executable(WhisperTranscriberTests
  tests/transcription/WhisperTranscriberTests.cpp
)

target_include_directories(WhisperTranscriberTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(WhisperTranscriberTests
  PRIVATE
    EchoTransCore
    Qt5::Test
)

add_executable(TranscriptionAudioPreprocessorTests
  tests/transcription/TranscriptionAudioPreprocessorTests.cpp
)

target_include_directories(TranscriptionAudioPreprocessorTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(TranscriptionAudioPreprocessorTests
  PRIVATE
    EchoTransCore
    Qt5::Test
)

add_executable(MediaSubtitlePreparerTests
  tests/transcription/MediaSubtitlePreparerTests.cpp
)

target_include_directories(MediaSubtitlePreparerTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(MediaSubtitlePreparerTests
  PRIVATE
    EchoTransCore
    Qt5::Test
)

add_executable(MediaPlayerCoreTests
  tests/player/MediaPlayerCoreTests.cpp
)

target_include_directories(MediaPlayerCoreTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(MediaPlayerCoreTests
  PRIVATE
    EchoTransPlayer
    Qt5::Test
)

echotrans_copy_ffmpeg_runtime(MediaPlayerCoreTests)

add_executable(BaiduTranslatorTests
  tests/translation/BaiduTranslatorTests.cpp
)

target_include_directories(BaiduTranslatorTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BaiduTranslatorTests
  PRIVATE
    EchoTransCore
    Qt5::Test
)

add_executable(LiveSubtitleInterpreterTests
  tests/live/LiveSubtitleInterpreterTests.cpp
)

target_include_directories(LiveSubtitleInterpreterTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(LiveSubtitleInterpreterTests
  PRIVATE
    EchoTransCore
    Qt5::Test
)

add_executable(VideoFrameSynchronizerTests
  tests/player/VideoFrameSynchronizerTests.cpp
)

target_include_directories(VideoFrameSynchronizerTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(VideoFrameSynchronizerTests
  PRIVATE
    EchoTransPlayer
    Qt5::Test
)

add_executable(MainWindowTests
  tests/ui/MainWindowTests.cpp
  src/ui/MainWindow.cpp
)

target_include_directories(MainWindowTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(MainWindowTests
  PRIVATE
    ECHOTRANS_TESTING
)

target_link_libraries(MainWindowTests
  PRIVATE
    EchoTransCore
    EchoTransMedia
    EchoTransPlayer
    Qt5::Concurrent
    Qt5::Network
    Qt5::Test
    Qt5::Widgets
)

echotrans_copy_ffmpeg_runtime(MainWindowTests)

enable_testing()
add_test(NAME DependencyReportTests COMMAND DependencyReportTests)
add_test(NAME FFmpegMediaProbeTests COMMAND FFmpegMediaProbeTests)
add_test(NAME BlockingQueueTests COMMAND BlockingQueueTests)
add_test(NAME SubtitleTrackTests COMMAND SubtitleTrackTests)
add_test(NAME WhisperTranscriberTests COMMAND WhisperTranscriberTests)
add_test(NAME TranscriptionAudioPreprocessorTests COMMAND TranscriptionAudioPreprocessorTests)
add_test(NAME MediaSubtitlePreparerTests COMMAND MediaSubtitlePreparerTests)
add_test(NAME BaiduTranslatorTests COMMAND BaiduTranslatorTests)
add_test(NAME LiveSubtitleInterpreterTests COMMAND LiveSubtitleInterpreterTests)
add_test(NAME MediaPlayerCoreTests COMMAND MediaPlayerCoreTests)
add_test(NAME MainWindowTests COMMAND MainWindowTests)
add_test(NAME VideoFrameSynchronizerTests COMMAND VideoFrameSynchronizerTests)
