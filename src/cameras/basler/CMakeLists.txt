cmake_minimum_required(VERSION 3.21)
project(basler
        VERSION 0.0.1
        DESCRIPTION "Basler Camera for CameraViewer")

set(CMAKE_CXX_STANDARD 17) #Bug with GCC in basler library that is not compatible with c++20. Not using c++
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_basler_pylon()

add_library(basler SHARED basler_camera.h
                        basler_camera.cpp)

target_link_libraries(basler PRIVATE camera)

target_link_libraries(basler PUBLIC ${BaslerPylon_LIBRARIES})

target_include_directories(basler
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    ${BaslerPylon_INCLUDE_DIR}
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../API>)

if(BaslerPylon_LINK_DIR)
    set_target_properties(basler PROPERTIES INSTALL_RPATH "${BaslerPylon_LINK_DIR}")
    set(BASLER_LINK_PATH ${BaslerPylon_LINK_DIR} PARENT_SCOPE)
endif()

        #install and create export set
# install the target and create export-set
install(TARGETS basler
    EXPORT baslerTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

include(CMakePackageConfigHelpers)

#Create config File
configure_package_config_file(Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/baslerConfig.cmake"
    INSTALL_DESTINATION cmake
)

#install config file
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/baslerConfig.cmake"
DESTINATION cmake
)

# generate and install export file
install(EXPORT baslerTargets
FILE baslerTargets.cmake
NAMESPACE basler::
DESTINATION cmake
)

#Generate the export targets for the build tree
export(EXPORT "baslerTargets"
FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/baslerTargets.cmake"
NAMESPACE basler::
)
