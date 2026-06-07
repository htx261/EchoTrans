set(ECHOTRANS_FFMPEG_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg"
    CACHE PATH "Path to the FFmpeg development package")

set(ECHOTRANS_WHISPER_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/whisper.cpp"
    CACHE PATH "Path to whisper.cpp source tree")

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

set(ECHOTRANS_FFMPEG_INCLUDE_DIR "${ECHOTRANS_FFMPEG_ROOT}/include")
set(ECHOTRANS_FFMPEG_LIB_DIR "${ECHOTRANS_FFMPEG_ROOT}/lib")
set(ECHOTRANS_FFMPEG_BIN_DIR "${ECHOTRANS_FFMPEG_ROOT}/bin")

set(ECHOTRANS_WHISPER_INCLUDE_DIR "${ECHOTRANS_WHISPER_ROOT}/include")

foreach(required_path
    "${ECHOTRANS_FFMPEG_INCLUDE_DIR}"
    "${ECHOTRANS_FFMPEG_LIB_DIR}"
    "${ECHOTRANS_WHISPER_INCLUDE_DIR}")
  if(NOT EXISTS "${required_path}")
    message(FATAL_ERROR "Required dependency path missing: ${required_path}")
  endif()
endforeach()
