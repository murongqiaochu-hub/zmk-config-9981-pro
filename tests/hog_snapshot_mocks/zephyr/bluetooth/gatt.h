/* SPDX-License-Identifier: MIT -- pinned macro shape, minimal test attributes */
#ifndef HOG_SNAPSHOT_MOCK_GATT_H
#define HOG_SNAPSHOT_MOCK_GATT_H
#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/conn.h>
struct bt_gatt_attr { unsigned int marker; };
struct bt_gatt_service_static { const struct bt_gatt_attr *attrs; size_t attr_count; };
/* Same const/linkage/type as v3.5.0+zmk-fixes, without iterable section linking. */
#define STRUCT_SECTION_ITERABLE(type, name) struct type name
#define BT_GATT_SERVICE(attrs_) {.attrs = (attrs_), .attr_count = sizeof(attrs_) / sizeof((attrs_)[0])}
#define BT_GATT_SERVICE_DEFINE(name, ...) \
    const struct bt_gatt_attr attr_##name[] = {__VA_ARGS__}; \
    const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, name) = BT_GATT_SERVICE(attr_##name)
typedef void (*bt_gatt_complete_func_t)(struct bt_conn *, void *);
struct bt_gatt_notify_params {
    const void *uuid;
    const struct bt_gatt_attr *attr;
    const void *data;
    uint16_t len;
    bt_gatt_complete_func_t func;
    void *user_data;
};
int bt_gatt_notify_cb(struct bt_conn *conn, struct bt_gatt_notify_params *params);
#endif
