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
#include "gamepad_ble_map.h"
#include "gamepad_runtime.h"

/* Serve ONE compact composite map in place of the stock one. Apple invalidated
 * the entire HID device when this read returned stock196+appendix337; the
 * replacement is 238 bytes, under the ATT MTU, with identical report bodies.
 * Every other HOG read keeps the SDK helper's offset/error semantics.
 */
static ssize_t bbp_gamepad_ble_attr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len, uint16_t offset,
                                       const void *value, uint16_t value_len) {
    if (value != (const void *)zmk_hid_report_desc || value_len != sizeof(zmk_hid_report_desc)) {
        return bt_gatt_attr_read(conn, attr, buf, len, offset, value, value_len);
    }
    /* Apple invalidates the device when a Game Pad collection shares the HID
     * service with a keyboard, so only the dedicated gamepad profile gets one.
     * Every other profile receives the vendor-ID4 map that iPad accepts.
     */
    const bool gamepad_profile = zmk_ble_active_profile_index() == BBP_GAMEPAD_PROFILE;
    const uint8_t *map = gamepad_profile ? bbp_map_gamepad : bbp_map_safe;
    const size_t map_len = gamepad_profile ? sizeof(bbp_map_gamepad) : sizeof(bbp_map_safe);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, map, map_len);
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
