# Copyright (c) 2025 ZitaoTech
# SPDX-License-Identifier: MIT

if(CONFIG_BOARD_BBP9981)
    zephyr_library()
    zephyr_library_sources(custom_driver/a320_0x57.c)
    zephyr_library_sources(custom_driver/behavior_aa_sym_ctrl.c)
    zephyr_library_sources(custom_driver/trackpad_led.c)
    zephyr_library_sources(custom_driver/keyboard_backlight.c)
    # Compile final-DT assertions for this controlled column-drive experiment.
    zephyr_library_sources(custom_driver/matrix_isolation_guard.c)
    execute_process(COMMAND git -C "${ZEPHYR_BASE}" rev-parse HEAD
        RESULT_VARIABLE _iso_git_result OUTPUT_VARIABLE _iso_zephyr_rev
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(LENGTH "${_iso_zephyr_rev}" _iso_rev_len)
    if(NOT _iso_git_result EQUAL 0 OR NOT _iso_rev_len EQUAL 40)
        message(FATAL_ERROR "Cannot record actual Zephyr revision for matrix experiment")
    endif()
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/matrix_isolation_guard.c"
        APPEND PROPERTY COMPILE_DEFINITIONS "BBP_ISO_ZEPHYR_REV=\"${_iso_zephyr_rev}\"")
    message(STATUS "MATRIX_ISO col_hiz_20261017 Zephyr=${_iso_zephyr_rev}")
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
    if(CONFIG_BBP9981_GAMEPAD)
        # GATT macro shape and nRF RAM-lifetime proof were reviewed at this SDK.
        if(NOT _iso_zephyr_rev STREQUAL "dacab4875df72109b96cc8977547a0dc04875bcd")
            message(FATAL_ERROR "Gamepad requires re-review for this Zephyr revision: ${_iso_zephyr_rev}")
        endif()
        execute_process(COMMAND git -C "${ZEPHYR_BASE}" diff --quiet HEAD --
            drivers/usb/device/usb_dc_nrfx.c subsys/usb/device/class/hid/core.c
            subsys/usb/device/usb_device.c subsys/usb/device/usb_descriptor.c
            include/zephyr/usb/usb_device.h include/zephyr/usb/class/usb_hid.h
            include/zephyr/bluetooth/gatt.h subsys/bluetooth/host/gatt.c
            RESULT_VARIABLE _gamepad_sdk_dirty)
        if(NOT _gamepad_sdk_dirty EQUAL 0)
            message(FATAL_ERROR "Gamepad reviewed Zephyr USB/GATT files must be clean")
        endif()
        if(CONFIG_ZMK_USB)
            if(DEFINED ZEPHYR_HAL_NORDIC_MODULE_DIR)
                set(_gp_hal "${ZEPHYR_HAL_NORDIC_MODULE_DIR}")
            else()
                set(_gp_hal "${ZEPHYR_BASE}/../modules/hal/nordic")
            endif()
            execute_process(COMMAND git -C "${_gp_hal}" rev-parse HEAD
                RESULT_VARIABLE _gp_hal_git OUTPUT_VARIABLE _gp_hal_rev
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT _gp_hal_git EQUAL 0 OR NOT _gp_hal_rev STREQUAL "884c4d61746bc35fbd379c169fc87ddb56c6461d")
                message(FATAL_ERROR "Gamepad RAM proof requires reviewed hal_nordic 884c4d: ${_gp_hal_rev}")
            endif()
            execute_process(COMMAND git -C "${_gp_hal}" diff --quiet HEAD --
                nrfx/drivers/src/nrfx_usbd.c nrfx/drivers/include/nrfx_usbd.h nrfx/hal/nrf_usbd.h
                RESULT_VARIABLE _gp_hal_dirty)
            if(NOT _gp_hal_dirty EQUAL 0)
                message(FATAL_ERROR "Gamepad reviewed nRF USB implementation/headers must be clean")
            endif()
            message(STATUS "GAMEPAD_SDK Zephyr=${_iso_zephyr_rev} HAL=${_gp_hal_rev}")
        endif()
        foreach(_fixture IN ITEMS "src/hog.c" "src/usb_hid.c" "src/usb.c" "include/zmk/hid.h")
            file(SHA256 "${APPLICATION_SOURCE_DIR}/${_fixture}" _actual_sha)
            if(_fixture STREQUAL "src/hog.c")
                set(_expected_sha "2e8c12bb4197173d238a6be1e33abf2db5dedbd4a6cf29a83d5e2f6c79d4919b")
            elseif(_fixture STREQUAL "src/usb_hid.c")
                set(_expected_sha "3c7a934a2b5eac808889cd14cbc2d8b40e1dcc35a4ce0fbf6e6ac3a12b51ac8a")
            elseif(_fixture STREQUAL "src/usb.c")
                set(_expected_sha "4b182d9a942c6a09d4c6849f2c9504fd251b7bf5d1cd741e5d05dc82534c138e")
            else()
                set(_expected_sha "dd161a6631a9dad59f320389a0c146712b2c953215f4a97a8d0d64b0b3ffdee7")
            endif()
            if(NOT _actual_sha STREQUAL _expected_sha)
                message(FATAL_ERROR "Gamepad pinned ZMK source changed: ${_fixture}")
            endif()
        endforeach()
        zephyr_library_sources(custom_driver/behavior_gamepad.c)
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
            # AFTER the existing mouse snapshot prefix. Tail attrs keep5/9/13.
            set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/hog.c"
                DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
                "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/gamepad_ble_extension.h")
        endif()
    endif()
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
endif()
