# Copyright (c) 2025 ZitaoTech
# SPDX-License-Identifier: MIT

if(CONFIG_BOARD_BBP9981)
    zephyr_library()
    zephyr_library_sources(custom_driver/a320_0x57.c custom_driver/trackpad_mode.c)
    zephyr_library_sources(custom_driver/behavior_aa_sym_ctrl.c
                           custom_driver/behavior_aa_scroll_shoulder.c
                           custom_driver/behavior_aa_scroll_gesture_key.c
                           custom_driver/behavior_aa_layer.c)
    zephyr_library_sources(custom_driver/trackpad_led.c)
    zephyr_library_sources(custom_driver/keyboard_backlight.c)
    # v2.3.3: DTS precedes Kconfig; always provide the node's OFF-safe adapter.
    zephyr_library_sources(custom_driver/behavior_cursor_speed.c)
    if(CONFIG_BBP9981_CURSOR_SPEED)
        zephyr_library_sources(custom_driver/trackpad_cursor_speed.c)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/matrix_guard.cmake")
    zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
    zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/custom_driver)
    zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
    if(CONFIG_A320_MICROTAP)
        zephyr_library_sources(custom_driver/trackpad_microtap_runtime.c
                               custom_driver/behavior_microtap_shoulder.c)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/tap_extensions.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/gamepad.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/diagnostics.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ble_single_host.cmake")

endif()
