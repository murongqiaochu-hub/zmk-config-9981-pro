/* SPDX-License-Identifier: MIT
 * Real neutral shim, not a mock runtime. No Zephyr or Bluetooth linkage.
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h"

int main(void) {
    const uint8_t neutral[9] = {0, 0, 8, 0, 0, 0, 0, 0, 0};
    assert(bbp_gamepad_context_epoch() == 0U);
    assert(bbp_gamepad_context_valid(0U));
    assert(!bbp_gamepad_context_valid(1U));
    assert(!bbp_gamepad_context_valid(UINT32_MAX));
    for (unsigned int i = 0; i <= UINT8_MAX; ++i) {
        struct bbp_gamepad_report_body body;
        memset(&body, 0xff, sizeof(body));
        bbp_gamepad_get_report((uint8_t)i, &body);
        assert(memcmp(&body, neutral, sizeof(body)) == 0);
        bbp_gamepad_transport_invalidate();
        assert(bbp_gamepad_context_epoch() == 0U);
        assert(bbp_gamepad_context_valid(0U));
        bbp_gamepad_get_report((uint8_t)i, NULL);
    }
    return 0;
}
