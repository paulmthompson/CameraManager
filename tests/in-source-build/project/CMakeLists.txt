cmake_minimum_required(VERSION 3.24)

project(InSourceBuild)

#[[
Manifest mode from VCPKG will make all dependancies specified in vcpkg.json be associated and installed
with this particular project
]]
set(VCPKG_MANIFEST_MODE ON)

#[[
We are going to add CameraManager as a subdirectory and link to an executable that uses it

]]
set(BUILD_TESTING OFF CACHE INTERNAL "")
add_subdirectory(${cameramanager_source}
    ${CMAKE_CURRENT_BINARY_DIR}/cameramanager)

add_executable(InSourceBuild  EXCLUDE_FROM_ALL main.cpp)  

target_link_libraries(InSourceBuild PRIVATE CameraManager::CameraManager)

