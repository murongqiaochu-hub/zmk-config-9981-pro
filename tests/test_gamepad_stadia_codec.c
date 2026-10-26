/* SPDX-License-Identifier: MIT
 * Host-only wire codec vectors. Does NOT test device recognition or GATT.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../experiments/gamepad_protocol/stadia_codec.h"

int main(void) {
    uint8_t out[10];
    const uint8_t neutral[10] = {15,0,0,128,128,128,128,0,0,0};
    struct bbp_gamepad_report_body in = bbp_gamepad_neutral();
    assert(bbp_experiment_stadia_encode(&in, out));
    assert(!memcmp(out, neutral, sizeof(out)));
    /* Independent wire-bit vectors: input usage mask -> little endian payload. */
    const uint16_t input[] = {0x0001,0x0002,0x0008,0x0010,0x0040,0x0080,
                             0x0400,0x0800,0x1000,0x2000,0x4000};
    const uint16_t wire[] = {0x4000,0x2000,0x1000,0x0800,0x0400,0x0200,
                            0x0040,0x0020,0x0010,0x0100,0x0080};
    for (uint32_t mask = 0; mask < 65536U; ++mask) {
        in = bbp_gamepad_neutral();
        in.buttons_lo = (uint8_t)mask; in.buttons_hi = (uint8_t)(mask >> 8);
        uint16_t expected = 0;
        for (unsigned int i = 0; i < sizeof(input)/sizeof(input[0]); ++i) {
            if (mask & input[i]) { expected |= wire[i]; }
        }
        assert(bbp_experiment_stadia_encode(&in, out));
        assert(out[1] == (uint8_t)expected && out[2] == (uint8_t)(expected >> 8));
        assert(out[9] == 0 && (out[1] & 3U) == 0 && (out[2] & 128U) == 0);
    }
    in = bbp_gamepad_neutral();
    for (int axis = -127; axis <= 127; ++axis) {
        in.x = (int8_t)axis; in.y = (int8_t)-axis;
        in.z = (int8_t)axis; in.rz = (int8_t)-axis;
        assert(bbp_experiment_stadia_encode(&in, out));
        assert(out[3] == axis+128 && out[4] == 128-axis);
        assert(out[5] == axis+128 && out[6] == 128-axis);
    }
    in = bbp_gamepad_neutral();
    for (unsigned int hat = 0; hat <= 8; ++hat) {
        in.hat = (uint8_t)hat;
        assert(bbp_experiment_stadia_encode(&in, out));
        assert(out[0] == (hat == 8 ? 15 : hat));
    }
    for (unsigned int trigger = 0; trigger <= 255; ++trigger) {
        in.lt = (uint8_t)trigger; in.rt = (uint8_t)(255-trigger);
        assert(bbp_experiment_stadia_encode(&in, out));
        assert(out[7] == trigger && out[8] == 255-trigger);
        assert(!!(out[1] & 4U) == (trigger > 64));
        assert(!!(out[1] & 8U) == (255-trigger > 64));
    }
    assert(!bbp_experiment_stadia_encode(NULL, out));
    assert(!memcmp(out, neutral, sizeof(out)));
    assert(!bbp_experiment_stadia_encode(&in, NULL));
    in.hat = 9;
    assert(!bbp_experiment_stadia_encode(&in, out));
    assert(!memcmp(out, neutral, sizeof(out)));
    in = bbp_gamepad_neutral(); in.x = INT8_MIN;
    assert(!bbp_experiment_stadia_encode(&in, out));
    assert(!memcmp(out, neutral, sizeof(out)));
    puts("PASS experimental Stadia codec: 65536 button words, 255 signed-axis values, 9 hats, 256 triggers, invalid/neutral vectors; HOST ONLY");
    return 0;
}
