/* SPDX-License-Identifier: Apache-2.0
 * Opt-in raw-motion/quiet click heuristic (pure C11, no allocation).
 *
 * This does NOT detect physical contact or promise recognition of light taps.
 * Only a conservative short micro-swipe can qualify: >=3 valid nonzero frames,
 * >=3 Manhattan raw counts and >=15ms first-to-last motion span. On quiet,
 * weaker candidates replay as movement, not clicks. Zero motion, missing data,
 * faults and reset never create clicks.
 * Call reset before first use. The caller owns and serializes each state.
 * enabled is supplied explicitly on EVERY step; blocked suppresses clicks.
 * valid=false means unknown/error, NOT a fabricated valid (0, 0) sample.
 *
 * Frames are unscaled int16 relative counts. Only nonzero valid motion frames
 * are buffered/returned; zero polls only advance timers. On rejection, replay
 * ALL returned frames, in order, using their original at_ms in the unchanged
 * pointer pipeline. Never adjust pointer remainders or synthesize compensation.
 * A click deliberately discards ONLY the old candidate's buffered movement.
 *
 * At most 15 frames remain pending, reserving the 16th result slot for the
 * frame that triggers rejection. Capacity pressure fails safe to movement,
 * including the triggering frame; it never truncates or reorders movement.
 * Once rejected/passed through, a movement run stays passthrough until a
 * >=30ms quiet interval; its small tail cannot become a new click candidate.
 * There is no click cooldown: separated pulses can click on consecutive steps.
 *
 * A new nonzero frame first checks whether the OLD candidate is already quiet.
 * Therefore a result can have click=true while the state contains a NEW
 * candidate, or while frames[] contains rejected NEW movement. Do not reset
 * merely because click=true; that would silently discard the new candidate.
 * Handle frames[0..count) and click independently. When both are present, the
 * old click precedes ALL returned new movement; enqueue click before frames.
 * At most one click per step.
 *
 * Limits are inclusive; candidate age >100ms, motion span >80ms, traveled
 * Manhattan path >16 or ANY cumulative Linf offset >8 reject immediately.
 * Quiet is >=30ms since the last nonzero frame, not a contact-release claim.
 * The 100ms candidate cap includes waiting for quiet (so 70ms motion +30ms
 * quiet can qualify; 80ms motion +30ms quiet cannot). No late/retroactive click.
 *
 * uint32 milliseconds use modular subtraction, including natural rollover.
 * Poll gaps >100ms or backwards/half-range clock jumps flush without clicking.
 * Equal timestamps are allowed but cannot advance quiet; frame count is bounded.
 * Invalid samples/clock faults flush and require quiet from the fault's time;
 * invalid dx/dy are ignored. Valid disabled/blocked motion is passed unchanged.
 * No call means no event; the next step checks the poll gap before quiet.
 * reset deliberately drops pending motion; the caller should use it for hard
 * scroll/endpoint/mode boundaries to avoid replaying old movement across modes.
 * State pointers must be non-NULL; do not mutate state fields outside reset.
 */
#ifndef TRACKPAD_MICROTAP_H
#define TRACKPAD_MICROTAP_H

#include <stdbool.h>
#include <stdint.h>

#define TP_MICROTAP_QUIET_MS 30U
#define TP_MICROTAP_MIN_FRAMES 3U
#define TP_MICROTAP_MIN_PATH 3U
#define TP_MICROTAP_MIN_MOTION_SPAN_MS 15U
#define TP_MICROTAP_MAX_CANDIDATE_MS 100U
#define TP_MICROTAP_MAX_MOTION_SPAN_MS 80U
#define TP_MICROTAP_MAX_PATH 16U
#define TP_MICROTAP_MAX_OFFSET 8U
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
    uint32_t last_poll_ms;
    uint32_t first_ms;
    uint32_t last_motion_ms;
    int32_t sum_x;
    int32_t sum_y;
    uint32_t path;
    uint32_t max_offset;
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

static inline void tp_microtap_clear_candidate(struct tp_microtap_state *s) {
    s->count = 0U;
    s->first_ms = 0U;
    s->sum_x = 0;
    s->sum_y = 0;
    s->path = 0U;
    s->max_offset = 0U;
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
                                                          bool valid, bool enabled,
                                                          bool blocked) {
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

    if (s->mode == TP_MICROTAP_PASSTHROUGH &&
        (uint32_t)(now - s->last_motion_ms) >= TP_MICROTAP_QUIET_MS) {
        s->mode = TP_MICROTAP_IDLE;
    }

    if (s->mode == TP_MICROTAP_CANDIDATE) {
        if ((uint32_t)(now - s->first_ms) > TP_MICROTAP_MAX_CANDIDATE_MS) {
            tp_microtap_flush(s, &out);
            s->mode = TP_MICROTAP_PASSTHROUGH;
        } else if ((uint32_t)(now - s->last_motion_ms) >= TP_MICROTAP_QUIET_MS) {
            /* Upper bounds were checked per frame; lower bounds gate clicks.
             * The old run is already quiet, so a new frame may start a separate
             * candidate even when the old underfilled run replays as movement. */
            const bool admitted = s->count >= TP_MICROTAP_MIN_FRAMES &&
                                  s->path >= TP_MICROTAP_MIN_PATH &&
                                  (uint32_t)(s->last_motion_ms - s->first_ms) >=
                                      TP_MICROTAP_MIN_MOTION_SPAN_MS;
            if (admitted) {
                tp_microtap_clear_candidate(s);
                out.click = true;
            } else {
                tp_microtap_flush(s, &out);
            }
            s->mode = TP_MICROTAP_IDLE;
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

    /* Live candidates have |sum_x/y|<=8 and path<=16; even int16 extremes
     * added here remain safely within int32/uint32, before any narrowing. */
    const int32_t x = s->sum_x + (int32_t)dx;
    const int32_t y = s->sum_y + (int32_t)dy;
    const uint32_t ax = tp_microtap_magnitude(x);
    const uint32_t ay = tp_microtap_magnitude(y);
    const uint32_t offset = ax > ay ? ax : ay;
    const uint32_t path = s->path + tp_microtap_magnitude((int32_t)dx) +
                          tp_microtap_magnitude((int32_t)dy);
    if (s->count >= TP_MICROTAP_MAX_PENDING ||
        (uint32_t)(now - s->first_ms) > TP_MICROTAP_MAX_MOTION_SPAN_MS ||
        path > TP_MICROTAP_MAX_PATH || offset > TP_MICROTAP_MAX_OFFSET) {
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
    if (offset > s->max_offset) {
        s->max_offset = offset;
    }
    s->last_motion_ms = now;
    return out;
}

#endif /* TRACKPAD_MICROTAP_H */
