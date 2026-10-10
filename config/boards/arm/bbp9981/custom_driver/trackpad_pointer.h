/* Fractional cursor motion only; no filtering, buffering or synthetic inertia.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Exact rational form of the existing continuous gain:
 * 1.5 * (0.4 + brightness / 100), halved for physical Ctrl.
 * A common denominator preserves fractions without intermediate truncation.
 */
#define TP_POINTER_DENOMINATOR 400
#define TP_POINTER_IDLE_MS 50U

struct tp_pointer_axis {
    int32_t remainder;
    uint32_t last_motion_time;
    bool active;
};

struct tp_pointer_state {
    struct tp_pointer_axis x;
    struct tp_pointer_axis y;
    uint16_t gain;
    bool gain_valid;
};

static inline void tp_pointer_reset(struct tp_pointer_state *state) {
    *state = (struct tp_pointer_state){0};
}

static inline void tp_pointer_axis_expire(struct tp_pointer_axis *axis, uint32_t now) {
    if (axis->active && (uint32_t)(now - axis->last_motion_time) > TP_POINTER_IDLE_MS) {
        *axis = (struct tp_pointer_axis){0};
    }
}

static inline void tp_pointer_expire(struct tp_pointer_state *state, uint32_t now) {
    tp_pointer_axis_expire(&state->x, now);
    tp_pointer_axis_expire(&state->y, now);
}

static inline int16_t tp_pointer_axis_motion(struct tp_pointer_axis *axis, int8_t raw,
                                             uint16_t gain, uint32_t now) {
    tp_pointer_axis_expire(axis, now);
    /* Zero input never spends a remainder: stopping cannot generate movement.
     * Keep fractions through short gaps so sparse low-speed motion still works.
     */
    if (raw == 0) {
        return 0;
    }
    /* A reversal starts fresh on this axis, not a tail in the old direction. */
    if ((raw > 0 && axis->remainder < 0) || (raw < 0 && axis->remainder > 0)) {
        axis->remainder = 0;
    }
    int32_t scaled = (int32_t)raw * gain + axis->remainder;
    int32_t whole = scaled / TP_POINTER_DENOMINATOR; /* C truncates towards zero. */
    axis->remainder = scaled - whole * TP_POINTER_DENOMINATOR;
    axis->last_motion_time = now;
    axis->active = true;
    /* Sensor deltas are int8; even brightness=255 remains safely within int16. */
    return (int16_t)whole;
}

/* Run even on no-motion polls: an observed gain round-trip is a transition too. */
static inline void tp_pointer_prepare(struct tp_pointer_state *state, uint8_t brightness,
                                       bool slow) {
    uint16_t gain = 3U * (40U + brightness) * (slow ? 1U : 2U);
    if (!state->gain_valid || state->gain != gain) {
        tp_pointer_reset(state);
        state->gain = gain;
        state->gain_valid = true;
    }
}

static inline void tp_pointer_motion(struct tp_pointer_state *state, int8_t dx, int8_t dy,
                                      uint8_t brightness, bool slow, uint32_t now,
                                      int16_t *out_x, int16_t *out_y) {
    tp_pointer_prepare(state, brightness, slow);
    *out_x = tp_pointer_axis_motion(&state->x, dx, state->gain, now);
    *out_y = tp_pointer_axis_motion(&state->y, dy, state->gain, now);
}
