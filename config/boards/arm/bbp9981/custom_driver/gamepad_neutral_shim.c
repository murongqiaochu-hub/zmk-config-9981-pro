/* SPDX-License-Identifier: MIT
 * Enumeration-only isolation seam, NOT a playable BLE gamepad runtime.
 * No input subscriptions, workers, notifications or security requests.
 * Original extension/report headers are retained byte-for-byte; their dynamic
 * runtime is deliberately absent. This experiment is not a mathematical complement.
 */
#include "gamepad_runtime.h"

uint32_t bbp_gamepad_context_epoch(void) { return 0U; }
bool bbp_gamepad_context_valid(uint32_t expected) { return expected == 0U; }
void bbp_gamepad_transport_invalidate(void) {}
void bbp_gamepad_get_report(uint8_t transport, struct bbp_gamepad_report_body *out) {
    (void)transport;
    if (out) { *out = bbp_gamepad_neutral(); }
}
