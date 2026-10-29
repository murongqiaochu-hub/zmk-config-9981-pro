/* SPDX-License-Identifier: MIT
 * Explicit trial-only connection lifecycle. The gate itself has ONE instance
 * in the HOG TU. No cached conn refs, queue draining or notification here.
 * Existing gamepad_runtime.c still owns its normal lifecycle and frame refs.
 */
#include <stdbool.h>
#include <zephyr/bluetooth/conn.h>
#if !defined(BBP9981_COMPOSITE_DEVICE_TRIAL_LIFECYCLE) || BBP9981_COMPOSITE_DEVICE_TRIAL_LIFECYCLE != 1
#error "trial lifecycle must only be compiled by explicit trial staging"
#endif
extern void bbp_trial_connection_reset(struct bt_conn *conn, bool connected);
static void bbp_trial_connected(struct bt_conn *conn, uint8_t err) {
    bbp_trial_connection_reset(conn, err == 0);
}
static void bbp_trial_disconnected(struct bt_conn *conn, uint8_t reason) {
    (void)reason;
    bbp_trial_connection_reset(conn, false);
}
BT_CONN_CB_DEFINE(bbp_composite_trial_connection) = {
    .connected = bbp_trial_connected,
    .disconnected = bbp_trial_disconnected,
};
