/* SPDX-License-Identifier: MIT -- observer only, no payload or identity bytes. */
#pragma once
#include <stdint.h>
#include <zephyr/bluetooth/gatt.h>
#if defined(CONFIG_BBP9981_IPAD_DIAG) && CONFIG_BBP9981_IPAD_DIAG
int bbp_ipdiag_notify(struct bt_conn *conn, struct bt_gatt_notify_params *params);
void bbp_ipdiag_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                    uint16_t requested, uint16_t offset, const void *value,
                    uint16_t total, ssize_t result, bool map);
#endif
