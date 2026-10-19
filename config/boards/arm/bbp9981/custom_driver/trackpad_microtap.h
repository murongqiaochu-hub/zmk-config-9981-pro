/* SPDX-License-Identifier: Apache-2.0
 * Opt-in screen-space motion-pulse click heuristic (pure C11, no allocation).
 *
 * A candidate qualifies when a valid raw motion burst spans at most 160ms and
 * produces a 2..50px Euclidean NET cursor displacement through the same normal/
 * slow pointer gain, followed by >=120ms of valid zero-motion polls. Historical
 * replay suggested a much lower floor than the 20px reference; it is a heuristic,
 * not a measured accuracy threshold. A qualifying burst emits one click and
 * consumes its buffered movement; out-of-band motion is replayed unchanged.
 * This is a motion heuristic, NOT verified physical contact detection; short
 * slides in the qualifying size/time window can still become clicks.
 *
 * Frames are unscaled signed-8-bit sensor counts stored as int16. A shadow
 * pointer state computes eligibility in screen pixels; rejection replays the
 * original raw frames and timestamps through the unchanged real pointer state.
 * Never adjust real pointer remainders or synthesize compensation.
 * Zero motion, missing data, faults and reset never create clicks.
 * Call reset before first use. The caller owns and serializes each state.
 * enabled is supplied explicitly on EVERY step; blocked suppresses clicks.
 * valid=false means unknown/error, NOT a fabricated valid (0, 0) sample.
 *
 * At most eight frames remain pending; the 16-frame result ABI stays unchanged.
 * A ninth frame or span>160ms releases the candidate as ordinary movement.
 * After rejection OR an accepted click, require >=200ms quiet before rearming;
 * bounce/continuous movement extends this guard rather than creating more taps.
 * Accepted automatic click decisions are separated by >=500ms. Suppressed motion
 * is replayed, never queued for a later click. Quiet is >=120ms since last motion,
 * NOT contact-up: no contact protocol has been verified.
 *
 * A new nonzero frame first checks whether the OLD candidate is already quiet.
 * Therefore a result can have click=true while frames[] contains NEW movement.
 * Do not reset merely because click=true: that would remove the rearm guard.
 * Handle frames[0..count) and click independently. When both are present, the
 * old click precedes ALL returned new movement; enqueue click before frames.
 * At most one click per step.
 *
 * Raw path/offset limits are corruption guards, not the screen-space click rule.
 * Candidate age >360ms, ninth frame, span >160ms, raw L1 path >8192 or ANY
 * cumulative Linf offset >4096 reject immediately. A native event arriving after
 * an auto was emitted cannot undo it; native guards only prevent future autos.
 * No late/retroactive click. Quiet 120ms exceeds the 100ms maximum poll gap, so
 * intermediate valid zero polls are required; silence/no calls cannot qualify.
 *
 * uint32 milliseconds use modular subtraction, including natural rollover.
 * Poll gaps >100ms or backwards/half-range clock jumps flush without clicking.
 * Equal timestamps are allowed but cannot advance quiet; frame count is bounded.
 * Invalid samples/clock faults flush and require quiet from the fault's time;
 * valid disabled/blocked motion is passed unchanged. Reset deliberately drops
 * pending motion at hard scroll/endpoint/mode boundaries. State pointers must be
 * non-NULL; do not mutate state fields outside reset.
 */
#ifndef TRACKPAD_MICROTAP_H
#define TRACKPAD_MICROTAP_H

#include <stdbool.h>
#include <stdint.h>
#include "trackpad_pointer.h"

#define TP_MICROTAP_QUIET_MS 120U
#define TP_MICROTAP_NAV_QUIET_MS 200U
#define TP_MICROTAP_MIN_FRAMES 1U
#define TP_MICROTAP_MAX_FRAMES 8U
#define TP_MICROTAP_MIN_CURSOR_DISTANCE_PX 2U
#define TP_MICROTAP_MAX_CURSOR_DISTANCE_PX 50U
#define TP_MICROTAP_MIN_MOTION_SPAN_MS 0U
#define TP_MICROTAP_MAX_CANDIDATE_MS 360U
#define TP_MICROTAP_MAX_MOTION_SPAN_MS 160U
/* Wide guard above every possible eight-frame signed8 sensor burst:
 * path<=2048, accumulated single-axis offset<=1024. */
#define TP_MICROTAP_MAX_PATH 8192U
#define TP_MICROTAP_MAX_OFFSET 4096U
#define TP_MICROTAP_CLICK_INTERVAL_MS 500U
#define TP_MICROTAP_MAX_POLL_GAP_MS 100U
#define TP_MICROTAP_FRAME_CAPACITY 16U
#define TP_MICROTAP_MAX_PENDING (TP_MICROTAP_FRAME_CAPACITY - 1U)

struct tp_microtap_frame {
    uint32_t at_ms;
    int16_t dx;
    int16_t dy;
};

struct tp_microtap_result {
    struct tp_microtap_frame frames[TP_MICROTAP_FRAME_CAPACITY];
    uint8_t count;
    bool click;
};

enum tp_microtap_mode {
    TP_MICROTAP_IDLE = 0,
    TP_MICROTAP_CANDIDATE,
    TP_MICROTAP_PASSTHROUGH,
};

struct tp_microtap_state {
    struct tp_microtap_frame frames[TP_MICROTAP_MAX_PENDING];
    enum tp_microtap_mode mode;
    uint8_t count;
    bool have_time;
    bool click_guard_active;
    uint32_t click_guard_ms;
    uint32_t last_poll_ms;
    uint32_t first_ms;
    uint32_t last_motion_ms;
    int32_t sum_x;
    int32_t sum_y;
    uint32_t path;
    uint32_t max_offset;
    struct tp_pointer_state measure_pointer;
    int32_t cursor_sum_x;
    int32_t cursor_sum_y;
};

static inline void tp_microtap_reset(struct tp_microtap_state *s) {
    *s = (struct tp_microtap_state){0};
}

/* Lets the caller defer idle expiry while original-time raw motion is pending. */
static inline bool tp_microtap_pending(const struct tp_microtap_state *s) {
    return s->count != 0U;
}

static inline uint32_t tp_microtap_magnitude(int32_t value) {
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static inline uint64_t tp_microtap_cursor_distance_sq(const struct tp_microtap_state *s) {
    const int64_t x = s->cursor_sum_x;
    const int64_t y = s->cursor_sum_y;
    return (uint64_t)(x * x) + (uint64_t)(y * y);
}

static inline bool tp_microtap_cursor_distance_in_range(const struct tp_microtap_state *s) {
    const uint64_t distance_sq = tp_microtap_cursor_distance_sq(s);
    const uint64_t min_sq = (uint64_t)TP_MICROTAP_MIN_CURSOR_DISTANCE_PX *
                            TP_MICROTAP_MIN_CURSOR_DISTANCE_PX;
    const uint64_t max_sq = (uint64_t)TP_MICROTAP_MAX_CURSOR_DISTANCE_PX *
                            TP_MICROTAP_MAX_CURSOR_DISTANCE_PX;
    return distance_sq >= min_sq && distance_sq <= max_sq;
}

static inline void tp_microtap_clear_candidate(struct tp_microtap_state *s) {
    s->count = 0U;
    s->first_ms = 0U;
    s->sum_x = 0;
    s->sum_y = 0;
    s->path = 0U;
    s->max_offset = 0U;
    tp_pointer_reset(&s->measure_pointer);
    s->cursor_sum_x = 0;
    s->cursor_sum_y = 0;
}

/* Internal invariant: pending count<=15. After a quiet underfilled candidate
 * is flushed, any new candidate has zero buffered frames on a second flush. */
static inline void tp_microtap_flush(struct tp_microtap_state *s,
                                     struct tp_microtap_result *out) {
    for (uint8_t i = 0U; i < s->count; ++i) {
        out->frames[out->count++] = s->frames[i];
    }
    tp_microtap_clear_candidate(s);
}

static inline struct tp_microtap_result tp_microtap_step(struct tp_microtap_state *s,
                                                          uint32_t now,
                                                          int16_t dx, int16_t dy,
                                                          bool slow_pointer, bool valid,
                                                          bool enabled, bool blocked) {
    struct tp_microtap_result out = {0};
    const struct tp_microtap_frame frame = {.at_ms = now, .dx = dx, .dy = dy};
    const bool motion = valid && (dx != 0 || dy != 0);
    const uint32_t poll_gap = (uint32_t)(now - s->last_poll_ms);
    const bool bad_time = s->have_time &&
                         (poll_gap > TP_MICROTAP_MAX_POLL_GAP_MS ||
                          poll_gap >= UINT32_C(0x80000000));
    s->have_time = true;
    s->last_poll_ms = now;

    /* Cancellation precedes quiet evaluation: faults never complete a click. */
    if (!valid || !enabled || blocked || bad_time) {
        const bool candidate = s->mode == TP_MICROTAP_CANDIDATE;
        if (bad_time) {
            s->click_guard_active = true;
            s->click_guard_ms = now; /* A clock fault cannot bypass the guard. */
        }
        tp_microtap_flush(s, &out);
        if (motion) {
            out.frames[out.count++] = frame;
        }
        if (!valid || bad_time || motion) {
            s->mode = TP_MICROTAP_PASSTHROUGH;
            s->last_motion_ms = now;
        } else if (candidate) {
            /* A valid zero cancellation preserves the last real motion time. */
            s->mode = TP_MICROTAP_PASSTHROUGH;
        }
        return out;
    }

    if (s->click_guard_active &&
        (uint32_t)(now - s->click_guard_ms) >= TP_MICROTAP_CLICK_INTERVAL_MS) {
        s->click_guard_active = false; /* Expiry alone never creates a click. */
    }
    if (s->mode == TP_MICROTAP_PASSTHROUGH &&
        (uint32_t)(now - s->last_motion_ms) >= TP_MICROTAP_NAV_QUIET_MS) {
        s->mode = TP_MICROTAP_IDLE;
    }

    if (s->mode == TP_MICROTAP_CANDIDATE) {
        if ((uint32_t)(now - s->first_ms) > TP_MICROTAP_MAX_CANDIDATE_MS) {
            tp_microtap_flush(s, &out);
            s->mode = TP_MICROTAP_PASSTHROUGH;
        } else if ((uint32_t)(now - s->last_motion_ms) >= TP_MICROTAP_QUIET_MS) {
            /* Only a short screen-space displacement in the requested band is
             * click-like; smaller drift and larger swipes remain pointer motion. */
            const bool admitted = s->count >= TP_MICROTAP_MIN_FRAMES &&
                                  s->count <= TP_MICROTAP_MAX_FRAMES &&
                                  tp_microtap_cursor_distance_in_range(s) &&
                                  !s->click_guard_active;
            if (admitted) {
                tp_microtap_clear_candidate(s);
                out.click = true;
                s->click_guard_active = true;
                s->click_guard_ms = now;
                /* Motion during rearm is ordinary movement, not queued taps.
                 * Continuous bounce extends the existing 200ms quiet guard. */
                s->mode = TP_MICROTAP_PASSTHROUGH;
            } else {
                tp_microtap_flush(s, &out);
                s->mode = TP_MICROTAP_PASSTHROUGH;
            }
        }
    }

    if (s->mode == TP_MICROTAP_PASSTHROUGH) {
        if (motion) {
            out.frames[out.count++] = frame;
            s->last_motion_ms = now;
        }
        return out;
    }
    if (!motion) {
        return out;
    }

    if (s->mode == TP_MICROTAP_IDLE) {
        tp_microtap_clear_candidate(s);
        s->mode = TP_MICROTAP_CANDIDATE;
        s->first_ms = now;
    }

    /* Live candidates have |sum_x/y|<=4096 and path<=8192; even int16 extremes
     * added here remain safely within int32/uint32, before any narrowing. */
    const int32_t x = s->sum_x + (int32_t)dx;
    const int32_t y = s->sum_y + (int32_t)dy;
    const uint32_t ax = tp_microtap_magnitude(x);
    const uint32_t ay = tp_microtap_magnitude(y);
    const uint32_t offset = ax > ay ? ax : ay;
    const uint32_t path = s->path + tp_microtap_magnitude((int32_t)dx) +
                          tp_microtap_magnitude((int32_t)dy);
    if (dx < INT8_MIN || dx > INT8_MAX || dy < INT8_MIN || dy > INT8_MAX ||
        s->count >= TP_MICROTAP_MAX_FRAMES || s->count >= TP_MICROTAP_MAX_PENDING ||
        (uint32_t)(now - s->first_ms) > TP_MICROTAP_MAX_MOTION_SPAN_MS ||
        path > TP_MICROTAP_MAX_PATH || offset > TP_MICROTAP_MAX_OFFSET) {
        tp_microtap_flush(s, &out);
        out.frames[out.count++] = frame;
        s->mode = TP_MICROTAP_PASSTHROUGH;
        s->last_motion_ms = now;
        return out;
    }

    int16_t cursor_dx = 0, cursor_dy = 0;
    tp_pointer_motion(&s->measure_pointer, (int8_t)dx, (int8_t)dy,
                      slow_pointer, now, &cursor_dx, &cursor_dy);
    const int32_t cursor_x = s->cursor_sum_x + cursor_dx;
    const int32_t cursor_y = s->cursor_sum_y + cursor_dy;
    const int64_t cursor_x64 = cursor_x;
    const int64_t cursor_y64 = cursor_y;
    const uint64_t max_cursor_sq = (uint64_t)TP_MICROTAP_MAX_CURSOR_DISTANCE_PX *
                                   TP_MICROTAP_MAX_CURSOR_DISTANCE_PX;
    if ((uint64_t)(cursor_x64 * cursor_x64) +
        (uint64_t)(cursor_y64 * cursor_y64) > max_cursor_sq) {
        /* Once this burst moves beyond 50 screen pixels, it is a swipe. Replay
         * immediately so later reversal cannot turn that swipe into a click. */
        tp_microtap_flush(s, &out);
        out.frames[out.count++] = frame;
        s->mode = TP_MICROTAP_PASSTHROUGH;
        s->last_motion_ms = now;
        return out;
    }

    s->frames[s->count++] = frame;
    s->sum_x = x;
    s->sum_y = y;
    s->path = path;
    s->cursor_sum_x = cursor_x;
    s->cursor_sum_y = cursor_y;
    if (offset > s->max_offset) {
        s->max_offset = offset;
    }
    s->last_motion_ms = now;
    return out;
}

#endif /* TRACKPAD_MICROTAP_H */
