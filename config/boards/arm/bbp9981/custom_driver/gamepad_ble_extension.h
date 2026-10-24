/* SPDX-License-Identifier: MIT
 * Force-include ONLY into pinned ZMK v0.3.0 app/src/hog.c, after any mouse
 * snapshot preinclude. Never inject this into another SDK translation unit.
 * ZMK: edf5c0814fd3ea202e43aad2d68fd32e882a518c
 * Zephyr GATT macros: dacab4875df72109b96cc8977547a0dc04875bcd
 */
#pragma once

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include "gamepad_runtime.h"

/* Include the official declarations and descriptor BEFORE the read alias. */
static const uint8_t bbp_gamepad_ble_descriptor[] = {BBP_GAMEPAD_DESCRIPTOR_BYTES};
_Static_assert(sizeof(struct bbp_gamepad_report_body) == 9U, "BLE Game Pad body has no ID byte");
_Static_assert(sizeof(zmk_hid_report_desc) + sizeof(bbp_gamepad_ble_descriptor) <= UINT16_MAX,
               "HOG report map must fit the GATT read ABI");

/* A two-segment immutable slice, not a large stack or shared concatenation.
 * Only the exact stock descriptor object AND stock length identify Report Map.
 * Other HOG reads keep the SDK helper's offset/error semantics; the optional
 * metadata correction below changes only the two bcdHID bytes of HID Information.
 */
static ssize_t bbp_gamepad_ble_attr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len, uint16_t offset,
                                       const void *value, uint16_t value_len) {
#if IS_ENABLED(CONFIG_BBP9981_BLE_HID_METADATA_COMPAT)
#pragma message("BBP9981_BLE_HID_METADATA_COMPAT_ACTIVE")
    if (attr && attr->uuid && value && value_len == 4U &&
        bt_uuid_cmp(attr->uuid, BT_UUID_HIDS_INFO) == 0) {
        const uint8_t *original = value;
        /* The native helper copies synchronously; no pointer is retained.
         * HID 1.11 is little-endian 0x0111. Country and flags remain stock. */
        const uint8_t corrected[4] = {0x11U, 0x01U, original[2], original[3]};
        return bt_gatt_attr_read(conn, attr, buf, len, offset, corrected, sizeof(corrected));
    }
#endif
    if (value != (const void *)zmk_hid_report_desc || value_len != sizeof(zmk_hid_report_desc)) {
        return bt_gatt_attr_read(conn, attr, buf, len, offset, value, value_len);
    }
    const size_t stock_size = sizeof(zmk_hid_report_desc);
    const size_t total_size = stock_size + sizeof(bbp_gamepad_ble_descriptor);
    if ((size_t)offset > total_size) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    size_t count = total_size - offset;
    if (count > len) { count = len; }
    if (!count) { return 0; }

    uint8_t *out = buf;
    size_t pos = offset, copied = 0U;
    if (pos < stock_size) {
        copied = stock_size - pos;
        if (copied > count) { copied = count; }
        memcpy(out, zmk_hid_report_desc + pos, copied);
        pos += copied;
    }
    if (copied < count) {
        memcpy(out + copied, bbp_gamepad_ble_descriptor + (pos - stock_size), count - copied);
    }
    return (ssize_t)count;
}

static bool bbp_gamepad_ble_connected(const struct bt_conn *conn) {
    struct bt_conn_info info;
    return conn && bt_conn_get_info(conn, &info) == 0 && info.state == BT_CONN_STATE_CONNECTED;
}

/* This check takes no reference: the sending worker owns captured_conn. Besides
 * the epoch, reject profile/route changes whose event has not yet been handled.
 */
static bool bbp_gamepad_ble_route_matches(const struct bt_conn *conn) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    if (!conn || endpoint.transport != ZMK_TRANSPORT_BLE) { return false; }
    int profile = zmk_ble_active_profile_index();
    const bt_addr_le_t *dst = bt_conn_get_dst(conn);
    return profile >= 0 && endpoint.ble.profile_index == profile && dst &&
           zmk_ble_profile_index(dst) == profile;
}

/* The active getter returns its OWN temporary reference in pinned ZMK. Always
 * release that reference, never consume the worker's captured_conn ownership.
 * Pointer equality also rejects an old object for the same reconnected peer.
 */
static bool bbp_gamepad_ble_peer_current(struct bt_conn *conn, uint32_t epoch) {
    if (!bbp_gamepad_ble_connected(conn) || !bbp_gamepad_ble_route_matches(conn) ||
        !bbp_gamepad_context_valid(epoch)) {
        return false;
    }
    struct bt_conn *active = zmk_ble_active_profile_conn();
    bool matches = active == conn;
    if (active) { bt_conn_unref(active); }
    return matches && bbp_gamepad_ble_connected(conn) && bbp_gamepad_ble_route_matches(conn) &&
           bbp_gamepad_context_valid(epoch);
}

static ssize_t bbp_gamepad_ble_read_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                         void *buf, uint16_t len, uint16_t offset) {
    struct bbp_gamepad_report_body body = bbp_gamepad_neutral();
    uint32_t epoch = bbp_gamepad_context_epoch();
    if (bbp_gamepad_ble_peer_current(conn, epoch)) {
        bbp_gamepad_get_report(1U, &body); /* ZMK_TRANSPORT_BLE == 1, no report ID byte. */
        if (!bbp_gamepad_ble_peer_current(conn, epoch)) {
            body = bbp_gamepad_neutral();
        }
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &body, sizeof(body));
}

/* Report Reference is the ONLY BLE report payload carrying ID4. The INPUT
 * characteristic body is exactly nine bytes, with neutral hat=8 (not zero)
 * and LT/RT=0. X/Y/Z/Rz are active signed8 axes; LT/RT are unsigned8 axes.
 * Non-const matches the SDK's void * GATT user_data initializer ABI.
 */
static uint8_t bbp_gamepad_ble_report_ref[2] = {BBP_GAMEPAD_REPORT_ID, 0x01U};
static ssize_t bbp_gamepad_ble_read_report_ref(struct bt_conn *conn,
                                             const struct bt_gatt_attr *attr, void *buf,
                                             uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(bbp_gamepad_ble_report_ref));
}

static void bbp_gamepad_ble_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    (void)attr;
    (void)value;
    /* A separate CCC object; never modify stock host_requests_notification.
     * Resnapshot/cancel through the main runtime, never send from a BT callback.
     */
    bbp_gamepad_transport_invalidate();
}

/* Fail closed if a future source/macro change violates the private tail ABI.
 * No UUID scan or mutable service is needed: callback/data identity proves
 * that this is OUR encrypted value/reference pair before pointer arithmetic
 * is used by the sender.
 */
static const struct bt_gatt_attr *
bbp_gamepad_ble_value_attr(const struct bt_gatt_service_static *service) {
    if (!service || !service->attrs || service->attr_count < 4U) { return NULL; }
    const struct bt_gatt_attr *value = &service->attrs[service->attr_count - 3U];
    const struct bt_gatt_attr *ref = &service->attrs[service->attr_count - 1U];
    if (value->read != bbp_gamepad_ble_read_report ||
        !(value->perm & BT_GATT_PERM_READ_ENCRYPT) ||
        ref->read != bbp_gamepad_ble_read_report_ref ||
        ref->user_data != bbp_gamepad_ble_report_ref) {
        return NULL;
    }
    return value;
}

extern const struct bt_gatt_service_static hog_svc;
int bbp_gamepad_ble_send(struct bt_conn *captured_conn,
                          const struct bbp_gamepad_report_body *body,
                          uint32_t expected_context_epoch) {
    if (!body) { return -EINVAL; }
    if (!bbp_gamepad_ble_connected(captured_conn)) { return -ENOTCONN; }

    /* Tail layout: declaration, VALUE, independent CCC, Report Reference.
     * Use VALUE, not declaration: pinned bt_gatt_notify_cb must see its
     * READ_ENCRYPT permission instead of the declaration's plain READ.
     */
    const struct bt_gatt_attr *value = bbp_gamepad_ble_value_attr(&hog_svc);
    if (!value) { return -EINVAL; }
    /* An unenabled CCC is backpressure, not evidence for a security upgrade. */
    if (!bt_gatt_is_subscribed(captured_conn, value, BT_GATT_CCC_NOTIFY)) { return -EAGAIN; }
    struct bt_gatt_notify_params params = {
        .attr = value, .data = body, .len = sizeof(*body),
    };
    /* Final gate immediately before SDK submission. Neutral is a release to
     * this OLD captured connection and intentionally ignores route/epoch.
     * The non-neutral active check balances only the getter's temporary ref;
     * it never consumes/retains the worker's captured_conn reference. Neutral
     * performs no active lookup. No NULL broadcast, retry or security request;
     * the owning main worker handles those policies.
     */
    if (!bbp_gamepad_is_neutral(body) &&
        !bbp_gamepad_ble_peer_current(captured_conn, expected_context_epoch)) {
        return -ESTALE;
    }
    return bt_gatt_notify_cb(captured_conn, &params);
}

/* Literal small expansion checked against the pinned gatt.h. A macro alias
 * such as OLD_BT_GATT_SERVICE_DEFINE does NOT save its expansion and recurses.
 * Append after ALL original attrs, preserving stock indices 5/9/13 verbatim.
 */
#undef BT_GATT_SERVICE_DEFINE
#define BT_GATT_SERVICE_DEFINE(_name, ...) \
    const struct bt_gatt_attr attr_##_name[] = { \
        __VA_ARGS__, \
        BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
                               BT_GATT_PERM_READ_ENCRYPT, bbp_gamepad_ble_read_report, NULL, NULL), \
        BT_GATT_CCC(bbp_gamepad_ble_ccc_changed, \
                    BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT), \
        BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, \
                           bbp_gamepad_ble_read_report_ref, NULL, bbp_gamepad_ble_report_ref) \
    }; \
    const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, _name) = BT_GATT_SERVICE(attr_##_name)

/* Last on purpose: do not rewrite official declarations or our SDK passthrough
 * calls. The subsequent stock hog.c definition now reads the extended map.
 */
#define bt_gatt_attr_read bbp_gamepad_ble_attr_read
