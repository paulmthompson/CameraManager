add_executable(save_stress_test save_stress_test.cpp)

target_include_directories(save_stress_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/cameras/virtual"
)

target_link_libraries(save_stress_test PRIVATE CameraManager::CameraManager)

set(_stress_tests stress_save_light stress_save_heavy stress_save_multi)

add_test(NAME stress_save_light
    COMMAND save_stress_test --sim-fps 100 --duration-s 3
)
add_test(NAME stress_save_heavy
    COMMAND save_stress_test --sim-fps 500 --width 1920 --height 1080 --duration-s 10 --max-loop-overrun-ms 500
)
add_test(NAME stress_save_multi
    COMMAND save_stress_test --sim-fps 500 --duration-s 5 --num-cameras 2 --output ./stress_test_multi.mp4 --max-loop-overrun-ms 500
)

foreach(_test IN LISTS _stress_tests)
    set_tests_properties(${_test} PROPERTIES
        LABELS "stress;gpu"
        SKIP_RETURN_CODE 77
    )
endforeach()

if(WIN32)
    # Copy linked runtime DLLs next to the test exe so ctest can find them.
    # Exit code 0xc0000135 at launch means a DLL is still missing.
    add_custom_command(TARGET save_stress_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:save_stress_test>
            $<TARGET_FILE_DIR:save_stress_test>
        COMMAND_EXPAND_LISTS
    )

    # CameraManager links basler.dll when BUILD_BASLER=ON; Pylon runtime DLLs
    # live in the SDK install and are not part of TARGET_RUNTIME_DLLS.
    if(BUILD_BASLER AND DEFINED BASLER_PATH)
        get_filename_component(_pylon_root "${BASLER_PATH}/../../.." ABSOLUTE)
        set(_pylon_runtime "${_pylon_root}/Runtime/x64")
        if(EXISTS "${_pylon_runtime}")
            file(TO_NATIVE_PATH "${_pylon_runtime}" _pylon_runtime_native)
            foreach(_test IN LISTS _stress_tests)
                set_property(TEST ${_test} APPEND PROPERTY
                    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_pylon_runtime_native}"
                )
            endforeach()
        else()
            message(WARNING "Pylon runtime not found at ${_pylon_runtime}; stress tests may fail to load basler.dll")
        endif()
    endif()
endif()
