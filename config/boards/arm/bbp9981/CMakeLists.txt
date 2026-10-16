# Copyright (c) 2025 ZitaoTech
# SPDX-License-Identifier: MIT

if(CONFIG_BOARD_BBP9981)
    zephyr_library()
    zephyr_library_sources(custom_driver/a320_0x57.c)
    zephyr_library_sources(custom_driver/behavior_aa_sym_ctrl.c)
    zephyr_library_sources(custom_driver/trackpad_led.c)
    zephyr_library_sources(custom_driver/keyboard_backlight.c)
    zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
    zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/custom_driver)
    zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
    if(CONFIG_A320_MICROTAP)
        zephyr_library_sources(custom_driver/trackpad_microtap_runtime.c
                               custom_driver/behavior_microtap_shoulder.c)
    endif()
    if(CONFIG_A320_TAP_OUTPUT)
        zephyr_library_sources(custom_driver/trackpad_tap_output.c custom_driver/trackpad_tap_pipeline.c)
        # Source-specific extension, no SDK checkout edits or global force-include.
        if(CMAKE_VERSION VERSION_LESS 3.18)
            message(FATAL_ERROR "Tap SDK source extensions require CMake >= 3.18")
        endif()
        set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/hid.c"
            DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
            "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/trackpad_hid_owners.h")
        if(CONFIG_ZMK_BLE)
            set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/hog.c"
                DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
                "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/trackpad_hog_snapshot.h")
        endif()
        if(CONFIG_ZMK_USB)
            set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/usb.c"
                DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
                "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/trackpad_usb_epoch.h")
            set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/usb_hid.c"
                DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
                "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/trackpad_usb_snapshot.h")
        endif()
    endif()
    if(CONFIG_BBP9981_MENU_DIAG)
        zephyr_library_sources(custom_driver/menu_click_diag.c)
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
endif()
