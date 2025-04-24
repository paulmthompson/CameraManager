
include(FetchContent)

FetchContent_Declare(
  ffmpeg_wrapper
  GIT_REPOSITORY https://github.com/paulmthompson/ffmpeg_wrapper.git
  GIT_TAG 0f79a451d337c4ea67baf82bd336d1216d0841b8
)

add_subdirectory(ffmpeg_wrapper)
