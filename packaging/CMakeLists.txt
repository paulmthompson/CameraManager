

#install and create export set
# install the target and create export-set
install(TARGETS CameraManager
        EXPORT CameraManagerTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CameraManager # include/SomeLibrary
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
