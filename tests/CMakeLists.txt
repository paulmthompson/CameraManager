



#[[
First we can test an in-source build, meaning that CameraManager source code will be included in that project.
]]
add_subdirectory(in-source-build)
add_subdirectory(contract)

if (BUILD_STRESS_TESTS)
    add_subdirectory(stress)
endif()
