#=============================
# Preamble
#=============================
cmake_minimum_required(VERSION 3.25)
project(src/CameraManager
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

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(set_rpath)

#[[
Manifest mode from VCPKG will make all dependancies specified in vcpkg.json be associated and installed
with this particular project
]]
set(VCPKG_MANIFEST_MODE ON)

#[[
In general, it is not good practice to export all symbols from a shared library
Specific class and functions can be exported using the __declspec(dllexport) keywords in the header file
]]
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

#=============================
# Externally Provided Content
#=============================

add_subdirectory(dependencies)

#=============================
# Main Build Targets
#=============================

add_subdirectory(src)

#=============================
# Testing
#=============================

#[[
BUILD_TESTING is a cache variable that defaults to true which was defined by the CTest module
If we wish to run testing, we should add the tests subdirectory.
]]
if (BUILD_TESTING)
  add_subdirectory(tests)
endif()

#=============================
# Packaging
#=============================

add_subdirectory(packaging)