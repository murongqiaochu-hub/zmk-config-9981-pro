/* SPDX-License-Identifier: MIT
 * Independent Game Pad HIDS. Do not force-include into, or rewrite, stock HOG.
 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/atomic.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include "gamepad_runtime.h"

static const uint8_t bbp_gamepad_ble_descriptor[] = {BBP_GAMEPAD_DESCRIPTOR_BYTES};
_Static_assert(sizeof(bbp_gamepad_ble_descriptor) == 141U, "Independent Game Pad Map");
_Static_assert(sizeof(struct bbp_gamepad_report_body) == 9U, "BLE body excludes Report ID");
/* bcdHID=1.11, country=0, normally connectable; NOT remote wake capable. */
static const uint8_t bbp_gamepad_ble_hid_info[] = {0x11, 0x01, 0x00, 0x02};
static uint8_t bbp_gamepad_ble_report_ref[] = {BBP_GAMEPAD_REPORT_ID, 0x01};
static ATOMIC_DEFINE(bbp_gamepad_ble_suspended, CONFIG_BT_MAX_CONN);

static bool bbp_gamepad_ble_connected(const struct bt_conn *conn) {
    struct bt_conn_info info;
    return conn && bt_conn_get_info(conn, &info) == 0 && info.state == BT_CONN_STATE_CONNECTED;
}

static bool bbp_gamepad_ble_is_suspended(const struct bt_conn *conn) {
    return !conn || bt_conn_index(conn) >= CONFIG_BT_MAX_CONN ||
           atomic_test_bit(bbp_gamepad_ble_suspended, bt_conn_index(conn));
}

static bool bbp_gamepad_ble_route_matches(const struct bt_conn *conn) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    if (!conn || endpoint.transport != ZMK_TRANSPORT_BLE) { return false; }
    int profile = zmk_ble_active_profile_index();
    const bt_addr_le_t *dst = bt_conn_get_dst(conn);
    return profile >= 0 && endpoint.ble.profile_index == profile && dst &&
           zmk_ble_profile_index(dst) == profile;
}

/* Active getter owns a temporary reference; never consume worker ownership. */
static bool bbp_gamepad_ble_peer_current(struct bt_conn *conn, uint32_t epoch) {
    if (!bbp_gamepad_ble_connected(conn) || !bbp_gamepad_ble_route_matches(conn) ||
        !bbp_gamepad_context_valid(epoch)) { return false; }
    struct bt_conn *active = zmk_ble_active_profile_conn();
    bool matches = active == conn;
    if (active) { bt_conn_unref(active); }
    return matches && bbp_gamepad_ble_connected(conn) && bbp_gamepad_ble_route_matches(conn) &&
           bbp_gamepad_context_valid(epoch);
}

static ssize_t bbp_gamepad_ble_read_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, bbp_gamepad_ble_descriptor,
                             sizeof(bbp_gamepad_ble_descriptor));
}
static ssize_t bbp_gamepad_ble_read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                        void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, bbp_gamepad_ble_hid_info,
                             sizeof(bbp_gamepad_ble_hid_info));
}
static ssize_t bbp_gamepad_ble_read_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                          void *buf, uint16_t len, uint16_t offset) {
    struct bbp_gamepad_report_body body = bbp_gamepad_neutral();
    uint32_t epoch = bbp_gamepad_context_epoch();
    if (!bbp_gamepad_ble_is_suspended(conn) && bbp_gamepad_ble_peer_current(conn, epoch)) {
        bbp_gamepad_get_report(1U, &body);
        if (bbp_gamepad_ble_is_suspended(conn) || !bbp_gamepad_ble_peer_current(conn, epoch)) {
            body = bbp_gamepad_neutral();
        }
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &body, sizeof(body));
}
static ssize_t bbp_gamepad_ble_read_report_ref(struct bt_conn *conn,
                                              const struct bt_gatt_attr *attr, void *buf,
                                              uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(bbp_gamepad_ble_report_ref));
}

/* CP is local to this HIDS and each connection slot, never stock HOG's state.
 * WWR still validates malformed writes. Suspend is terminal (-ENOTSUP), not a
 * security/CCC failure. Exit invalidates the gamepad runtime to rearm neutral.
 */
static ssize_t bbp_gamepad_ble_write_cp(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                       const void *buf, uint16_t len, uint16_t offset,
                                       uint8_t flags) {
    (void)attr;
    (void)flags;
    if (offset != 0U) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    if (len != 1U) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN); }
    if (!buf || *(const uint8_t *)buf > 1U) {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    if (!bbp_gamepad_ble_connected(conn) || bt_conn_index(conn) >= CONFIG_BT_MAX_CONN) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    if (*(const uint8_t *)buf == 0U) {
        atomic_set_bit(bbp_gamepad_ble_suspended, bt_conn_index(conn));
        if (bbp_gamepad_ble_route_matches(conn)) { bbp_gamepad_transport_invalidate(); }
    } else {
        /* Cancel old reports BEFORE reopening the gate, then wake again AFTER
         * reopening: invalidate also wakes the worker, which may otherwise run
         * while still suspended and consume the neutral-rearm wake. Both calls
         * are gamepad-only; neither touches stock HOG or its control point.
         */
        bool current_route = bbp_gamepad_ble_route_matches(conn);
        atomic_set_bit(bbp_gamepad_ble_suspended, bt_conn_index(conn));
        if (current_route) { bbp_gamepad_transport_invalidate(); }
        atomic_clear_bit(bbp_gamepad_ble_suspended, bt_conn_index(conn));
        if (current_route) { bbp_gamepad_transport_invalidate(); }
    }
    return len;
}
static void bbp_gamepad_ble_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    (void)attr;
    (void)value;
    bbp_gamepad_transport_invalidate();
}

/* Official macro, separate primary service and CCC. zz sorts after stock HOG
 * in Zephyr iterable sections; integration must check final linked ordering.
 */
BT_GATT_SERVICE_DEFINE(zz_bbp_gamepad_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_ENCRYPT, bbp_gamepad_ble_read_info, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_ENCRYPT, bbp_gamepad_ble_read_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, bbp_gamepad_ble_write_cp, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, bbp_gamepad_ble_read_report, NULL, NULL),
    BT_GATT_CCC(bbp_gamepad_ble_ccc_changed,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT,
                       bbp_gamepad_ble_read_report_ref, NULL, bbp_gamepad_ble_report_ref)
);

static const struct bt_gatt_attr *
bbp_gamepad_ble_value_attr(const struct bt_gatt_service_static *service) {
    if (!service || !service->attrs || service->attr_count < 4U) { return NULL; }
    const struct bt_gatt_attr *value = &service->attrs[service->attr_count - 3U];
    const struct bt_gatt_attr *ref = &service->attrs[service->attr_count - 1U];
    if (value->read != bbp_gamepad_ble_read_report ||
        !(value->perm & BT_GATT_PERM_READ_ENCRYPT) ||
        ref->read != bbp_gamepad_ble_read_report_ref ||
        ref->user_data != bbp_gamepad_ble_report_ref) { return NULL; }
    return value;
}
int bbp_gamepad_ble_send(struct bt_conn *captured_conn,
                          const struct bbp_gamepad_report_body *body,
                          uint32_t expected_context_epoch) {
    if (!body) { return -EINVAL; }
    if (!bbp_gamepad_ble_connected(captured_conn)) { return -ENOTCONN; }
    if (bbp_gamepad_ble_is_suspended(captured_conn)) { return -ENOTSUP; }
    const struct bt_gatt_attr *value = bbp_gamepad_ble_value_attr(&zz_bbp_gamepad_svc);
    if (!value) { return -EINVAL; }
    if (!bt_gatt_is_subscribed(captured_conn, value, BT_GATT_CCC_NOTIFY)) { return -EAGAIN; }
    struct bt_gatt_notify_params params = {
        .attr = value, .data = body, .len = sizeof(*body),
    };
    /* Neutral release can target the old captured peer, but never a suspended
     * peer. No NULL broadcast, retry, security upgrade or reference retention.
     */
    if (!bbp_gamepad_is_neutral(body) &&
        !bbp_gamepad_ble_peer_current(captured_conn, expected_context_epoch)) {
        return bbp_gamepad_ble_is_suspended(captured_conn) ? -ENOTSUP : -ESTALE;
    }
    if (bbp_gamepad_ble_is_suspended(captured_conn)) { return -ENOTSUP; }
    return bt_gatt_notify_cb(captured_conn, &params);
}

static void bbp_gamepad_ble_connected_cb(struct bt_conn *conn, uint8_t err) {
    (void)err;
    if (conn && bt_conn_index(conn) < CONFIG_BT_MAX_CONN) {
        atomic_clear_bit(bbp_gamepad_ble_suspended, bt_conn_index(conn));
    }
}
static void bbp_gamepad_ble_disconnected_cb(struct bt_conn *conn, uint8_t reason) {
    (void)reason;
    if (conn && bt_conn_index(conn) < CONFIG_BT_MAX_CONN) {
        atomic_clear_bit(bbp_gamepad_ble_suspended, bt_conn_index(conn));
    }
}
BT_CONN_CB_DEFINE(bbp_gamepad_ble_conn_callbacks) = {
    .connected = bbp_gamepad_ble_connected_cb,
    .disconnected = bbp_gamepad_ble_disconnected_cb,
};
