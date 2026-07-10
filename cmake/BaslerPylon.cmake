# BaslerPylon.cmake - centralized Basler Pylon SDK discovery for CameraManager
#
# Supported versions:
#   Windows: Pylon 6.x, 7.x (MSVC import libs under Development/lib/x64)
#   Linux:   Pylon 5.x, 7.x (shared libs under lib or lib64)
#   Future:  Pylon 8.x placeholders can be added to the candidate tables below
#
# Optional cache hints:
#   PYLON_VERSION      - preferred major version; other majors are still tried on failure
#   PYLON_DEV_DIR      - Windows Development directory override
#   PYLON_ROOT         - Linux install root override
#   BASLER_PATH        - legacy library directory override (tried first)
#   BASLER_INCLUDE_PATH - legacy include directory override (used with BASLER_PATH)
#
# Outputs (set by find_basler_pylon):
#   BaslerPylon_FOUND
#   BaslerPylon_VERSION_MAJOR
#   BaslerPylon_VERSION_MINOR
#   BaslerPylon_INCLUDE_DIR
#   BaslerPylon_LIBRARIES
#   BaslerPylon_LINK_DIR
#   BaslerPylon_RUNTIME_DIR
#   BASLER_LINK_PATH (legacy alias for BaslerPylon_LINK_DIR)

if(_BASLER_PYLON_CMAKE_INCLUDED)
    return()
endif()
set(_BASLER_PYLON_CMAKE_INCLUDED TRUE)

# ---------------------------------------------------------------------------
# Per-major library name tables (minor-version forgiveness via NAMES lists)
# ---------------------------------------------------------------------------

set(_BaslerPylon_win6_base_names
    PylonBase_v6_4 PylonBase_v6_3 PylonBase_v6_2 PylonBase_v6_1 PylonBase_v6_0
)
set(_BaslerPylon_win6_utility_names
    PylonUtility_v6_4 PylonUtility_v6_3 PylonUtility_v6_2 PylonUtility_v6_1 PylonUtility_v6_0
)
set(_BaslerPylon_win6_genapi_names
    GenApi_MD_VC141_v3_1_Basler_pylon
    GenApi_MD_VC142_v3_1_Basler_pylon
)
set(_BaslerPylon_win6_gcbase_names
    GCBase_MD_VC141_v3_1_Basler_pylon
    GCBase_MD_VC142_v3_1_Basler_pylon
)

set(_BaslerPylon_win7_base_names
    PylonBase_v7_5 PylonBase_v7_4 PylonBase_v7_3 PylonBase_v7_2 PylonBase_v7_1 PylonBase_v7_0
)
set(_BaslerPylon_win7_utility_names
    PylonUtility_v7_5 PylonUtility_v7_4 PylonUtility_v7_3 PylonUtility_v7_2 PylonUtility_v7_1 PylonUtility_v7_0
)
set(_BaslerPylon_win7_genapi_names
    GenApi_MD_VC141_v3_1_Basler_pylon
    GenApi_MD_VC142_v3_1_Basler_pylon
    GenApi_MD_VC143_v3_1_Basler_pylon
)
set(_BaslerPylon_win7_gcbase_names
    GCBase_MD_VC141_v3_1_Basler_pylon
    GCBase_MD_VC142_v3_1_Basler_pylon
    GCBase_MD_VC143_v3_1_Basler_pylon
)

set(_BaslerPylon_linux_base_names pylonbase)
set(_BaslerPylon_linux_utility_names pylonutility)
set(_BaslerPylon_linux_genapi_names GenApi_gcc_v3_1_Basler_pylon)
set(_BaslerPylon_linux_gcbase_names GCBase_gcc_v3_1_Basler_pylon)

# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

macro(_basler_pylon_get_major_lib_names major out_base out_utility out_genapi out_gcbase)
    if(WIN32)
        if(${major} EQUAL 6)
            set(${out_base} ${_BaslerPylon_win6_base_names})
            set(${out_utility} ${_BaslerPylon_win6_utility_names})
            set(${out_genapi} ${_BaslerPylon_win6_genapi_names})
            set(${out_gcbase} ${_BaslerPylon_win6_gcbase_names})
        elseif(${major} EQUAL 7)
            set(${out_base} ${_BaslerPylon_win7_base_names})
            set(${out_utility} ${_BaslerPylon_win7_utility_names})
            set(${out_genapi} ${_BaslerPylon_win7_genapi_names})
            set(${out_gcbase} ${_BaslerPylon_win7_gcbase_names})
        else()
            set(${out_base} "")
            set(${out_utility} "")
            set(${out_genapi} "")
            set(${out_gcbase} "")
        endif()
    else()
        if(${major} EQUAL 5 OR ${major} EQUAL 7)
            set(${out_base} ${_BaslerPylon_linux_base_names})
            set(${out_utility} ${_BaslerPylon_linux_utility_names})
            set(${out_genapi} ${_BaslerPylon_linux_genapi_names})
            set(${out_gcbase} ${_BaslerPylon_linux_gcbase_names})
        else()
            set(${out_base} "")
            set(${out_utility} "")
            set(${out_genapi} "")
            set(${out_gcbase} "")
        endif()
    endif()
endmacro()

function(_basler_pylon_parse_version_from_lib lib_path out_major out_minor)
    set(_major 0)
    set(_minor 0)
    if(lib_path)
        get_filename_component(_lib_name "${lib_path}" NAME_WE)
        if(_lib_name MATCHES "v([0-9]+)_([0-9]+)")
            set(_major ${CMAKE_MATCH_1})
            set(_minor ${CMAKE_MATCH_2})
        endif()
    endif()
    set(${out_major} ${_major} PARENT_SCOPE)
    set(${out_minor} ${_minor} PARENT_SCOPE)
endfunction()

function(_basler_pylon_try_find_libs link_dir major
        out_success out_base out_utility out_genapi out_gcbase)
    _basler_pylon_get_major_lib_names(${major} _base_names _utility_names _genapi_names _gcbase_names)
    if(NOT _base_names)
        set(${out_success} FALSE PARENT_SCOPE)
        set(${out_base} "" PARENT_SCOPE)
        set(${out_utility} "" PARENT_SCOPE)
        set(${out_genapi} "" PARENT_SCOPE)
        set(${out_gcbase} "" PARENT_SCOPE)
        return()
    endif()

    unset(_found_base)
    unset(_found_utility)
    unset(_found_genapi)
    unset(_found_gcbase)

    find_library(_found_base NAMES ${_base_names} PATHS "${link_dir}" NO_DEFAULT_PATH NO_CACHE)
    find_library(_found_utility NAMES ${_utility_names} PATHS "${link_dir}" NO_DEFAULT_PATH NO_CACHE)
    find_library(_found_genapi NAMES ${_genapi_names} PATHS "${link_dir}" NO_DEFAULT_PATH NO_CACHE)
    find_library(_found_gcbase NAMES ${_gcbase_names} PATHS "${link_dir}" NO_DEFAULT_PATH NO_CACHE)

    if(BASLER_PYLON_DEBUG)
        message(STATUS "try_find_libs major=${major} link_dir=${link_dir}")
        message(STATUS "  base_names=${_base_names}")
        message(STATUS "  found_base=${_found_base}")
        message(STATUS "  found_utility=${_found_utility}")
        message(STATUS "  found_genapi=${_found_genapi}")
        message(STATUS "  found_gcbase=${_found_gcbase}")
    endif()

    if(_found_base AND _found_utility AND _found_genapi AND _found_gcbase)
        set(${out_success} TRUE PARENT_SCOPE)
        set(${out_base} "${_found_base}" PARENT_SCOPE)
        set(${out_utility} "${_found_utility}" PARENT_SCOPE)
        set(${out_genapi} "${_found_genapi}" PARENT_SCOPE)
        set(${out_gcbase} "${_found_gcbase}" PARENT_SCOPE)
    else()
        set(${out_success} FALSE PARENT_SCOPE)
        set(${out_base} "" PARENT_SCOPE)
        set(${out_utility} "" PARENT_SCOPE)
        set(${out_genapi} "" PARENT_SCOPE)
        set(${out_gcbase} "" PARENT_SCOPE)
    endif()
endfunction()

function(_basler_pylon_try_windows_candidate dev_root major label
        out_found out_include out_link out_runtime out_libs out_major out_minor out_log)
    set(_include_dir "${dev_root}/include")
    set(_link_dir "${dev_root}/lib/x64")
    get_filename_component(_install_root "${dev_root}/.." ABSOLUTE)
    set(_runtime_dir "${_install_root}/Runtime/x64")

    if(NOT EXISTS "${_include_dir}/pylon/PylonIncludes.h")
        set(${out_found} FALSE PARENT_SCOPE)
        set(${out_log} "${label}: missing ${_include_dir}/pylon/PylonIncludes.h" PARENT_SCOPE)
        return()
    endif()

    _basler_pylon_get_major_lib_names(${major} _base_names _utility_names _genapi_names _gcbase_names)
    if(NOT _base_names)
        set(${out_found} FALSE PARENT_SCOPE)
        set(${out_log} "${label}: unsupported major version ${major} on Windows" PARENT_SCOPE)
        return()
    endif()

    _basler_pylon_try_find_libs("${_link_dir}" ${major} _libs_found _base _utility _genapi _gcbase)
    if(NOT _libs_found)
        set(${out_found} FALSE PARENT_SCOPE)
        set(${out_log}
            "${label}: libraries not found in ${_link_dir} (major ${major})"
            PARENT_SCOPE)
        return()
    endif()

    _basler_pylon_parse_version_from_lib("${_base}" _parsed_major _parsed_minor)
    if(_parsed_major EQUAL 0)
        set(_parsed_major ${major})
    endif()

    set(${out_found} TRUE PARENT_SCOPE)
    set(${out_include} "${_include_dir}" PARENT_SCOPE)
    set(${out_link} "${_link_dir}" PARENT_SCOPE)
    set(${out_runtime} "${_runtime_dir}" PARENT_SCOPE)
    set(${out_libs} "${_base};${_utility};${_genapi};${_gcbase}" PARENT_SCOPE)
    set(${out_major} ${_parsed_major} PARENT_SCOPE)
    set(${out_minor} ${_parsed_minor} PARENT_SCOPE)
    set(${out_log} "${label}: found Pylon ${_parsed_major}.${_parsed_minor} at ${dev_root}" PARENT_SCOPE)
endfunction()

function(_basler_pylon_try_linux_candidate install_root major lib_subdir label
        out_found out_include out_link out_runtime out_libs out_major out_minor out_log)
    set(_include_dir "${install_root}/include")
    set(_link_dir "${install_root}/${lib_subdir}")

    if(NOT EXISTS "${_include_dir}/pylon/PylonIncludes.h")
        set(${out_found} FALSE PARENT_SCOPE)
        set(${out_log} "${label}: missing ${_include_dir}/pylon/PylonIncludes.h" PARENT_SCOPE)
        return()
    endif()

    _basler_pylon_get_major_lib_names(${major} _base_names _utility_names _genapi_names _gcbase_names)
    if(NOT _base_names)
        set(${out_found} FALSE PARENT_SCOPE)
        set(${out_log} "${label}: unsupported major version ${major} on Linux" PARENT_SCOPE)
        return()
    endif()

    _basler_pylon_try_find_libs("${_link_dir}" ${major} _libs_found _base _utility _genapi _gcbase)
    if(NOT _libs_found)
        set(${out_found} FALSE PARENT_SCOPE)
        set(${out_log}
            "${label}: libraries not found in ${_link_dir} (major ${major})"
            PARENT_SCOPE)
        return()
    endif()

    set(_parsed_major ${major})
    set(_parsed_minor 0)

    set(${out_found} TRUE PARENT_SCOPE)
    set(${out_include} "${_include_dir}" PARENT_SCOPE)
    set(${out_link} "${_link_dir}" PARENT_SCOPE)
    set(${out_runtime} "${_link_dir}" PARENT_SCOPE)
    set(${out_libs} "${_base};${_utility};${_genapi};${_gcbase}" PARENT_SCOPE)
    set(${out_major} ${_parsed_major} PARENT_SCOPE)
    set(${out_minor} ${_parsed_minor} PARENT_SCOPE)
    set(${out_log} "${label}: found Pylon ${_parsed_major}.x at ${install_root}" PARENT_SCOPE)
endfunction()

function(_basler_pylon_try_legacy_override out_found out_include out_link out_runtime
        out_libs out_major out_minor out_log)
    if(NOT BASLER_PATH OR NOT BASLER_INCLUDE_PATH)
        set(${out_found} FALSE PARENT_SCOPE)
        return()
    endif()

    if(NOT EXISTS "${BASLER_INCLUDE_PATH}/pylon/PylonIncludes.h")
        set(${out_found} FALSE PARENT_SCOPE)
        set(${out_log}
            "legacy override: missing ${BASLER_INCLUDE_PATH}/pylon/PylonIncludes.h"
            PARENT_SCOPE)
        return()
    endif()

    if(WIN32)
        set(_majors 7 6)
    else()
        set(_majors 7 5)
    endif()

    if(PYLON_VERSION)
        list(PREPEND _majors ${PYLON_VERSION})
        list(REMOVE_DUPLICATES _majors)
    endif()

    foreach(major IN LISTS _majors)
        _basler_pylon_try_find_libs("${BASLER_PATH}" ${major} _libs_found _base _utility _genapi _gcbase)
        if(_libs_found)
            if(WIN32)
                get_filename_component(_dev_root "${BASLER_PATH}/../.." ABSOLUTE)
                get_filename_component(_install_root "${_dev_root}/.." ABSOLUTE)
                set(_runtime_dir "${_install_root}/Runtime/x64")
            else()
                set(_runtime_dir "${BASLER_PATH}")
            endif()

            _basler_pylon_parse_version_from_lib("${_base}" _parsed_major _parsed_minor)
            if(_parsed_major EQUAL 0)
                set(_parsed_major ${major})
            endif()

            set(${out_found} TRUE PARENT_SCOPE)
            set(${out_include} "${BASLER_INCLUDE_PATH}" PARENT_SCOPE)
            set(${out_link} "${BASLER_PATH}" PARENT_SCOPE)
            set(${out_runtime} "${_runtime_dir}" PARENT_SCOPE)
            set(${out_libs} "${_base};${_utility};${_genapi};${_gcbase}" PARENT_SCOPE)
            set(${out_major} ${_parsed_major} PARENT_SCOPE)
            set(${out_minor} ${_parsed_minor} PARENT_SCOPE)
            set(${out_log}
                "legacy override: found Pylon ${_parsed_major}.${_parsed_minor} via BASLER_PATH"
                PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_found} FALSE PARENT_SCOPE)
    set(${out_log}
        "legacy override: no matching libraries in ${BASLER_PATH}"
        PARENT_SCOPE)
endfunction()

function(_basler_pylon_collect_windows_candidates out_candidates)
    set(_candidates)

    if(DEFINED ENV{PYLON_DEV_DIR} AND NOT "$ENV{PYLON_DEV_DIR}" STREQUAL "")
        list(APPEND _candidates "PYLON_DEV_DIR|$ENV{PYLON_DEV_DIR}")
    endif()

    list(APPEND _candidates
        "pylon 7|C:/Program Files/Basler/pylon 7/Development"
        "pylon 6|C:/Program Files/Basler/pylon 6/Development"
        "pylon|C:/Program Files/Basler/pylon/Development"
    )

    set(${out_candidates} ${_candidates} PARENT_SCOPE)
endfunction()

function(_basler_pylon_windows_major_search_order out_majors)
    set(_majors 7 6)
    if(PYLON_VERSION)
        list(PREPEND _majors ${PYLON_VERSION})
        list(REMOVE_DUPLICATES _majors)
    endif()
    set(${out_majors} ${_majors} PARENT_SCOPE)
endfunction()

function(_basler_pylon_collect_linux_candidates out_candidates)
    set(_candidates)

    if(DEFINED ENV{PYLON_ROOT} AND NOT "$ENV{PYLON_ROOT}" STREQUAL "")
        list(APPEND _candidates "7|PYLON_ROOT|$ENV{PYLON_ROOT}|lib")
        list(APPEND _candidates "5|PYLON_ROOT|$ENV{PYLON_ROOT}|lib64")
    endif()

    list(APPEND _candidates
        "7|/opt/pylon|/opt/pylon|lib"
        "5|/opt/pylon5|/opt/pylon5|lib64"
    )

    if(PYLON_VERSION)
        set(_preferred "")
        set(_rest "")
        foreach(_entry IN LISTS _candidates)
            string(REGEX MATCH "^([0-9]+)\\|" _match "${_entry}")
            if(CMAKE_MATCH_1 EQUAL PYLON_VERSION)
                list(APPEND _preferred "${_entry}")
            else()
                list(APPEND _rest "${_entry}")
            endif()
        endforeach()
        set(_candidates ${_preferred} ${_rest})
    endif()

    set(${out_candidates} ${_candidates} PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

function(find_basler_pylon)
    if(BaslerPylon_FOUND)
        return()
    endif()

    set(_attempt_logs)
    set(_found FALSE)

  # 1. Legacy override
    _basler_pylon_try_legacy_override(
        _found _include _link _runtime _libs _major _minor _log
    )
    if(_found)
        list(APPEND _attempt_logs "${_log}")
    else()
        if(_log)
            list(APPEND _attempt_logs "${_log}")
        endif()
    endif()

  # 2. Standard candidate search
    if(NOT _found)
        if(WIN32)
            _basler_pylon_collect_windows_candidates(_candidates)
            _basler_pylon_windows_major_search_order(_win_majors)
            foreach(_entry IN LISTS _candidates)
                string(REPLACE "|" ";" _parts "${_entry}")
                list(GET _parts 0 _label)
                list(GET _parts 1 _dev_root)

                foreach(major IN LISTS _win_majors)
                    _basler_pylon_try_windows_candidate(
                        "${_dev_root}" ${major} "${_label}"
                        _candidate_found _include _link _runtime _libs _major _minor _log
                    )
                    list(APPEND _attempt_logs "${_log}")
                    if(_candidate_found)
                        set(_found TRUE)
                        break()
                    endif()
                endforeach()
                if(_found)
                    break()
                endif()
            endforeach()
        else()
            _basler_pylon_collect_linux_candidates(_candidates)
            foreach(_entry IN LISTS _candidates)
                string(REPLACE "|" ";" _parts "${_entry}")
                list(GET _parts 0 _major)
                list(GET _parts 1 _label)
                list(GET _parts 2 _install_root)
                list(GET _parts 3 _lib_subdir)

                _basler_pylon_try_linux_candidate(
                    "${_install_root}" ${_major} "${_lib_subdir}" "${_label}"
                    _candidate_found _include _link _runtime _libs _major _minor _log
                )
                list(APPEND _attempt_logs "${_log}")
                if(_candidate_found)
                    set(_found TRUE)
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(_found)
        set(BaslerPylon_FOUND TRUE CACHE BOOL "Basler Pylon SDK found" FORCE)
        set(BaslerPylon_VERSION_MAJOR ${_major} CACHE STRING "Basler Pylon major version" FORCE)
        set(BaslerPylon_VERSION_MINOR ${_minor} CACHE STRING "Basler Pylon minor version" FORCE)
        set(BaslerPylon_INCLUDE_DIR "${_include}" CACHE PATH "Basler Pylon include directory" FORCE)
        set(BaslerPylon_LINK_DIR "${_link}" CACHE PATH "Basler Pylon library directory" FORCE)
        set(BaslerPylon_RUNTIME_DIR "${_runtime}" CACHE PATH "Basler Pylon runtime directory" FORCE)
        set(BaslerPylon_LIBRARIES "${_libs}" CACHE STRING "Basler Pylon libraries" FORCE)
        set(BASLER_LINK_PATH "${_link}" CACHE PATH "Legacy alias for BaslerPylon_LINK_DIR" FORCE)

        message(STATUS "Basler Pylon: ${_major}.${_minor}")
        message(STATUS "  include: ${_include}")
        message(STATUS "  link:    ${_link}")
        message(STATUS "  runtime: ${_runtime}")
    else()
        set(BaslerPylon_FOUND FALSE CACHE BOOL "Basler Pylon SDK found" FORCE)
        string(JOIN "\n  " _log_text ${_attempt_logs})
        if(BUILD_BASLER)
            message(FATAL_ERROR
                "Basler Pylon SDK not found (BUILD_BASLER=ON).\n"
                "Attempts:\n  ${_log_text}\n"
                "Install Pylon or set PYLON_DEV_DIR (Windows), PYLON_ROOT (Linux), "
                "or legacy BASLER_PATH/BASLER_INCLUDE_PATH."
            )
        else()
            message(STATUS "Basler Pylon SDK not found (BUILD_BASLER=OFF)")
        endif()
    endif()
endfunction()

function(basler_pylon_setup_test_runtime)
    if(NOT BaslerPylon_FOUND)
        return()
    endif()

    if(NOT BaslerPylon_RUNTIME_DIR OR NOT EXISTS "${BaslerPylon_RUNTIME_DIR}")
        message(WARNING
            "Pylon runtime not found at ${BaslerPylon_RUNTIME_DIR}; "
            "tests may fail to load Basler libraries"
        )
        return()
    endif()

    if(WIN32)
        file(TO_NATIVE_PATH "${BaslerPylon_RUNTIME_DIR}" _runtime_native)
        foreach(_test IN LISTS ARGN)
            set_property(TEST ${_test} APPEND PROPERTY
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_runtime_native}"
            )
        endforeach()
    else()
        foreach(_test IN LISTS ARGN)
            set_property(TEST ${_test} APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                    "LD_LIBRARY_PATH=path_list_prepend:${BaslerPylon_RUNTIME_DIR}"
            )
        endforeach()
    endif()
endfunction()
