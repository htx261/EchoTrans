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

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Concurrent Test)

set(ECHOTRANS_FFMPEG_LIBRARIES
  "${ECHOTRANS_FFMPEG_LIB_DIR}/avformat.lib"
  "${ECHOTRANS_FFMPEG_LIB_DIR}/avcodec.lib"
  "${ECHOTRANS_FFMPEG_LIB_DIR}/avutil.lib"
)

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

target_link_libraries(EchoTransCore
  PUBLIC
    Qt5::Core
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
)

target_link_libraries(EchoTransPlayer
  PUBLIC
    Qt5::Core
)

function(echotrans_copy_ffmpeg_runtime target_name)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ECHOTRANS_FFMPEG_BIN_DIR}"
        "$<TARGET_FILE_DIR:${target_name}>"
  )
endfunction()

add_executable(EchoTrans
  src/app/main.cpp
  src/ui/MainWindow.cpp
)

target_link_libraries(EchoTrans
  PRIVATE
    EchoTransCore
    EchoTransMedia
    Qt5::Concurrent
    Qt5::Widgets
)

echotrans_copy_ffmpeg_runtime(EchoTrans)

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

enable_testing()
add_test(NAME DependencyReportTests COMMAND DependencyReportTests)
add_test(NAME FFmpegMediaProbeTests COMMAND FFmpegMediaProbeTests)
add_test(NAME BlockingQueueTests COMMAND BlockingQueueTests)
add_test(NAME MediaPlayerCoreTests COMMAND MediaPlayerCoreTests)
