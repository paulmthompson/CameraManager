add_executable(camera_contract_tests camera_contract_tests.cpp)

target_include_directories(camera_contract_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/cameras/virtual"
)

target_link_libraries(camera_contract_tests PRIVATE CameraManager::CameraManager)

set(_camera_contract_tests
    contract_virtual_counter_without_recording
    contract_viewer_record_lifecycle
    contract_flush_countdown_saves_lingering_frames
    contract_record_requires_connected_camera
    contract_enqueue_rejects_invalid_frame_size
    contract_enqueue_rejects_stopped_worker
    contract_overload_backpressure_no_loss
    contract_saved_counter_is_ui_coherent
)

foreach(_test IN LISTS _camera_contract_tests)
    add_test(NAME ${_test}
        COMMAND camera_contract_tests ${_test}
    )
    set_tests_properties(${_test} PROPERTIES
        LABELS "contract"
        SKIP_RETURN_CODE 77
    )
endforeach()

if(WIN32)
    add_custom_command(TARGET camera_contract_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:camera_contract_tests>
            $<TARGET_FILE_DIR:camera_contract_tests>
        COMMAND_EXPAND_LISTS
    )

    if(BUILD_BASLER AND DEFINED BASLER_PATH)
        get_filename_component(_pylon_root "${BASLER_PATH}/../../.." ABSOLUTE)
        set(_pylon_runtime "${_pylon_root}/Runtime/x64")
        if(EXISTS "${_pylon_runtime}")
            file(TO_NATIVE_PATH "${_pylon_runtime}" _pylon_runtime_native)
            foreach(_test IN LISTS _camera_contract_tests)
                set_property(TEST ${_test} APPEND PROPERTY
                    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_pylon_runtime_native}"
                )
            endforeach()
        endif()
    endif()
endif()
