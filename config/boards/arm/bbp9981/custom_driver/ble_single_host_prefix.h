/* SPDX-License-Identifier: MIT
 * Force-include ONLY into pinned ZMK app/src/ble.c when the policy is ON.
 * Include native declarations before installing the two source-local aliases.
 * Advertising, profile variables and all other SDK APIs are untouched.
 */
#pragma once
#include <zephyr/bluetooth/conn.h>
#include "ble_single_host.h"

#if IS_ENABLED(CONFIG_BBP9981_BLE_SINGLE_HOST) && IS_ENABLED(CONFIG_ZMK_BLE) && \
    !(IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
/* An ARM build note proves the prefix reached the actual BLE translation unit. */
#pragma message("BBP9981_BLE_SINGLE_HOST_AUTH_PREFIX_ACTIVE")
#define bt_conn_auth_cb_register bbp_ble_single_host_auth_cb_register
#define bt_conn_auth_info_cb_register bbp_ble_single_host_auth_info_cb_register
#endif
