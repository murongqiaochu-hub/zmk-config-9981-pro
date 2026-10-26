/* SPDX-License-Identifier: MIT
 * Experimental adapter for the EXISTING gamepad runtime worker/epoch contract.
 * Not linked by production CMake. Lifecycle functions must be wired to actual
 * connected/disconnected callbacks before use; no new queue or retry policy.
 * Callers of send own a live reference to captured_conn until it returns.
 */
#pragma once
#include <string.h>
#include <zephyr/kernel.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include "../../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h"
#include "pure_hids.h"

#ifndef CONFIG_BT_MAX_CONN
#error "CONFIG_BT_MAX_CONN is required for bounded per-connection state"
#endif
struct bbp_exp_peer_state {
    struct bt_conn *conn; /* Owns one reference until disconnected. */
    uint32_t generation;
    uint32_t registration; /* Changes only on registration, not suspend/CCC. */
    bool suspended;
};
struct bbp_exp_diagnostics {
    uint32_t counts[BBP_EXP_EVENT_COUNT];
    uint16_t last_offset[BBP_EXP_EVENT_COUNT];
    int last_result[BBP_EXP_EVENT_COUNT];
};
static struct k_spinlock bbp_exp_lock;
static struct bbp_exp_peer_state bbp_exp_peers[CONFIG_BT_MAX_CONN];
static struct bbp_exp_diagnostics bbp_exp_diag;
static uint32_t bbp_exp_generation; /* Lock-protected; unique across slots until wrap. */

void bbp_exp_observe(enum bbp_exp_event event, uint16_t offset, int result) {
    if ((unsigned int)event >= BBP_EXP_EVENT_COUNT) { return; }
    k_spinlock_key_t key = k_spin_lock(&bbp_exp_lock);
    if (bbp_exp_diag.counts[event] != UINT32_MAX) { bbp_exp_diag.counts[event]++; }
    bbp_exp_diag.last_offset[event] = offset;
    bbp_exp_diag.last_result[event] = result;
    k_spin_unlock(&bbp_exp_lock, key);
    /* An aggregate CCC transition cancels queued input, never authorizes send.
     * The sender still checks subscription on its exact captured connection.
     */
    if (event == BBP_EXP_CCC_AGGREGATE) { bbp_gamepad_transport_invalidate(); }
}
static void bbp_exp_read_diagnostics(struct bbp_exp_diagnostics *out) {
    if (!out) { return; }
    k_spinlock_key_t key = k_spin_lock(&bbp_exp_lock);
    *out = bbp_exp_diag;
    k_spin_unlock(&bbp_exp_lock, key);
}
static bool bbp_exp_connected_now(struct bt_conn *conn) {
    struct bt_conn_info info;
    return conn && !bt_conn_get_info(conn, &info) && info.state == BT_CONN_STATE_CONNECTED;
}
static int bbp_exp_transport_connected(struct bt_conn *conn, uint8_t error) {
    if (error || !bbp_exp_connected_now(conn)) { return -ENOTCONN; }
    struct bt_conn *owned = bt_conn_ref(conn);
    int ret = -ENOMEM;
    bool added = false;
    uint32_t registration = 0;
    k_spinlock_key_t key = k_spin_lock(&bbp_exp_lock);
    size_t free_slot = CONFIG_BT_MAX_CONN;
    for (size_t i=0; i<CONFIG_BT_MAX_CONN; ++i) {
        if (bbp_exp_peers[i].conn == conn) { ret = 0; break; }
        if (!bbp_exp_peers[i].conn && free_slot == CONFIG_BT_MAX_CONN) { free_slot = i; }
    }
    if (ret && free_slot < CONFIG_BT_MAX_CONN) {
        struct bbp_exp_peer_state *p = &bbp_exp_peers[free_slot];
        p->conn = owned; owned = NULL; p->generation = ++bbp_exp_generation; p->suspended = false;
        p->registration = registration = p->generation;
        ret = 0; added = true;
    }
    k_spin_unlock(&bbp_exp_lock, key);
    if (owned) { bt_conn_unref(owned); }
    if (added && !bbp_exp_connected_now(conn)) {
        /* A disconnect may have arrived between the first check and insertion.
         * Retire only this registration, never a newer incarnation at same ptr.
         */
        struct bt_conn *drop = NULL;
        key = k_spin_lock(&bbp_exp_lock);
        for (size_t i=0; i<CONFIG_BT_MAX_CONN; ++i) {
            struct bbp_exp_peer_state *p = &bbp_exp_peers[i];
            if (p->conn == conn && p->registration == registration) {
                drop=p->conn; p->conn=NULL; p->suspended=false;
                p->generation=++bbp_exp_generation; break;
            }
        }
        k_spin_unlock(&bbp_exp_lock, key);
        if (drop) { bt_conn_unref(drop); }
        ret = -ENOTCONN;
    }
    if (added) { bbp_gamepad_transport_invalidate(); }
    bbp_exp_observe(BBP_EXP_LIFECYCLE, 0U, ret);
    return ret;
}
static void bbp_exp_transport_disconnected(struct bt_conn *conn) {
    if (!conn) { return; }
    struct bt_conn *drop = NULL;
    k_spinlock_key_t key = k_spin_lock(&bbp_exp_lock);
    for (size_t i=0; i<CONFIG_BT_MAX_CONN; ++i) {
        struct bbp_exp_peer_state *p = &bbp_exp_peers[i];
        if (p->conn == conn) {
            drop = p->conn; p->conn = NULL; p->suspended = false; p->generation = ++bbp_exp_generation; break;
        }
    }
    k_spin_unlock(&bbp_exp_lock, key);
    if (drop) { bt_conn_unref(drop); bbp_gamepad_transport_invalidate(); }
    bbp_exp_observe(BBP_EXP_LIFECYCLE, 1U, drop ? 0 : -ENOTCONN);
}
/* No SDK calls under our lock. The caller owns its connection lifetime. */
static int bbp_exp_peer_ticket(struct bt_conn *conn, uint32_t *ticket) {
    if (!conn) { return -ENOTCONN; }
    int ret = -ENOTCONN;
    k_spinlock_key_t key = k_spin_lock(&bbp_exp_lock);
    for (size_t i=0; i<CONFIG_BT_MAX_CONN; ++i) {
        if (bbp_exp_peers[i].conn == conn) {
            *ticket = bbp_exp_peers[i].generation;
            ret = bbp_exp_peers[i].suspended ? -EAGAIN : 0; break;
        }
    }
    k_spin_unlock(&bbp_exp_lock, key);
    return ret;
}
static bool bbp_exp_ticket_valid(struct bt_conn *conn, uint32_t ticket) {
    uint32_t current;
    return bbp_exp_connected_now(conn) && !bbp_exp_peer_ticket(conn, &current) && current == ticket;
}
void bbp_exp_set_suspended(struct bt_conn *conn, bool suspended) {
    bool changed = false;
    k_spinlock_key_t key = k_spin_lock(&bbp_exp_lock);
    for (size_t i=0; conn && i<CONFIG_BT_MAX_CONN; ++i) {
        struct bbp_exp_peer_state *p = &bbp_exp_peers[i];
        if (p->conn == conn) {
            if (p->suspended != suspended) {
                p->suspended = suspended; p->generation = ++bbp_exp_generation; changed = true;
            }
            break;
        }
    }
    k_spin_unlock(&bbp_exp_lock, key);
    /* Resume requires fresh input, never resurrect a pre-suspend button. */
    if (changed) { bbp_gamepad_transport_invalidate(); }
}
int bbp_exp_subscription_write(struct bt_conn *conn, uint16_t value) {
    if (!conn || value > BT_GATT_CCC_NOTIFY) { return -EINVAL; }
    bool found = false;
    k_spinlock_key_t key = k_spin_lock(&bbp_exp_lock);
    for (size_t i=0; i<CONFIG_BT_MAX_CONN; ++i) {
        if (bbp_exp_peers[i].conn == conn) {
            bbp_exp_peers[i].generation = ++bbp_exp_generation; found = true; break;
        }
    }
    k_spin_unlock(&bbp_exp_lock, key);
    /* Even same-value writes conservatively cancel old input. This runs before
     * SDK commit; it does not claim subscription until is_subscribed says so.
     */
    if (found) { bbp_gamepad_transport_invalidate(); }
    return found ? 0 : -ENOTCONN;
}
static bool bbp_exp_route_matches(struct bt_conn *conn) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    if (endpoint.transport != ZMK_TRANSPORT_BLE) { return false; }
    int profile = zmk_ble_active_profile_index();
    const bt_addr_le_t *dst = bt_conn_get_dst(conn);
    if (profile < 0 || endpoint.ble.profile_index != profile || !dst ||
        zmk_ble_profile_index(dst) != profile) { return false; }
    return true;
}
static bool bbp_exp_current_peer(struct bt_conn *conn, uint32_t epoch) {
    if (!bbp_exp_connected_now(conn) || !bbp_gamepad_context_valid(epoch) ||
        !bbp_exp_route_matches(conn)) { return false; }
    struct bt_conn *active = zmk_ble_active_profile_conn();
    bool same = active == conn;
    if (active) { bt_conn_unref(active); }
    return same && bbp_gamepad_context_valid(epoch) && bbp_exp_connected_now(conn) &&
           bbp_exp_route_matches(conn);
}
bool bbp_exp_capture_report(struct bt_conn *conn, struct bbp_gamepad_report_body *out) {
    if (!out) { return false; }
    *out = bbp_gamepad_neutral();
    uint32_t ticket, epoch = bbp_gamepad_context_epoch();
    if (bbp_exp_peer_ticket(conn, &ticket) || !bbp_exp_current_peer(conn, epoch)) { return false; }
    bbp_gamepad_get_report(ZMK_TRANSPORT_BLE, out);
    if (!bbp_exp_ticket_valid(conn, ticket) || !bbp_exp_current_peer(conn, epoch)) {
        *out = bbp_gamepad_neutral(); return false;
    }
    return true;
}
static int bbp_exp_send_checked(struct bt_conn *conn, const struct bbp_gamepad_report_body *body,
                                uint32_t epoch) {
    if (!body) { return -EINVAL; }
    if (!bbp_exp_connected_now(conn)) { return -ENOTCONN; }
    uint32_t ticket;
    int ret = bbp_exp_peer_ticket(conn, &ticket);
    if (ret) { return ret; }
    const struct bt_gatt_attr *value = &bbp_exp_hids.attrs[8];
    if (value->read != bbp_exp_read_input || !(value->perm & BT_GATT_PERM_READ_ENCRYPT)) {
        return -EINVAL;
    }
    if (!bt_gatt_is_subscribed(conn, value, BT_GATT_CCC_NOTIFY)) { return -EAGAIN; }
    uint8_t encoded[BBP_EXP_INPUT_SIZE];
    if (!bbp_experiment_stadia_encode(body, encoded)) { return -EINVAL; }
    bool neutral = bbp_gamepad_is_neutral(body);
    /* Existing runtime owns OLD captured peer release; neutral ignores route
     * and runtime epoch, but never ignores disconnect, suspend or local epoch.
     */
    if ((!neutral && !bbp_exp_current_peer(conn, epoch)) || !bbp_exp_ticket_valid(conn, ticket)) {
        return -ESTALE;
    }
    if (!neutral && (!bbp_exp_route_matches(conn) || !bbp_gamepad_context_valid(epoch))) {
        return -ESTALE;
    }
    /* Cancellation after this last gate may leave ONE in-flight SDK submission.
     * Never hold the spinlock across the SDK allocation/send. The existing
     * worker must follow with neutral/cancellation; no atomic host ACK claim.
     */
    struct bt_gatt_notify_params params = {.attr=value, .data=encoded, .len=sizeof(encoded)};
    /* Pinned Zephyr copies payload into its ATT buffer before returning.
     * SDK submission is not a host ACK; no callbacks or NULL-peer broadcasts.
     */
    return bt_gatt_notify_cb(conn, &params);
}
static int bbp_exp_transport_send(struct bt_conn *captured_conn,
                                  const struct bbp_gamepad_report_body *body, uint32_t epoch) {
    int ret = bbp_exp_send_checked(captured_conn, body, epoch);
    bbp_exp_observe(BBP_EXP_SEND_RESULT, 0U, ret);
    return ret;
}
