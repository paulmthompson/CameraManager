

add_test(NAME camera_manager_add_subdirectory_configure
  COMMAND ${CMAKE_COMMAND}
    -G "${CMAKE_GENERATOR}"
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -Dcameramanager_source=${PROJECT_SOURCE_DIR}
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}
        -DBASLER_PATH=${BASLER_PATH}
        -DBASLER_INCLUDE_PATH=${BASLER_INCLUDE_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/project
)

add_test(NAME camera_manager_add_subdirectory_build
  COMMAND ${CMAKE_COMMAND} --build . --target InSourceBuild
)
