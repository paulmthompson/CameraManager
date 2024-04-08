#=============================
# Preamble
#=============================
cmake_minimum_required(VERSION 3.25)
project(CameraManager
        VERSION 0.0.1
        DESCRIPTION "Interface for scientific cameras")
#[[
The CTest module helps with several testing functions.
It defines the BUILD_TESTING cache variable that defaults to true, and also automatically calls enable_testing(), so this does not need to be called manually
CTest should be included early in the top level CMakeLists.txt file
Professional CMake 25.10.3
]]
include(CTest)

#=============================
# Project Wide Setup
#=============================

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
#Cannot use 20 with GCC on linux (all versions of pylon? does this matter for manager or just basler due to bug in basler library. Clang works okay.
set(CMAKE_CXX_STANDARD 20) 
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#[[
Best practice for rpath from Professional Cmake regarding rpath (26.2.2)
]]
if(APPLE)
        set(base @loader_path)
else()
        set(base $ORIGIN)
endif()
include(GNUInstallDirs)
file(RELATIVE_PATH relDir
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
)
set(CMAKE_INSTALL_RPATH ${base} ${base}/${relDir})

#[[
Manifest mode from VCPKG will make all dependancies specified in vcpkg.json be associated and installed
with this particular project
]]
set(VCPKG_MANIFEST_MODE ON)



#[[
We will get external package ffmpeg_wrapper using fetchcontent
]]
add_subdirectory(external)

#[[
We will get external package nlohmann_json using find_package
]]
find_package(nlohmann_json CONFIG REQUIRED)

#[[
Our Camera API is a shared library that will provide the base class for all concrete camera implementations
]]
add_subdirectory(API)

add_subdirectory(cameras/virtual)

set(BUILD_BASLER ON)
if (BUILD_BASLER)
        add_subdirectory(cameras/basler)
        list(APPEND CMAKE_INSTALL_RPATH ${BASLER_LINK_PATH})
endif()

add_library(CameraManager SHARED 
    CameraManager/cameramanager.hpp
    CameraManager/cameramanager.cpp
    C_Wrapper/CameraCWrapper.cpp
    C_Wrapper/CameraCWrapper.h)
add_library(CameraManager::CameraManager ALIAS CameraManager)

target_include_directories(CameraManager PUBLIC
                                    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/API>"
                                    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/CameraManager>"
                                    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/C_Wrapper>"
                                    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
                                     )

target_link_libraries(CameraManager PRIVATE nlohmann_json::nlohmann_json)

target_link_libraries(CameraManager PUBLIC camera)

target_link_libraries(CameraManager PUBLIC virtual)

if (BUILD_BASLER)
        target_link_libraries(CameraManager PUBLIC basler)
endif()

#[[
In general, it is not good practice to export all symbols from a shared library
Specific class and functions can be exported using the __declspec(dllexport) keywords in the header file
]]
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)


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

#[[
BUILD_TESTING is a cache variable that defaults to true which was defined by the CTest module
If we wish to run testing, we should add the tests subdirectory.
]]
if (BUILD_TESTING)
  add_subdirectory(tests)
endif()

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