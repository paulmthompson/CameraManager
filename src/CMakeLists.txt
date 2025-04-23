
#[[
We will get external package nlohmann_json using find_package
]]
find_package(nlohmann_json CONFIG REQUIRED)

#[[
Our Camera API is a shared library that will provide the base class for all concrete camera implementations
]]
add_subdirectory(API)

add_subdirectory(cameras/virtual)

option(BUILD_BASLER "Build with Basler camera support" OFF)
if (BUILD_BASLER)
    add_subdirectory(cameras/basler)
    list(APPEND CMAKE_INSTALL_RPATH ${BASLER_LINK_PATH})
    target_compile_definitions(CameraManager PRIVATE BUILD_WITH_BASLER)
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
