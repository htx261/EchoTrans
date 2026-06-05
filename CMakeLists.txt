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

target_link_libraries(EchoTransCore
  PUBLIC
    Qt5::Core
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
