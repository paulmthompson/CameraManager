cmake_minimum_required(VERSION 3.21)
project(basler
        VERSION 0.0.1
        DESCRIPTION "Basler Camera for CameraViewer")

set(CMAKE_CXX_STANDARD 17) #Bug with GCC in basler library that is not compatible with c++20. Not using c++
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT PYLON_VERSION)
    set(PYLON_VERSION 5)
endif()

add_library(basler SHARED basler_camera.h
                        basler_camera.cpp)
IF (UNIX)
    if (PYLON_VERSION EQUAL 5)
        message(STATUS "Finding libraries for Pylon version 5")
        find_library(PYLON_BASE_LIBRARY pylonbase HINTS ${BASLER_PATH})
        find_library(PYLON_GENAPI_LIBRARY GenApi_gcc_v3_1_Basler_pylon HINTS ${BASLER_PATH})
        find_library(PYLON_GCBASE_LIBRARY GCBase_gcc_v3_1_Basler_pylon HINTS ${BASLER_PATH})
        find_library(PYLON_UTILITY_LIBRARY pylonutility HINTS ${BASLER_PATH})
        set_target_properties(basler PROPERTIES 
                                INSTALL_RPATH /opt/pylon5/lib64)
        set (BASLER_LINK_PATH /opt/pylon5/lib64 PARENT_SCOPE) 
    ELSEIF (PYLON_VERSION EQUAL 6)
        message(STATUS "Pylon version 6 not supported")
    ELSEIF (PYLON_VERSION EQUAL 7)
        message(STATUS "Finding libraries for Pylon version 7")

        find_library(PYLON_BASE_LIBRARY pylonbase HINTS ${BASLER_PATH})
        find_library(PYLON_GENAPI_LIBRARY GenApi_gcc_v3_1_Basler_pylon HINTS ${BASLER_PATH})
        find_library(PYLON_GCBASE_LIBRARY GCBase_gcc_v3_1_Basler_pylon HINTS ${BASLER_PATH})
        find_library(PYLON_UTILITY_LIBRARY pylonutility HINTS ${BASLER_PATH})
        set_target_properties(basler PROPERTIES 
                                INSTALL_RPATH /opt/pylon/lib)
        set (BASLER_LINK_PATH /opt/pylon/lib PARENT_SCOPE) 

    ELSE()
        message(STATUS "Pylon version not supported")
    endif()
    
ELSEIF (WIN32)
    if (PYLON_VERSION EQUAL 6)
        find_library(PYLON_BASE_LIBRARY PylonBase_v6_1 HINTS ${BASLER_PATH} REQUIRED)
        find_library(PYLON_GENAPI_LIBRARY GenApi_MD_VC141_v3_1_Basler_pylon HINTS ${BASLER_PATH} REQUIRED)
        find_library(PYLON_GCBASE_LIBRARY GCBase_MD_VC141_v3_1_Basler_pylon HINTS ${BASLER_PATH} REQUIRED)
        find_library(PYLON_UTILITY_LIBRARY PylonUtility_v6_1 HINTS ${BASLER_PATH} REQUIRED)
        set (BASLER_LINK_PATH "C:/Program Files/Basler/pylon 6/Development/lib/x64" PARENT_SCOPE) 
    endif()
endif()

target_link_libraries(basler PRIVATE camera)

target_link_libraries(basler PUBLIC
     ${PYLON_BASE_LIBRARY}
     ${PYLON_GENAPI_LIBRARY}
     ${PYLON_GCBASE_LIBRARY}
     ${PYLON_UTILITY_LIBRARY}
)

target_include_directories(basler
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    ${BASLER_INCLUDE_PATH}
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../API>)

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