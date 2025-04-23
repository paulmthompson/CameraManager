
include(FetchContent)

FetchContent_Declare(
  ffmpeg_wrapper
  GIT_REPOSITORY https://github.com/paulmthompson/ffmpeg_wrapper.git
  GIT_TAG 4b26e7d1fec4e833a9455b7b656c2c3c971426bd
)

add_subdirectory(ffmpeg_wrapper)
