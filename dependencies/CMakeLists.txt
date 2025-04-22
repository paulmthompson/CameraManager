
include(FetchContent)

FetchContent_Declare(
  ffmpeg_wrapper
  GIT_REPOSITORY https://github.com/paulmthompson/ffmpeg_wrapper.git
  GIT_TAG 694f346e5eeaca6c464bb88aa30f43b306c63047
)

add_subdirectory(ffmpeg_wrapper)
