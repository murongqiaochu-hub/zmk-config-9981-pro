# SPDX-License-Identifier: MIT
# Included from the board directory; preserve SDK source scope and ordering.
if(CONFIG_BBP9981_GAMEPAD)
    include("${CMAKE_CURRENT_LIST_DIR}/gamepad_sdk_guard.cmake")
    zephyr_library_sources(custom_driver/behavior_gamepad.c custom_driver/gamepad_runtime.c)
    if(CONFIG_ZMK_USB)
        # Independent interface; never replace HID_0 callbacks/semaphore.
        zephyr_library_sources(custom_driver/gamepad_usb.c)
        if(NOT CONFIG_A320_TAP_OUTPUT)
            set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/usb.c"
                DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
                "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/trackpad_usb_epoch.h")
        endif()
        # Chain AFTER that prefix; distinguish CONFIGURED(0) without changing HID_0.
        set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/usb.c"
            DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
            "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/gamepad_usb_lifecycle.h")
    endif()
    if(CONFIG_ZMK_BLE)
        # A separate HIDS instance; never rewrite the stock keyboard/mouse HOG.
        zephyr_library_sources(custom_driver/gamepad_ble_service.c)
    endif()
endif()
