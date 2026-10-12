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
endif()
