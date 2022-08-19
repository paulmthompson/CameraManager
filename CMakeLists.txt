cmake_minimum_required(VERSION 3.21)
project(CameraManager
        VERSION 0.0.1
        DESCRIPTION "Interface for scientific cameras")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(CameraManager SHARED 
    API/camera.hpp 
    CameraManager/cameramanager.hpp
    C_Wrapper/CameraCWrapper.cpp
    C_Wrapper/CameraCWrapper.h)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(ffmpeg_wrapper CONFIG REQUIRED)

target_include_directories(CameraManager PUBLIC
                                    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/API>"
                                    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/CameraManager>"
                                    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/C_Wrapper>"
                                    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
                                     )

target_link_libraries(CameraManager PUBLIC ffmpeg_wrapper::ffmpeg_wrapper)
target_link_libraries(CameraManager PRIVATE nlohmann_json::nlohmann_json)

add_subdirectory(cameras/virtual)
add_subdirectory(cameras/basler)

target_link_libraries(CameraManager PUBLIC virtual)
target_link_libraries(CameraManager PUBLIC basler)

#install and create export set
# install the target and create export-set
install(TARGETS CameraManager
        EXPORT CameraManagerTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
        PUBLIC_HEADER DESTINATION include/CameraManager # include/SomeLibrary
)



include(CMakePackageConfigHelpers)

#Create config File
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/CameraManagerConfig.cmake"
  INSTALL_DESTINATION cmake
)

#install config file
install(FILES
          "${CMAKE_CURRENT_BINARY_DIR}/CameraManagerConfig.cmake"
        DESTINATION cmake
)

# generate and install export file
install(EXPORT CameraManagerTargets
        FILE CameraManagerTargets.cmake
        NAMESPACE CameraManager::
        DESTINATION cmake
)

#Generate the export targets for the build tree
export(EXPORT "CameraManagerTargets"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/CameraManagerTargets.cmake"
    NAMESPACE CameraManager::
)