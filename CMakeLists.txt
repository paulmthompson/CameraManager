cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(camera SHARED API/camera.hpp Loader/loader.hpp)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(ffmpeg_wrapper CONFIG REQUIRED)

target_include_directories(camera PUBLIC
                                    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/API>"
                                    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
                                     )

target_link_libraries(camera PUBLIC ffmpeg_wrapper::ffmpeg_wrapper)
target_link_libraries(camera PRIVATE nlohmann_json::nlohmann_json)

add_subdirectory(cameras/virtual)
add_subdirectory(cameras/basler)

target_link_libraries(camera PUBLIC virtual)
target_link_libraries(camera PUBLIC basler)

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")