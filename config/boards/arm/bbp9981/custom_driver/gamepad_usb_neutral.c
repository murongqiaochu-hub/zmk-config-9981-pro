/* SPDX-License-Identifier: MIT
 * USB enumeration isolation only, not a playable gamepad/runtime.
 * No input listeners, workers, settings or proactive transport writes.
 */
#include "gamepad_runtime.h"

uint32_t bbp_gamepad_context_epoch(void) { return 0U; }
bool bbp_gamepad_context_valid(uint32_t expected) {
    (void)expected;
    return false;
}
void bbp_gamepad_transport_invalidate(void) {}
void bbp_gamepad_get_report(uint8_t transport, struct bbp_gamepad_report_body *out) {
    (void)transport;
    if (out) { *out = bbp_gamepad_neutral(); }
}
