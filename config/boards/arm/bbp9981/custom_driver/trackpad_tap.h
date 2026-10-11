/* SPDX-License-Identifier: Apache-2.0
 * Hardware-independent, single-contact tap recognizer (C11, no allocation).
 *
 * This is NOT a contact decoder, pointer filter, or HID/button producer.
 * Only the caller can declare a packet's contact credible. Missing packets,
 * read errors, undecodable contact, and zero-initialized inputs MUST NOT be UP.
 * An all-zero input is invalid; TP_TAP_UNKNOWN is also invalid even if valid=true.
 * Call tp_tap_reset() before first use and after a sensor/clock/endpoint reset.
 * Call tp_tap_step() on each sample, or with valid=false to report an input fault.
 * Not calling step at all cannot produce an event; the next sample checks the gap.
 * Observe config.enabled changes through step, or reset on out-of-band changes.
 *
 * Units/boundaries (local conservative defaults, NOT Apple specifications):
 * - timestamps and timing limits: uint32_t milliseconds, modulo 2^32;
 * - dx/dy: unscaled, unaccelerated relative raw counts; zero motion != UP;
 * - displacement: Euclidean distance from the origin immediately BEFORE the
 *   first credible DOWN. Include that first packet's delta and every credible
 *   packet through release confirmation, including residual motion on UP.
 *   max_displacement_sq tracks the largest (sum_x^2 + sum_y^2), not endpoint
 *   distance or traveled path length. Returning to the origin cannot uncancel.
 * - duration/displacement/gap limits are inclusive; debounce uses >= and at
 *   least min_samples distinct, strictly advancing timestamps. A half-range
 *   timestamp jump, backward timestamp, or duplicate is a stale input fault.
 * - duration ends at first credible UP, NOT UP-confirmation time. A DOWN bounce
 *   resumes the ORIGINAL first-DOWN time and cumulative displacement, including
 *   elapsed time during the provisional UP. Unconfirmed DOWN followed by UP is
 *   rejected as a short touch and never rebuilt while that contact bounces.
 * - reset, disable/re-enable, config changes, and faults invalidate candidates.
 *   Initialization and every cancellation require a new stable, credible UP
 *   before arming. Fault/suppression packets do not count toward UP debounce.
 *   Cancellation reason is sticky until stable UP; the first cause is retained.
 *
 * click_ready is a one-step event after a confirmed release, never a held button.
 * The output layer must decide whether/how to emit a press/release; this library
 * never emits either and owns no key, pointer, LED, endpoint, or hardware state.
 * fsm/config pointers must be non-NULL; serialize calls for each fsm instance.
 */
#ifndef TRACKPAD_TAP_H
#define TRACKPAD_TAP_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

/* UNKNOWN cannot be confused with a credible UP merely by valid=true. */
enum tp_tap_contact {
    TP_TAP_UNKNOWN = -1,
    TP_TAP_UP = 0,
    TP_TAP_DOWN = 1,
};

enum tp_tap_state {
    TP_TAP_WAIT_UP = 0,
    TP_TAP_DISABLED,
    TP_TAP_ARMED,
    TP_TAP_DOWN_DEBOUNCE,
    TP_TAP_CONTACT,
    TP_TAP_UP_DEBOUNCE,
    TP_TAP_CANCEL_WAIT_UP,
};

enum tp_tap_reason {
    TP_TAP_REASON_RESET = 0,
    TP_TAP_REASON_DISABLED,
    TP_TAP_REASON_NEED_UP,
    TP_TAP_REASON_ARMED,
    TP_TAP_REASON_DOWN_PENDING,
    TP_TAP_REASON_DOWN_CONFIRMED,
    TP_TAP_REASON_UP_PENDING,
    TP_TAP_REASON_UP_BOUNCE,
    TP_TAP_REASON_READY,
    TP_TAP_REASON_SHORT_TOUCH,
    TP_TAP_REASON_DURATION,
    TP_TAP_REASON_DISPLACEMENT,
    TP_TAP_REASON_COORD_OVERFLOW,
    TP_TAP_REASON_INVALID,
    TP_TAP_REASON_GAP,
    TP_TAP_REASON_STALE_TIME,
    TP_TAP_REASON_SUPPRESSED,
    TP_TAP_REASON_BAD_CONFIG,
    TP_TAP_REASON_CONFIG_CHANGED,
};

struct tp_tap_config {
    bool enabled;
    uint32_t max_duration_ms;
    uint32_t max_displacement_counts;
    uint32_t down_debounce_ms;
    uint32_t up_debounce_ms;
    uint32_t min_samples;
    uint32_t max_gap_ms;
};

/* Centralized knobs. Default-disabled: enabling requires a credible decoder. */
static inline struct tp_tap_config tp_tap_default_config(void) {
    return (struct tp_tap_config){
        .enabled = false,
        .max_duration_ms = 200U,
        .max_displacement_counts = 12U,
        .down_debounce_ms = 20U,
        .up_debounce_ms = 20U,
        .min_samples = 2U,
        .max_gap_ms = 100U,
    };
}

struct tp_tap_input {
    uint32_t t_ms;
    bool valid;
    enum tp_tap_contact contact;
    int32_t dx;
    int32_t dy;
    /* True for physical click, scroll, mode/endpoint conflict, etc. */
    bool suppress;
};

struct tp_tap_result {
    bool click_ready;
    enum tp_tap_state state;
    enum tp_tap_reason reason;
    /* Last candidate's diagnostics persist after cancellation/completion.
     * No candidate is live unless the FSM is in a candidate state. */
    uint32_t touch_duration_ms;
    int32_t sum_x;
    int32_t sum_y;
    uint64_t max_displacement_sq;
    uint32_t down_samples;
    uint32_t up_samples;
};

struct tp_tap_fsm {
    enum tp_tap_state state;
    enum tp_tap_reason reason;
    struct tp_tap_config config;
    bool enabled;
    bool config_seen;
    bool have_time;
    bool up_tracking;
    bool down_confirmed;
    uint32_t last_ms;
    uint32_t first_down_ms;
    uint32_t first_up_ms;
    uint32_t up_since_ms;
    uint32_t down_samples;
    uint32_t up_samples;
    uint32_t touch_duration_ms;
    int32_t sum_x;
    int32_t sum_y;
    uint64_t max_displacement_sq;
};

static inline void tp_tap_reset(struct tp_tap_fsm *fsm) {
    *fsm = (struct tp_tap_fsm){
        .state = TP_TAP_WAIT_UP,
        .reason = TP_TAP_REASON_RESET,
    };
}

static inline struct tp_tap_result tp_tap_snapshot(const struct tp_tap_fsm *fsm,
                                                   bool click_ready) {
    return (struct tp_tap_result){
        .click_ready = click_ready,
        .state = fsm->state,
        .reason = fsm->reason,
        .touch_duration_ms = fsm->touch_duration_ms,
        .sum_x = fsm->sum_x,
        .sum_y = fsm->sum_y,
        .max_displacement_sq = fsm->max_displacement_sq,
        .down_samples = fsm->down_samples,
        .up_samples = fsm->up_samples,
    };
}

static inline bool tp_tap_config_valid(const struct tp_tap_config *config) {
    const uint32_t half_range = UINT32_C(0x80000000);
    return config->max_duration_ms > 0U && config->max_duration_ms < half_range &&
           config->down_debounce_ms > 0U && config->down_debounce_ms < half_range &&
           config->up_debounce_ms > 0U && config->up_debounce_ms < half_range &&
           config->max_duration_ms >= config->down_debounce_ms &&
           config->max_gap_ms > 0U && config->max_gap_ms < half_range &&
           config->max_displacement_counts <= (uint32_t)INT32_MAX &&
           config->min_samples >= 2U;
}

static inline bool tp_tap_same_limits(const struct tp_tap_config *a,
                                       const struct tp_tap_config *b) {
    /* Compare fields, not struct padding. enabled is handled separately. */
    return a->max_duration_ms == b->max_duration_ms &&
           a->max_displacement_counts == b->max_displacement_counts &&
           a->down_debounce_ms == b->down_debounce_ms &&
           a->up_debounce_ms == b->up_debounce_ms &&
           a->min_samples == b->min_samples && a->max_gap_ms == b->max_gap_ms;
}

static inline void tp_tap_cancel(struct tp_tap_fsm *fsm, enum tp_tap_reason reason) {
    if (fsm->state != TP_TAP_CANCEL_WAIT_UP) {
        fsm->reason = reason;
    }
    fsm->state = TP_TAP_CANCEL_WAIT_UP;
    fsm->down_confirmed = false;
    fsm->up_tracking = false;
    fsm->up_samples = 0U;
}

/* Only credible, unsuppressed, timely samples may call this helper. */
static inline void tp_tap_wait_up(struct tp_tap_fsm *fsm,
                                   const struct tp_tap_config *config,
                                   struct tp_tap_input input) {
    if (input.contact == TP_TAP_DOWN) {
        fsm->up_tracking = false;
        fsm->up_samples = 0U;
        return;
    }
    if (!fsm->up_tracking) {
        fsm->up_tracking = true;
        fsm->up_since_ms = input.t_ms;
        fsm->up_samples = 1U;
    } else if (fsm->up_samples < config->min_samples) {
        ++fsm->up_samples;
    }
    if ((uint32_t)(input.t_ms - fsm->up_since_ms) >= config->up_debounce_ms &&
        fsm->up_samples >= config->min_samples) {
        fsm->state = TP_TAP_ARMED;
        fsm->reason = TP_TAP_REASON_ARMED;
        fsm->up_tracking = false;
    }
}

/* Check addition before narrowing; square magnitudes without signed overflow.
 * Two int32 magnitudes squared sum to at most 2^63, safely within uint64_t. */
static inline enum tp_tap_reason tp_tap_add_motion(struct tp_tap_fsm *fsm,
                                                   const struct tp_tap_config *config,
                                                   struct tp_tap_input input) {
    int64_t x = (int64_t)fsm->sum_x + (int64_t)input.dx;
    int64_t y = (int64_t)fsm->sum_y + (int64_t)input.dy;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {
        return TP_TAP_REASON_COORD_OVERFLOW;
    }
    fsm->sum_x = (int32_t)x;
    fsm->sum_y = (int32_t)y;
    uint64_t ax = (uint64_t)(x < 0 ? -x : x);
    uint64_t ay = (uint64_t)(y < 0 ? -y : y);
    uint64_t displacement_sq = ax * ax + ay * ay;
    if (displacement_sq > fsm->max_displacement_sq) {
        fsm->max_displacement_sq = displacement_sq;
    }
    uint64_t radius = config->max_displacement_counts;
    return fsm->max_displacement_sq > radius * radius
               ? TP_TAP_REASON_DISPLACEMENT : TP_TAP_REASON_DOWN_PENDING;
}

/* Every call returns exactly one snapshot, with at most one click-ready pulse. */
static inline struct tp_tap_result tp_tap_step(struct tp_tap_fsm *fsm,
                                               const struct tp_tap_config *config,
                                               struct tp_tap_input input) {
    if (!config->enabled) {
        fsm->enabled = false;
        fsm->state = TP_TAP_DISABLED;
        fsm->reason = TP_TAP_REASON_DISABLED;
        fsm->have_time = false;
        fsm->up_tracking = false;
        fsm->down_confirmed = false;
        fsm->up_samples = 0U;
        return tp_tap_snapshot(fsm, false);
    }
    if (!tp_tap_config_valid(config)) {
        fsm->enabled = true;
        fsm->config_seen = false;
        tp_tap_cancel(fsm, TP_TAP_REASON_BAD_CONFIG);
        return tp_tap_snapshot(fsm, false);
    }
    if (!fsm->enabled) {
        fsm->enabled = true;
        fsm->config = *config;
        fsm->config_seen = true;
        fsm->state = TP_TAP_WAIT_UP;
        fsm->reason = TP_TAP_REASON_NEED_UP;
        fsm->have_time = false;
        fsm->up_tracking = false;
        fsm->down_confirmed = false;
        fsm->up_samples = 0U;
    } else if (!fsm->config_seen || !tp_tap_same_limits(&fsm->config, config)) {
        fsm->config = *config;
        fsm->config_seen = true;
        tp_tap_cancel(fsm, TP_TAP_REASON_CONFIG_CHANGED);
        return tp_tap_snapshot(fsm, false);
    }

    if (fsm->have_time) {
        uint32_t dt = (uint32_t)(input.t_ms - fsm->last_ms);
        if (dt == 0U || dt >= UINT32_C(0x80000000)) {
            tp_tap_cancel(fsm, TP_TAP_REASON_STALE_TIME);
            return tp_tap_snapshot(fsm, false);
        }
        fsm->last_ms = input.t_ms;
        if (dt > config->max_gap_ms) {
            tp_tap_cancel(fsm, TP_TAP_REASON_GAP);
            return tp_tap_snapshot(fsm, false);
        }
    } else {
        fsm->last_ms = input.t_ms;
        fsm->have_time = true;
    }
    if (input.suppress) {
        tp_tap_cancel(fsm, TP_TAP_REASON_SUPPRESSED);
        return tp_tap_snapshot(fsm, false);
    }
    if (!input.valid || (input.contact != TP_TAP_UP && input.contact != TP_TAP_DOWN)) {
        tp_tap_cancel(fsm, TP_TAP_REASON_INVALID);
        return tp_tap_snapshot(fsm, false);
    }

    if (fsm->state == TP_TAP_WAIT_UP || fsm->state == TP_TAP_CANCEL_WAIT_UP) {
        tp_tap_wait_up(fsm, config, input);
        return tp_tap_snapshot(fsm, false);
    }
    if (fsm->state == TP_TAP_ARMED) {
        if (input.contact == TP_TAP_UP) {
            fsm->reason = TP_TAP_REASON_ARMED;
            return tp_tap_snapshot(fsm, false);
        }
        fsm->state = TP_TAP_DOWN_DEBOUNCE;
        fsm->reason = TP_TAP_REASON_DOWN_PENDING;
        fsm->first_down_ms = input.t_ms;
        fsm->first_up_ms = 0U;
        fsm->down_samples = 1U;
        fsm->up_samples = 0U;
        fsm->down_confirmed = false;
        fsm->touch_duration_ms = 0U;
        fsm->sum_x = 0;
        fsm->sum_y = 0;
        fsm->max_displacement_sq = 0U;
        enum tp_tap_reason motion = tp_tap_add_motion(fsm, config, input);
        if (motion == TP_TAP_REASON_DISPLACEMENT || motion == TP_TAP_REASON_COORD_OVERFLOW) {
            tp_tap_cancel(fsm, motion);
        }
        return tp_tap_snapshot(fsm, false);
    }

    /* A live candidate: freeze elapsed time ONLY while provisional UP stays UP. */
    if (input.contact == TP_TAP_DOWN || fsm->state != TP_TAP_UP_DEBOUNCE) {
        fsm->touch_duration_ms = (uint32_t)(input.t_ms - fsm->first_down_ms);
    }
    if (input.contact == TP_TAP_UP && fsm->state != TP_TAP_UP_DEBOUNCE) {
        fsm->first_up_ms = input.t_ms;
    }
    enum tp_tap_reason motion = tp_tap_add_motion(fsm, config, input);
    if (motion == TP_TAP_REASON_DISPLACEMENT || motion == TP_TAP_REASON_COORD_OVERFLOW) {
        tp_tap_cancel(fsm, motion);
        tp_tap_wait_up(fsm, config, input);
        return tp_tap_snapshot(fsm, false);
    }
    if (fsm->touch_duration_ms > config->max_duration_ms) {
        tp_tap_cancel(fsm, TP_TAP_REASON_DURATION);
        tp_tap_wait_up(fsm, config, input);
        return tp_tap_snapshot(fsm, false);
    }

    if (input.contact == TP_TAP_DOWN) {
        if (fsm->state == TP_TAP_UP_DEBOUNCE) {
            /* down_confirmed was retained, as were first_down and all motion. */
            fsm->state = TP_TAP_CONTACT;
            fsm->reason = TP_TAP_REASON_UP_BOUNCE;
            fsm->up_tracking = false;
            fsm->up_samples = 0U;
        } else if (!fsm->down_confirmed) {
            if (fsm->down_samples < config->min_samples) {
                ++fsm->down_samples;
            }
            if (fsm->touch_duration_ms >= config->down_debounce_ms &&
                fsm->down_samples >= config->min_samples) {
                fsm->down_confirmed = true;
                fsm->state = TP_TAP_CONTACT;
                fsm->reason = TP_TAP_REASON_DOWN_CONFIRMED;
            } else {
                fsm->reason = TP_TAP_REASON_DOWN_PENDING;
            }
        } else {
            fsm->reason = TP_TAP_REASON_DOWN_CONFIRMED;
        }
        return tp_tap_snapshot(fsm, false);
    }

    if (!fsm->down_confirmed) {
        tp_tap_cancel(fsm, TP_TAP_REASON_SHORT_TOUCH);
        tp_tap_wait_up(fsm, config, input);
        return tp_tap_snapshot(fsm, false);
    }
    if (fsm->state != TP_TAP_UP_DEBOUNCE) {
        fsm->state = TP_TAP_UP_DEBOUNCE;
        fsm->reason = TP_TAP_REASON_UP_PENDING;
        fsm->up_tracking = true;
        fsm->up_since_ms = input.t_ms;
        fsm->up_samples = 1U;
    } else if (fsm->up_samples < config->min_samples) {
        ++fsm->up_samples;
    }
    if ((uint32_t)(input.t_ms - fsm->up_since_ms) >= config->up_debounce_ms &&
        fsm->up_samples >= config->min_samples) {
        fsm->state = TP_TAP_ARMED;
        fsm->reason = TP_TAP_REASON_READY;
        fsm->up_tracking = false;
        fsm->down_confirmed = false;
        return tp_tap_snapshot(fsm, true);
    }
    return tp_tap_snapshot(fsm, false);
}

static inline const char *tp_tap_state_name(enum tp_tap_state state) {
    switch (state) {
    case TP_TAP_WAIT_UP: return "wait-up";
    case TP_TAP_DISABLED: return "disabled";
    case TP_TAP_ARMED: return "armed";
    case TP_TAP_DOWN_DEBOUNCE: return "down-debounce";
    case TP_TAP_CONTACT: return "contact";
    case TP_TAP_UP_DEBOUNCE: return "up-debounce";
    case TP_TAP_CANCEL_WAIT_UP: return "cancel-wait-up";
    default: return "unknown-state";
    }
}

static inline const char *tp_tap_reason_name(enum tp_tap_reason reason) {
    switch (reason) {
    case TP_TAP_REASON_RESET: return "reset";
    case TP_TAP_REASON_DISABLED: return "disabled";
    case TP_TAP_REASON_NEED_UP: return "need-stable-up";
    case TP_TAP_REASON_ARMED: return "armed";
    case TP_TAP_REASON_DOWN_PENDING: return "down-pending";
    case TP_TAP_REASON_DOWN_CONFIRMED: return "down-confirmed";
    case TP_TAP_REASON_UP_PENDING: return "up-pending";
    case TP_TAP_REASON_UP_BOUNCE: return "up-bounce";
    case TP_TAP_REASON_READY: return "click-ready";
    case TP_TAP_REASON_SHORT_TOUCH: return "short-touch";
    case TP_TAP_REASON_DURATION: return "duration";
    case TP_TAP_REASON_DISPLACEMENT: return "displacement";
    case TP_TAP_REASON_COORD_OVERFLOW: return "coordinate-overflow";
    case TP_TAP_REASON_INVALID: return "invalid-contact";
    case TP_TAP_REASON_GAP: return "sample-gap";
    case TP_TAP_REASON_STALE_TIME: return "stale-time";
    case TP_TAP_REASON_SUPPRESSED: return "suppressed";
    case TP_TAP_REASON_BAD_CONFIG: return "bad-config";
    case TP_TAP_REASON_CONFIG_CHANGED: return "config-changed";
    default: return "unknown-reason";
    }
}

#endif /* TRACKPAD_TAP_H */
