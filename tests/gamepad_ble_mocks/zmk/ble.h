/* SPDX-License-Identifier: MIT -- pinned public declarations, host-only model. */
#pragma once
#include <zephyr/bluetooth/conn.h>
int zmk_ble_active_profile_index(void);
int zmk_ble_profile_index(const bt_addr_le_t *addr);
struct bt_conn *zmk_ble_active_profile_conn(void);
