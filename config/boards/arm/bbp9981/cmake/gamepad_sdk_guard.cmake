# SPDX-License-Identifier: MIT
# Reviewed SDK identities are acceptance gates, not advisory version hints.
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
