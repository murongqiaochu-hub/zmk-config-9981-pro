/* SPDX-License-Identifier: MIT
 * HOST-ONLY preinclude for the fixed tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c.
 * This is not firmware and must never be linked into a Zephyr image. The SDK
 * fixture's single hog_svc declaration is expanded with two additional reports;
 * its original declarations, callbacks, control point and sender indices stay.
 * No second HIDS service, production route, queue, pairing or advertising.
 *
 * The HID mock has a SHORT 45-byte representative descriptor, not the stock
 * 196-byte map. The host harness must supply the independently pinned 196 bytes.
 * Only the exact mock descriptor object, size AND actual map attribute identify
 * the intercepted read. All other reads use the original mock bt_gatt_attr_read.
 * There is no claim here about a production force-include or real Zephyr CCC.
 */
#pragma once
#if !defined(BBP_COMPOSITE_HOG_HOST_ONLY) || BBP_COMPOSITE_HOG_HOST_ONLY != 1
#error "composite_hog_host.h requires BBP_COMPOSITE_HOG_HOST_ONLY=1"
#endif
#if !defined(BBP_COMPOSITE_HOST_ONLY) || BBP_COMPOSITE_HOST_ONLY != 1
#error "composite_hog_host.h requires BBP_COMPOSITE_HOST_ONLY=1"
#endif
#if !defined(CONFIG_ZMK_POINTING) || CONFIG_ZMK_POINTING != 1 || \
    !defined(CONFIG_ZMK_HID_INDICATORS) || CONFIG_ZMK_HID_INDICATORS != 1 || \
    !defined(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING) || CONFIG_ZMK_POINTING_SMOOTH_SCROLLING != 0
#error "composite host model requires pointing=1, indicators=1, smooth=0"
#endif

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/hid.h>
#include "composite_map_host.h"

/* The harness copies in a SHA-verified stock fixture, not the mock descriptor. */
static uint8_t bbp_composite_hog_stock[BBP_COMPOSITE_STOCK_MAP_SIZE];
static bool bbp_composite_hog_stock_ready;
static int bbp_composite_hog_load_stock(const uint8_t *stock, size_t size) {
    if (!stock || size != sizeof(bbp_composite_hog_stock)) { return -EINVAL; }
    memmove(bbp_composite_hog_stock, stock, size);
    bbp_composite_hog_stock_ready = true;
    return 0;
}

/* Harness-owned, deliberately per-connection snapshot hook. No lifecycle,
 * synchronization, active-route or epoch guarantee is provided by this model. */
bool bbp_composite_hog_capture(struct bt_conn *conn, struct bbp_gamepad_report_body *body);
extern const struct bt_gatt_service_static hog_svc;

static ssize_t bbp_composite_hog_attr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                          void *buf, uint16_t len, uint16_t offset,
                                          const void *value, uint16_t value_len) {
    if (attr != &hog_svc.attrs[4] || value != (const void *)zmk_hid_report_desc ||
        value_len != sizeof(zmk_hid_report_desc)) {
        return bt_gatt_attr_read(conn, attr, buf, len, offset, value, value_len);
    }
    if (!bbp_composite_hog_stock_ready) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    int result = bbp_composite_map_read(bbp_composite_hog_stock, sizeof(bbp_composite_hog_stock),
                                        offset, buf, len);
    if (result == -ERANGE) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    if (result < 0) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    return result;
}

static ssize_t bbp_composite_hog_read_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                         void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, 2U);
}
static ssize_t bbp_composite_hog_read_input(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                           void *buf, uint16_t len, uint16_t offset) {
    struct bbp_gamepad_report_body body = bbp_gamepad_neutral();
    uint8_t encoded[BBP_EXP_INPUT_SIZE];
    if (!conn || !bbp_composite_hog_capture(conn, &body)) { body = bbp_gamepad_neutral(); }
    /* Invalid state is encoded as neutral by the existing safe wrapper. */
    (void)bbp_composite_encode(&body, encoded, sizeof(encoded));
    return bt_gatt_attr_read(conn, attr, buf, len, offset, encoded, sizeof(encoded));
}
static ssize_t bbp_composite_hog_read_output(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len, uint16_t offset) {
    const uint8_t zero[BBP_EXP_OUTPUT_SIZE] = {0};
    return bt_gatt_attr_read(conn, attr, buf, len, offset, zero, sizeof(zero));
}
static ssize_t bbp_composite_hog_write_output(struct bt_conn *conn,
                                             const struct bt_gatt_attr *attr, const void *buf,
                                             uint16_t len, uint16_t offset, uint8_t flags) {
    (void)conn; (void)attr;
    /* Mock accepts complete writes/commands, rejects prepare/execute fragments. */
    if (flags & (uint8_t)~0x02U) { return BT_GATT_ERR(0x06); }
    if (offset) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    if (len != BBP_EXP_OUTPUT_SIZE) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN); }
    if (!buf) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    return len; /* Accepted but discarded; not an actuator implementation. */
}
static void bbp_composite_hog_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    /* Aggregate change only: this callback is NOT evidence of a peer subscription. */
    (void)attr; (void)value;
}

/* Output precedes input so the input value/CCC/reference remain at -3/-2/-1.
 * The expansion retains the fixture's complete __VA_ARGS__ verbatim: notably
 * stock LED and Control Point are not re-created by this host prototype. */
#undef BT_GATT_SERVICE_DEFINE
#define BT_GATT_SERVICE_DEFINE(_name, ...) \
    const struct bt_gatt_attr attr_##_name[] = { \
        __VA_ARGS__, \
        BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, \
            BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, \
            BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, \
            bbp_composite_hog_read_output, bbp_composite_hog_write_output, NULL), \
        BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, \
            bbp_composite_hog_read_ref, NULL, (void *)bbp_composite_output_ref), \
        BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
            BT_GATT_PERM_READ_ENCRYPT, bbp_composite_hog_read_input, NULL, NULL), \
        BT_GATT_CCC(bbp_composite_hog_ccc_changed, \
            BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT), \
        BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, \
            bbp_composite_hog_read_ref, NULL, (void *)bbp_composite_input_ref) \
    }; \
    const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, _name) = BT_GATT_SERVICE(attr_##_name)

/* Last: only the subsequently compiled fixture sees this wrapper. */
#define bt_gatt_attr_read bbp_composite_hog_attr_read
