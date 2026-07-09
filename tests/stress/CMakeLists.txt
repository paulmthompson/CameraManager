function(add_stress_test_executable target_name)
    add_executable(${target_name} ${ARGN})
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_SOURCE_DIR}/src/cameras/virtual"
        "${CMAKE_SOURCE_DIR}/src/cameras/basler"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(${target_name} PRIVATE CameraManager::CameraManager)

    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target_name}>
                $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

add_stress_test_executable(save_stress_test save_stress_test.cpp)
add_stress_test_executable(pylon_sim_stress_test
    pylon_sim_stress_test.cpp
    pylon_sim_camera.cpp
)
add_stress_test_executable(basler_stress_test basler_stress_test.cpp)

set(_stress_tests stress_save_light stress_save_heavy stress_save_multi)
set(_pylon_sim_tests stress_pylon_sim_light stress_pylon_sim_spike)
set(_basler_tests stress_basler_smoke)

add_test(NAME stress_save_light
    COMMAND save_stress_test --sim-fps 100 --duration-s 3
)
add_test(NAME stress_save_heavy
    COMMAND save_stress_test --sim-fps 500 --width 1920 --height 1080 --duration-s 10 --max-loop-overrun-ms 500
)
add_test(NAME stress_save_multi
    COMMAND save_stress_test --sim-fps 500 --duration-s 5 --num-cameras 2 --output ./stress_test_multi.mp4 --max-loop-overrun-ms 500
)
add_test(NAME stress_pylon_sim_light
    COMMAND pylon_sim_stress_test --cam-fps 500 --duration-s 5 --max-loop-overrun-ms 500
)
add_test(NAME stress_pylon_sim_spike
    COMMAND pylon_sim_stress_test --cam-fps 500 --duration-s 10 --inject-spike-probability 0.01 --inject-spike-ms 41 --max-loop-overrun-ms 500
)
add_test(NAME stress_basler_smoke
    COMMAND basler_stress_test --serial 23484120 --duration-s 5 --max-loop-overrun-ms 500
)

set(_all_stress_tests ${_stress_tests} ${_pylon_sim_tests} ${_basler_tests})

foreach(_test IN LISTS _all_stress_tests)
    set_tests_properties(${_test} PROPERTIES
        LABELS "stress;gpu"
        SKIP_RETURN_CODE 77
    )
endforeach()

foreach(_test IN LISTS _basler_tests)
    set_property(TEST ${_test} APPEND PROPERTY LABELS "basler")
endforeach()

if(WIN32)
    if(BUILD_BASLER AND DEFINED BASLER_PATH)
        get_filename_component(_pylon_root "${BASLER_PATH}/../../.." ABSOLUTE)
        set(_pylon_runtime "${_pylon_root}/Runtime/x64")
        if(EXISTS "${_pylon_runtime}")
            file(TO_NATIVE_PATH "${_pylon_runtime}" _pylon_runtime_native)
            foreach(_test IN LISTS _all_stress_tests)
                set_property(TEST ${_test} APPEND PROPERTY
                    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_pylon_runtime_native}"
                )
            endforeach()
        else()
            message(WARNING "Pylon runtime not found at ${_pylon_runtime}; stress tests may fail to load basler.dll")
        endif()
    endif()
endif()
