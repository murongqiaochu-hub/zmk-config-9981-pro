/* SPDX-License-Identifier: MIT
 * Real production-runtime ABI/lifecycle wiring for the staged BLE-only profile.
 * No new worker, endpoint preference, key mapping or identity is introduced.
 */
#ifndef BBP9981_PURE_BLE_CANDIDATE
#error "pure_runtime.c requires the explicit isolated candidate build"
#endif
#include "pure_transport.h"

int bbp_gamepad_ble_send(struct bt_conn *captured_conn,
                         const struct bbp_gamepad_report_body *body,
                         uint32_t expected_context_epoch) {
    return bbp_exp_transport_send(captured_conn, body, expected_context_epoch);
}
static void bbp_exp_on_connected(struct bt_conn *conn, uint8_t error) {
    (void)bbp_exp_transport_connected(conn, error);
}
static void bbp_exp_on_disconnected(struct bt_conn *conn, uint8_t reason) {
    (void)reason;
    bbp_exp_transport_disconnected(conn);
}
BT_CONN_CB_DEFINE(bbp_exp_connection) = {
    .connected = bbp_exp_on_connected,
    .disconnected = bbp_exp_on_disconnected,
};

/* Export a bounded in-RAM snapshot for a future explicit diagnostic collector.
 * No log writer, USB console reader, settings write or persistent output here.
 */
void bbp9981_pure_gamepad_diagnostics(struct bbp_exp_diagnostics *out) {
    bbp_exp_read_diagnostics(out);
}
