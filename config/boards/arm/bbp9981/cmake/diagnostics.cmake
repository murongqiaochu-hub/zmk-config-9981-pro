# SPDX-License-Identifier: MIT
# Included from the board directory; preserve SDK source scope and ordering.
if(CONFIG_BBP9981_MENU_DIAG)
    zephyr_library_sources(custom_driver/menu_click_diag.c)
    if(CMAKE_VERSION VERSION_LESS 3.18)
        message(FATAL_ERROR "Endpoint diagnostic extension requires CMake >= 3.18")
    endif()
    # Same source-only APP-directory scope as HID above; no global prefix.
    # The worker's snapshot reference requires this injection at link time.
    set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/endpoints.c"
        DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
        "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/menu_diag_endpoints.h")
    set_property(GLOBAL PROPERTY BBP9981_DIAG_PREFIX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver")
    # Defer until module/library targets exist; amend only the pinned matrix TU.
    function(bbp9981_inject_matrix_diag)
        get_property(_prefix GLOBAL PROPERTY BBP9981_DIAG_PREFIX_DIR)
        get_property(_libs GLOBAL PROPERTY ZEPHYR_LIBS)
        set(_found FALSE)
        foreach(_lib IN LISTS _libs)
            if(TARGET "${_lib}")
                get_target_property(_sources "${_lib}" SOURCES)
                foreach(_source IN LISTS _sources)
                    get_filename_component(_name "${_source}" NAME)
                    if(_name STREQUAL "kscan_gpio_matrix.c")
                        if(NOT IS_ABSOLUTE "${_source}")
                            message(FATAL_ERROR "Matrix diagnostic requires pinned absolute source path")
                        endif()
                        set_property(SOURCE "${_source}" TARGET_DIRECTORY "${_lib}" APPEND PROPERTY
                            COMPILE_OPTIONS "-I${APPLICATION_SOURCE_DIR}/module/drivers/kscan"
                            "-include" "${_prefix}/menu_diag_matrix.h")
                        set(_found TRUE)
                    endif()
                endforeach()
            endif()
        endforeach()
        if(NOT _found)
            message(FATAL_ERROR "Pinned kscan matrix diagnostic target was not found")
        endif()
    endfunction()
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL bbp9981_inject_matrix_diag)
endif()