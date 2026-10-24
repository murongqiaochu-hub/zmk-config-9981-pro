# SPDX-License-Identifier: MIT
# Only the selected host policy is new; keep all existing source prefixes intact.
if(CONFIG_BBP9981_BLE_SINGLE_HOST AND CONFIG_ZMK_BLE AND
   (NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
    if(CMAKE_VERSION VERSION_LESS 3.18)
        message(FATAL_ERROR "Single-host BLE integration requires CMake >= 3.18")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/ble_single_host_sdk_guard.cmake")
    zephyr_library_sources(custom_driver/ble_single_host.c)
    # Wrap only the pinned BLE TU's auth registration, not global BT APIs.
    set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/ble.c"
        DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
        "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/ble_single_host_prefix.h")
    # Read-only proof from the actual final ARM ELF, not the CMake recorder.
    set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
        COMMAND "${CMAKE_NM}" "--defined-only" "$<TARGET_FILE:zephyr_final>")
endif()
