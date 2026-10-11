/* Experimental one-sample slip-release limiter; no queue or synthetic motion.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TP_SPIKE_HISTORY 4U
#define TP_SPIKE_PERIOD_MS 10U
#define TP_SPIKE_SLOW_SPEED 2U
#define TP_SPIKE_CAP_SPEED 6U
#define TP_SPIKE_MAX_GAP_MS 50U

struct tp_spike_state {
    uint16_t speeds[TP_SPIKE_HISTORY];
    uint8_t count, index;
    uint32_t last_sample;
    bool valid;
};
static inline void tp_spike_reset(struct tp_spike_state *state) {
    *state = (struct tp_spike_state){0};
}
static inline uint32_t tp_spike_abs(int16_t value) {
    return value < 0 ? (uint32_t)(-(int32_t)value) : (uint32_t)value;
}
static inline int16_t tp_spike_component(int16_t value, uint32_t cap, uint32_t norm) {
    uint32_t scaled = (tp_spike_abs(value) * cap + norm / 2U) / norm;
    return value < 0 ? -(int32_t)scaled : (int32_t)scaled;
}
static inline bool tp_spike_apply(struct tp_spike_state *state, int16_t *x, int16_t *y,
                                  uint32_t now) {
    uint32_t dt = state->valid ? (uint32_t)(now - state->last_sample) : TP_SPIKE_PERIOD_MS;
    if (!state->valid || dt == 0U || dt > TP_SPIKE_MAX_GAP_MS) {
        tp_spike_reset(state);
        dt = TP_SPIKE_PERIOD_MS;
    }
    uint32_t ax = tp_spike_abs(*x), ay = tp_spike_abs(*y);
    uint32_t norm = ax > ay ? ax : ay;
    uint32_t speed = (norm * TP_SPIKE_PERIOD_MS + dt - 1U) / dt;
    bool slow_history = state->count == TP_SPIKE_HISTORY;
    for (unsigned i = 0; i < state->count; ++i) {
        if (state->speeds[i] > TP_SPIKE_SLOW_SPEED) { slow_history = false; }
    }
    uint32_t cap = (TP_SPIKE_CAP_SPEED * dt + TP_SPIKE_PERIOD_MS - 1U) / TP_SPIKE_PERIOD_MS;
    uint32_t half = (norm + 1U) / 2U;
    if (cap < half) { cap = half; } /* Never discard more than half of either axis. */
    bool clipped = slow_history && norm > cap;
    if (clipped) {
        *x = tp_spike_component(*x, cap, norm);
        *y = tp_spike_component(*y, cap, norm);
    }
    /* Store RAW speed, not the limited speed: subsequent sustained fast motion bypasses. */
    state->speeds[state->index] = speed > UINT16_MAX ? UINT16_MAX : (uint16_t)speed;
    state->index = (state->index + 1U) % TP_SPIKE_HISTORY;
    if (state->count < TP_SPIKE_HISTORY) { state->count++; }
    state->last_sample = now;
    state->valid = true;
    return clipped;
}
