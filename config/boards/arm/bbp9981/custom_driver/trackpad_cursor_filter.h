/* Raw mouse-cursor conditioning only; no SDK, I/O, clock, allocation or queue.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Stage 1 is the historical single-frame slip-release limiter:
 * four RAW Chebyshev speeds <= 2 counts/10ms arm a 6 counts/10ms cap.
 * The cap is never below ceil(raw_norm/2); components round symmetrically.
 * Only that frame is attenuated; RAW speed disarms the next fast frame.
 * This is a heuristic: an intentional fast start after slow motion can also
 * be attenuated. Discarded spike displacement is not queued or repaid.
 */
#define TP_CURSOR_SPIKE_HISTORY 4U
#define TP_CURSOR_SPIKE_PERIOD_MS 10U
#define TP_CURSOR_SPIKE_SLOW_SPEED 2U
#define TP_CURSOR_SPIKE_CAP_SPEED 6U
#define TP_CURSOR_IDLE_MS 50U

struct tp_cursor_spike {
    uint16_t speeds[TP_CURSOR_SPIKE_HISTORY];
    uint8_t count;
    uint8_t index;
    uint32_t last_sample;
    bool valid;
};

/* Stage 2 is a per-axis, one-RAW-count Schmitt band, not a moving average:
 *   acc += raw; if |acc| > 1, emit acc-sign(acc), retain sign(acc).
 * Otherwise emit zero and retain acc. Zero input NEVER spends a remainder.
 * Each axis expires independently after >50ms without nonzero axis input.
 * A reversal may first cancel the old one-count remainder, then establish
 * the new one-count remainder: +1,-1,-1,-1 emits 0,0,0,-1. There is never
 * old-direction output. This intentional boundary dead band suppresses +/-1
 * oscillation but is NOT lossless path-length preservation. During a normal
 * uninterrupted slide, the signed input-output sum is exactly the retained
 * remainder (at most one count/axis), not a recurring loss per packet.
 */
struct tp_cursor_jitter_axis {
    int8_t remainder; /* Always -1, 0 or +1 after reset/apply. */
    uint32_t last_motion_time;
    bool active;
};

struct tp_cursor_jitter {
    struct tp_cursor_jitter_axis x;
    struct tp_cursor_jitter_axis y;
};

struct tp_cursor_filter {
    struct tp_cursor_spike spike;
    struct tp_cursor_jitter jitter;
};

static inline void tp_cursor_spike_reset(struct tp_cursor_spike *state) {
    *state = (struct tp_cursor_spike){0};
}

static inline void tp_cursor_jitter_reset(struct tp_cursor_jitter *state) {
    *state = (struct tp_cursor_jitter){0};
}

/* Zero initialization is equivalent to reset. Caller resets on mouse-mode /
 * gain epochs and sensor faults; this pure core cannot observe those events.
 */
static inline void tp_cursor_filter_reset(struct tp_cursor_filter *state) {
    *state = (struct tp_cursor_filter){0};
}

static inline uint32_t tp_cursor_abs(int16_t value) {
    return value < 0 ? (uint32_t)(-(int32_t)value) : (uint32_t)value;
}

/* Internal scaler: norm > cap, norm > 0, and value came from an int8 sample. */
static inline int16_t tp_cursor_spike_component(int16_t value, uint32_t cap,
                                               uint32_t norm) {
    uint32_t scaled = (tp_cursor_abs(value) * cap + norm / 2U) / norm;
    return (int16_t)(value < 0 ? -(int32_t)scaled : (int32_t)scaled);
}

/* Independently callable stage for tests. Inputs are unscaled signed-8 RAW
 * samples; outputs remain signed-8 representable even though the API is int16.
 * Equal timestamps or unsigned elapsed >50ms reset ONLY the spike history;
 * the current raw sample passes this stage and seeds a new history at 10ms.
 * Unsigned subtraction accepts natural uint32 wrap and rejects backward /
 * half-range anomalies as a gap, with no signed-time comparison or lockout.
 */
static inline bool tp_cursor_spike_apply(struct tp_cursor_spike *state,
                                         int8_t dx, int8_t dy, uint32_t now,
                                         int16_t *out_x, int16_t *out_y) {
    uint32_t dt = state->valid ? (uint32_t)(now - state->last_sample)
                               : TP_CURSOR_SPIKE_PERIOD_MS;
    if (!state->valid || dt == 0U || dt > TP_CURSOR_IDLE_MS) {
        tp_cursor_spike_reset(state);
        dt = TP_CURSOR_SPIKE_PERIOD_MS;
    }
    uint32_t ax = tp_cursor_abs(dx), ay = tp_cursor_abs(dy);
    uint32_t norm = ax > ay ? ax : ay;
    uint32_t speed = (norm * TP_CURSOR_SPIKE_PERIOD_MS + dt - 1U) / dt;
    bool slow_history = state->count == TP_CURSOR_SPIKE_HISTORY;
    for (unsigned i = 0; i < state->count; ++i) {
        if (state->speeds[i] > TP_CURSOR_SPIKE_SLOW_SPEED) {
            slow_history = false;
        }
    }
    uint32_t cap = (TP_CURSOR_SPIKE_CAP_SPEED * dt + TP_CURSOR_SPIKE_PERIOD_MS - 1U)
                   / TP_CURSOR_SPIKE_PERIOD_MS;
    uint32_t half = (norm + 1U) / 2U;
    if (cap < half) {
        cap = half;
    }
    bool clipped = slow_history && norm > cap;
    *out_x = clipped ? tp_cursor_spike_component(dx, cap, norm) : (int16_t)dx;
    *out_y = clipped ? tp_cursor_spike_component(dy, cap, norm) : (int16_t)dy;

    /* Store RAW speed, never limited speed. For int8 input and dt >= 1 this
     * is at most 1280, so conversion to uint16 is exact. Zero samples count
     * as slow samples, just as in the historical limiter.
     */
    state->speeds[state->index] = (uint16_t)speed;
    state->index = (uint8_t)((state->index + 1U) % TP_CURSOR_SPIKE_HISTORY);
    if (state->count < TP_CURSOR_SPIKE_HISTORY) {
        ++state->count;
    }
    state->last_sample = now;
    state->valid = true;
    return clipped;
}

static inline void tp_cursor_jitter_axis_expire(struct tp_cursor_jitter_axis *axis,
                                                uint32_t now) {
    if (axis->active && (uint32_t)(now - axis->last_motion_time) > TP_CURSOR_IDLE_MS) {
        *axis = (struct tp_cursor_jitter_axis){0};
    }
}

static inline void tp_cursor_jitter_expire(struct tp_cursor_jitter *state, uint32_t now) {
    tp_cursor_jitter_axis_expire(&state->x, now);
    tp_cursor_jitter_axis_expire(&state->y, now);
}

static inline int16_t tp_cursor_jitter_axis_apply(struct tp_cursor_jitter_axis *axis,
                                                 int8_t raw, uint32_t now) {
    tp_cursor_jitter_axis_expire(axis, now);
    if (raw == 0) {
        return 0;
    }
    int16_t acc = (int16_t)((int16_t)axis->remainder + (int16_t)raw);
    int16_t output = 0;
    if (acc > 1) {
        output = (int16_t)(acc - 1);
        acc = 1;
    } else if (acc < -1) {
        output = (int16_t)(acc + 1);
        acc = -1;
    }
    axis->remainder = (int8_t)acc;
    axis->last_motion_time = now;
    axis->active = true;
    /* acc before emission is [-129,128]; removing its retained sign leaves
     * [-128,127]. With remainder in [-1,1], output cannot reverse raw, exceed
     * raw magnitude, or create motion on a zero-input axis.
     */
    return output;
}

/* Independently callable stage; each input is an int8 raw/limited component. */
static inline void tp_cursor_jitter_apply(struct tp_cursor_jitter *state,
                                          int8_t dx, int8_t dy, uint32_t now,
                                          int16_t *out_x, int16_t *out_y) {
    *out_x = tp_cursor_jitter_axis_apply(&state->x, dx, now);
    *out_y = tp_cursor_jitter_axis_apply(&state->y, dy, now);
}

/* Optional no-sample/idle maintenance. Does not feed synthetic zero samples
 * into the spike history, emit motion or refresh either last-motion time.
 * Calling apply with (0,0) is also safe, but records a real zero sample.
 */
static inline void tp_cursor_filter_expire(struct tp_cursor_filter *state, uint32_t now) {
    if (state->spike.valid &&
        (uint32_t)(now - state->spike.last_sample) > TP_CURSOR_IDLE_MS) {
        tp_cursor_spike_reset(&state->spike);
    }
    tp_cursor_jitter_expire(&state->jitter, now);
}

/* Explicit stage switches are for independent verification/experiments, not
 * Kconfig. A disabled stage is reset on that call, so re-enabling cannot
 * resurrect stale history. The production entry below always runs both.
 */
static inline void tp_cursor_filter_apply_stages(struct tp_cursor_filter *state,
                                                 int8_t dx, int8_t dy, uint32_t now,
                                                 bool spike_enabled, bool jitter_enabled,
                                                 int16_t *out_x, int16_t *out_y) {
    int16_t x = dx, y = dy;
    if (spike_enabled) {
        (void)tp_cursor_spike_apply(&state->spike, dx, dy, now, &x, &y);
    } else {
        tp_cursor_spike_reset(&state->spike);
    }
    if (jitter_enabled) {
        /* Stage 1 only reduces magnitude; these conversions are exact. */
        tp_cursor_jitter_apply(&state->jitter, (int8_t)x, (int8_t)y, now, out_x, out_y);
    } else {
        tp_cursor_jitter_reset(&state->jitter);
        *out_x = x;
        *out_y = y;
    }
}

/* Caller supplies initialized state, valid distinct outputs and uint32 ms.
 * Order is RAW -> one-frame spike limiter -> per-axis Schmitt -> old pointer
 * gain/output. Keep the original raw path for tap criteria; this filter does
 * not modify that path and is not intended for wheel or gamepad processing.
 * At most half a component is discarded by stage 1; the subsequent one-count
 * Schmitt boundary is additional. No claim of complete displacement/path
 * conservation is made for spikes, reversals, expiry or explicit resets.
 */
static inline void tp_cursor_filter_apply(struct tp_cursor_filter *state,
                                          int8_t dx, int8_t dy, uint32_t now,
                                          int16_t *out_x, int16_t *out_y) {
    tp_cursor_filter_apply_stages(state, dx, dy, now, true, true, out_x, out_y);
}
