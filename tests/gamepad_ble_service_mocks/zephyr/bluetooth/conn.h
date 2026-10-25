/* SPDX-License-Identifier: MIT -- extend archival fixture without editing it. */
#pragma once
#include "../../../gamepad_ble_mocks/zephyr/bluetooth/conn.h"
#ifndef CONFIG_BT_MAX_CONN
#define CONFIG_BT_MAX_CONN 4
#endif
static inline uint8_t bt_conn_index(const struct bt_conn *conn) { return conn->dst.a.val[0]; }
struct bt_conn_cb {
    void (*connected)(struct bt_conn *, uint8_t);
    void (*disconnected)(struct bt_conn *, uint8_t);
};
#define BT_CONN_CB_DEFINE(name) struct bt_conn_cb name
