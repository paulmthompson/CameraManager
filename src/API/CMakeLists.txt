cmake_minimum_required(VERSION 3.21)
project(camera
        VERSION 0.0.1
        DESCRIPTION "General Camera class")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
        
#This is necessary to work on windows. A better way would be to define only the symbols that need to be exported
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(camera SHARED
            camera.hpp
            camera.cpp
            )

target_link_libraries(camera PUBLIC ffmpeg_wrapper::ffmpeg_wrapper)

target_include_directories(camera PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

install(TARGETS camera
        EXPORT cameraTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)

include(CMakePackageConfigHelpers)

#Create config File
configure_package_config_file(Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/cameraConfig.cmake"
  INSTALL_DESTINATION cmake
)

#install config file
install(FILES
          "${CMAKE_CURRENT_BINARY_DIR}/cameraConfig.cmake"
        DESTINATION cmake
)

# generate and install export file
install(EXPORT cameraTargets
        FILE cameraTargets.cmake
        NAMESPACE camera::
        DESTINATION cmake
)

#Generate the export targets for the build tree
export(EXPORT "cameraTargets"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/cameraTargets.cmake"
    NAMESPACE camera::
)