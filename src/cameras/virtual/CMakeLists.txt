cmake_minimum_required(VERSION 3.21)
project(virtual
        VERSION 0.0.1
        DESCRIPTION "Virtual Camera for CameraViewer")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(virtual SHARED virtual_camera.h virtual_camera.cpp)

target_link_libraries(virtual PRIVATE camera)

target_include_directories(virtual
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../API>)

    #install and create export set
# install the target and create export-set
install(TARGETS virtual
        EXPORT virtualTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)

include(CMakePackageConfigHelpers)

#Create config File
configure_package_config_file(Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/virtualConfig.cmake"
  INSTALL_DESTINATION cmake
)

#install config file
install(FILES
          "${CMAKE_CURRENT_BINARY_DIR}/virtualConfig.cmake"
        DESTINATION cmake
)

# generate and install export file
install(EXPORT virtualTargets
        FILE virtualTargets.cmake
        NAMESPACE virtual::
        DESTINATION cmake
)

#Generate the export targets for the build tree
export(EXPORT "virtualTargets"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/virtualTargets.cmake"
    NAMESPACE virtual::
)