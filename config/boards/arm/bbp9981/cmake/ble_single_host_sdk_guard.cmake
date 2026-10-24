# SPDX-License-Identifier: MIT
# The policy delegates pinned auth callbacks and relies on their pre-save order.
execute_process(COMMAND git -C "${APPLICATION_SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE _ble_host_zmk_git OUTPUT_VARIABLE _ble_host_zmk_rev
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _ble_host_zmk_git EQUAL 0 OR
   NOT _ble_host_zmk_rev STREQUAL "edf5c0814fd3ea202e43aad2d68fd32e882a518c")
    message(FATAL_ERROR "Single-host BLE requires reviewed ZMK edf5c081: ${_ble_host_zmk_rev}")
endif()
execute_process(COMMAND git -C "${ZEPHYR_BASE}" rev-parse HEAD
    RESULT_VARIABLE _ble_host_zephyr_git OUTPUT_VARIABLE _ble_host_zephyr_rev
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _ble_host_zephyr_git EQUAL 0 OR
   NOT _ble_host_zephyr_rev STREQUAL "dacab4875df72109b96cc8977547a0dc04875bcd")
    message(FATAL_ERROR "Single-host BLE requires reviewed Zephyr dacab487: ${_ble_host_zephyr_rev}")
endif()
execute_process(COMMAND git -C "${APPLICATION_SOURCE_DIR}" diff --quiet HEAD --
    src/ble.c include/zmk/ble.h include/zmk/ble/profile.h
    include/zmk/events/ble_active_profile_changed.h include/zmk/event_manager.h
    RESULT_VARIABLE _ble_host_zmk_dirty)
if(NOT _ble_host_zmk_dirty EQUAL 0)
    message(FATAL_ERROR "Single-host BLE reviewed ZMK sources must be clean")
endif()
execute_process(COMMAND git -C "${ZEPHYR_BASE}" diff --quiet HEAD --
    include/zephyr/bluetooth/conn.h include/zephyr/bluetooth/bluetooth.h
    include/zephyr/bluetooth/addr.h include/zephyr/kernel.h
    kernel/sched.c kernel/work.c subsys/bluetooth/host/conn.c
    subsys/bluetooth/host/id.c subsys/bluetooth/host/keys.c
    subsys/bluetooth/host/smp.c
    RESULT_VARIABLE _ble_host_zephyr_dirty)
if(NOT _ble_host_zephyr_dirty EQUAL 0)
    message(FATAL_ERROR "Single-host BLE reviewed Zephyr connection/security sources must be clean")
endif()
foreach(_ble_host_fixture IN ITEMS "src/ble.c" "include/zmk/ble.h"
        "include/zmk/ble/profile.h" "include/zmk/events/ble_active_profile_changed.h")
    file(SHA256 "${APPLICATION_SOURCE_DIR}/${_ble_host_fixture}" _ble_host_sha)
    if(_ble_host_fixture STREQUAL "src/ble.c")
        set(_ble_host_expected "ab936cdb01c2c9ddd3c90ac1c231fb18b791f92e2f5ab8e934b8d8bbd23291d0")
    elseif(_ble_host_fixture STREQUAL "include/zmk/ble.h")
        set(_ble_host_expected "f1a212524ca101289d21fcfe9f2e27de0f61a741582878974d7b7b262f6ec500")
    elseif(_ble_host_fixture STREQUAL "include/zmk/ble/profile.h")
        set(_ble_host_expected "f680f4603b13e2a9b6e00639a60d62e80ae3add18440f802e3e7ad8940f28aef")
    else()
        set(_ble_host_expected "aed2b7d6c3ba6a28319501cdcce25aee7d6fa6254c29a6bbc6b864cf8f81559b")
    endif()
    if(NOT _ble_host_sha STREQUAL _ble_host_expected)
        message(FATAL_ERROR "Single-host BLE pinned source changed: ${_ble_host_fixture}")
    endif()
endforeach()
message(STATUS "BLE_SINGLE_HOST_SDK ZMK=${_ble_host_zmk_rev} Zephyr=${_ble_host_zephyr_rev}")
