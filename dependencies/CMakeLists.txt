
include(FetchContent)

FetchContent_Declare(
  ffmpeg_wrapper
  GIT_REPOSITORY https://github.com/paulmthompson/ffmpeg_wrapper.git
  GIT_TAG 53c26e0da33cea52e346341b1bb70e76b6126a93
)

add_subdirectory(ffmpeg_wrapper)
