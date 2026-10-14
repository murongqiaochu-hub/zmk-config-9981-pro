/* SPDX-License-Identifier: MIT */
#ifndef TRACKPAD_SHOULDER_H
#define TRACKPAD_SHOULDER_H

#include <stdbool.h>
#include <stdint.h>

#define TP_SHOULDER_TAP_MS UINT32_C(180)
#define TP_SHOULDER_GAP_MS UINT32_C(220)
#define TP_SHOULDER_OUTPUT_EDGES 8u
#define TP_SHOULDER_BUFFER_EDGES 6u
#define TP_SHOULDER_CLOCK_HALF_RANGE UINT32_C(0x80000000)

struct tp_shoulder_edge {
    uint32_t at_ms;
    bool pressed;
};

struct tp_shoulder_result {
    struct tp_shoulder_edge edges[TP_SHOULDER_OUTPUT_EDGES];
    uint8_t count;
    bool toggle;
};

/* Zero-initialize before the first call; serialize all access. This core owns
 * no HID state or enabled flag. The adapter starts enabled=false, applies every
 * result edge in array order, and flips enabled only when result.toggle is true.
 * output_down tracks edges RETURNED to the adapter, not transport acknowledgement.
 * Never discard a returned DOWN and subsequently apply its UP to another owner.
 */
struct tp_shoulder_state {
    struct tp_shoulder_edge buffered[TP_SHOULDER_BUFFER_EDGES];
    uint32_t press_at_ms;
    uint32_t release_at_ms;
    uint32_t last_ms;
    uint8_t buffered_count;
    uint8_t tap_count;
    bool down;
    bool output_down;
    bool passthrough;
    bool clock_valid;
};

/* Reset recognition without forgetting a DOWN already returned to the caller.
 * An unexposed buffered hold is cancelled: its eventual UP is suppressed rather
 * than emitted alone. Use flush(), NOT reset(), to retain delayed single/double
 * clicks on layer/other-key/endpoint interruption. Do not memset a live state.
 */
static inline void tp_shoulder_reset(struct tp_shoulder_state *s) {
    const bool output_down = s->output_down;
    const bool down = s->down || output_down;
    *s = (struct tp_shoulder_state){
        .down = down,
        .output_down = output_down,
        .passthrough = down,
    };
}

/* Suppress competing micro-click requests while a candidate or physical hold
 * owns the shoulder path; a consumed triple is immediately no longer busy.
 */
static inline bool tp_shoulder_busy(const struct tp_shoulder_state *s) {
    return s->buffered_count != 0u || s->down || s->output_down;
}

/* Between calls, a candidate contains at most two completed taps plus a DOWN.
 * Inconsistent recognizer bookkeeping is cancelled without losing output_down.
 */
static inline bool tp_shoulder_valid(const struct tp_shoulder_state *s) {
    if (s->buffered_count >= TP_SHOULDER_BUFFER_EDGES || s->tap_count > 2u) {
        return false;
    }
    if (s->passthrough) {
        return s->down && s->buffered_count == 0u && s->tap_count == 0u;
    }
    if (s->output_down ||
        (unsigned int)s->buffered_count !=
            2u * (unsigned int)s->tap_count + (s->down ? 1u : 0u)) {
        return false;
    }
    for (uint8_t i = 0u; i < s->buffered_count; ++i) {
        if (s->buffered[i].pressed != ((i % 2u) == 0u)) {
            return false;
        }
    }
    return true;
}

/* Deduplication also guarantees that recovery cannot emit an orphan UP.
 * A valid transition returns at most six edges; the public capacity is eight.
 */
static inline void tp_shoulder_emit(struct tp_shoulder_state *s,
                                     struct tp_shoulder_result *r,
                                     struct tp_shoulder_edge edge) {
    if (edge.pressed != s->output_down && r->count < TP_SHOULDER_OUTPUT_EDGES) {
        r->edges[r->count++] = edge;
        s->output_down = edge.pressed;
    }
}

static inline void tp_shoulder_drain(struct tp_shoulder_state *s,
                                      struct tp_shoulder_result *r) {
    for (uint8_t i = 0u; i < s->buffered_count; ++i) {
        tp_shoulder_emit(s, r, s->buffered[i]);
    }
    s->buffered_count = 0u;
    s->tap_count = 0u;
    s->passthrough = s->down;
}

/* Replay all delayed edges, including a currently held DOWN. That hold passes
 * through until its release, so interruption cannot complete a stale triple.
 * Repeated flushes are idempotent and never manufacture a release.
 */
static inline struct tp_shoulder_result tp_shoulder_flush(struct tp_shoulder_state *s,
                                                          uint32_t now) {
    struct tp_shoulder_result r = {0};
    if (!tp_shoulder_valid(s)) {
        tp_shoulder_reset(s);
    }
    tp_shoulder_drain(s, &r);
    s->last_ms = now;
    s->clock_valid = true;
    return r;
}

/* has_edge=false is a timer poll; pressed is ignored on such polls.
 * Tap: release <=180ms after press. Inter-tap gap: new press <=220ms after UP.
 * At exactly 220ms an actual new press takes precedence over idle expiration;
 * a no-edge poll at 220ms drains. Dispatch same-timestamp edges before polling.
 * A held button is drained on the FIRST call with held duration >180ms, even
 * without an input edge; its later release passes through and cannot toggle.
 * Duplicate edges do not change the hold/gap timestamps, but still run timers.
 * Unsigned subtraction supports normal uint32 wrap. A backwards/ambiguous jump
 * of >=2^31ms cancels recognition, drains valid buffered edges and passes the
 * current hold through. Forward gaps <2^31ms expire the normal timers safely.
 * Returned timestamps are original edge times; never sort delayed edges by time
 * (in particular after rollback). No cooldown follows a consumed triple.
 */
static inline struct tp_shoulder_result tp_shoulder_step(struct tp_shoulder_state *s,
                                                         uint32_t now,
                                                         bool has_edge,
                                                         bool pressed) {
    struct tp_shoulder_result r = {0};
    bool discontinuity = !tp_shoulder_valid(s);
    if (discontinuity) {
        tp_shoulder_reset(s);
    }
    if (s->clock_valid && now - s->last_ms >= TP_SHOULDER_CLOCK_HALF_RANGE) {
        discontinuity = true;
    }
    if (discontinuity) {
        tp_shoulder_drain(s, &r);
    }
    s->last_ms = now;
    s->clock_valid = true;

    const bool new_press = has_edge && pressed && !s->down;
    if (s->down && !s->passthrough && now - s->press_at_ms > TP_SHOULDER_TAP_MS) {
        tp_shoulder_drain(s, &r);
    } else if (!s->down && s->tap_count != 0u) {
        const uint32_t gap = now - s->release_at_ms;
        if (gap > TP_SHOULDER_GAP_MS || (gap == TP_SHOULDER_GAP_MS && !new_press)) {
            tp_shoulder_drain(s, &r);
        }
    }

    if (!has_edge || pressed == s->down) {
        return r;
    }

    s->down = pressed;
    if (pressed) {
        s->press_at_ms = now;
        if (discontinuity || s->passthrough) {
            s->passthrough = true;
            tp_shoulder_emit(s, &r, (struct tp_shoulder_edge){now, true});
        } else {
            s->buffered[s->buffered_count++] = (struct tp_shoulder_edge){now, true};
        }
    } else if (s->passthrough) {
        tp_shoulder_emit(s, &r, (struct tp_shoulder_edge){now, false});
        s->passthrough = false;
    } else {
        s->buffered[s->buffered_count++] = (struct tp_shoulder_edge){now, false};
        s->release_at_ms = now;
        ++s->tap_count;
        if (s->tap_count == 3u) {
            s->buffered_count = 0u;
            s->tap_count = 0u;
            r.toggle = true;
        }
    }
    return r;
}

#endif /* TRACKPAD_SHOULDER_H */
