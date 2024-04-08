
include(FetchContent)

FetchContent_Declare(
  ffmpeg_wrapper
  GIT_REPOSITORY https://github.com/paulmthompson/ffmpeg_wrapper.git
  GIT_TAG a3be2f58a57c317112055905024292295a3bffc0
)

add_subdirectory(ffmpeg_wrapper)
