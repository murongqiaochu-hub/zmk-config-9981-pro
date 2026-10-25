/* SPDX-License-Identifier: MIT */
#pragma once
#include "gamepad_report.h"
struct bt_conn;

/* Epoch is a cancellation token, not host acknowledgement. Incremented on
 * layer/connection/protocol context changes; it never follows key-repeat rates. */
uint32_t bbp_gamepad_context_epoch(void);
bool bbp_gamepad_context_valid(uint32_t expected);
void bbp_gamepad_transport_invalidate(void);
/* Game layer owns the sensor; dx/right and dy/down are raw relative deltas.
 * Sampling never waits for USB/BLE. valid=false centres virtual axes only. */
bool bbp_gamepad_layer_active(void);
uint32_t bbp_gamepad_pointer_epoch(void);
void bbp_gamepad_trackpad_poll(int16_t dx, int16_t dy, bool valid, int64_t sample_time);
/* Always returns neutral outside MEDIA or when this transport is not selected.
 * BLE adapters additionally require read peer == captured active BLE peer. */
void bbp_gamepad_get_report(uint8_t transport, struct bbp_gamepad_report_body *out);

bool bbp_gamepad_usb_is_configured(void);
void bbp_gamepad_usb_bus_reset(void);
int bbp_gamepad_usb_send(const struct bbp_gamepad_report_body *body,
                         uint32_t expected_usb_epoch, uint32_t expected_context_epoch);
int bbp_gamepad_ble_send(struct bt_conn *captured_conn,
                         const struct bbp_gamepad_report_body *body,
                         uint32_t expected_context_epoch);
