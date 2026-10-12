/* SPDX-License-Identifier: MIT -- relevant public ZMK v0.3.0 BLE declarations */
#ifndef HOG_SNAPSHOT_MOCK_BLE_H
#define HOG_SNAPSHOT_MOCK_BLE_H
#include <zephyr/bluetooth/conn.h>
int zmk_ble_active_profile_index(void);
int zmk_ble_profile_index(const bt_addr_le_t *addr);
struct bt_conn *zmk_ble_active_profile_conn(void);
#endif
