/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TP_AA_POSITION 41
#define TP_AA_HOLD_MS 450
#define TP_AA_DOUBLE_TAP_MS 350
#define TP_MODE_PRESSED 1u
#define TP_MODE_LATCHED 2u
#define TP_MODE_FLAGS (TP_MODE_PRESSED | TP_MODE_LATCHED)
#define TP_MODE_EPOCH_MASK (~TP_MODE_FLAGS)

/* Owned by the position listener. Publish snapshot atomically to poll workers. */
struct tp_key_state {
    bool pressed;
    bool latched;
    bool tap_pending;
    bool double_candidate;
    bool interrupted;
    int64_t press_at;
    int64_t last_release_at;
    uint32_t snapshot;
};

static inline void tp_key_state_publish(struct tp_key_state *s) {
    uint32_t flags = (s->pressed ? TP_MODE_PRESSED : 0u) |
                     (s->latched ? TP_MODE_LATCHED : 0u);
    uint32_t epoch = s->snapshot & TP_MODE_EPOCH_MASK;
    if ((flags != 0u) != ((s->snapshot & TP_MODE_FLAGS) != 0u)) {
        epoch = (epoch + (TP_MODE_FLAGS + 1u)) & TP_MODE_EPOCH_MASK;
    }
    s->snapshot = epoch | flags;
}

/* Use original event time, not delayed dispatch time after a hold-tap replay. */
static inline void tp_key_state_update(struct tp_key_state *s, uint32_t position,
                                       bool pressed, int64_t timestamp) {
    if (position != TP_AA_POSITION) {
        if (pressed) {
            s->tap_pending = false;
            s->double_candidate = false;
            if (s->pressed) {
                s->interrupted = true;
            }
        }
        return;
    }

    if (pressed) {
        if (s->pressed) {
            return;
        }
        s->press_at = timestamp;
        s->double_candidate = s->tap_pending && timestamp >= s->last_release_at &&
                              timestamp - s->last_release_at <= TP_AA_DOUBLE_TAP_MS;
        s->tap_pending = false;
        s->interrupted = false;
        s->pressed = true;
    } else {
        if (!s->pressed) {
            return;
        }
        int64_t held_ms = timestamp - s->press_at;
        s->pressed = false;
        if (!s->interrupted && held_ms >= 0 && held_ms < TP_AA_HOLD_MS) {
            if (s->double_candidate) {
                s->latched = !s->latched;
                s->tap_pending = false;
            } else {
                s->last_release_at = timestamp;
                s->tap_pending = true;
            }
        } else {
            s->tap_pending = false;
        }
        s->double_candidate = false;
    }
    /* Publish release + latch toggle together: no transient pointer mode. */
    tp_key_state_publish(s);
}
