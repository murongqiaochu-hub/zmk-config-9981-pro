/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include "gamepad_runtime.h"

/* First iPad bisect: no BLE gamepad GATT/report/security side effects. */
int bbp_gamepad_ble_send(struct bt_conn *captured_conn,
                         const struct bbp_gamepad_report_body *body,
                         uint32_t expected_context_epoch) {
    (void)captured_conn;
    (void)body;
    (void)expected_context_epoch;
    return -ENOTSUP;
}
