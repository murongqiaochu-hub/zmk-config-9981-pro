/* SPDX-License-Identifier: MIT
 * Pure velocity-to-stick math. No device, transport, scheduler or touch state.
 * Raw dx is right-positive and raw dy is down-positive, just like the mouse.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef BBP_GAMEPAD_STICK_GAIN
#define BBP_GAMEPAD_STICK_GAIN 8
#endif
#ifndef BBP_GAMEPAD_STICK_DEADZONE
#define BBP_GAMEPAD_STICK_DEADZONE 4
#endif
#ifndef BBP_GAMEPAD_STICK_QUIET_MS
#define BBP_GAMEPAD_STICK_QUIET_MS 120U
#endif
_Static_assert(BBP_GAMEPAD_STICK_GAIN > 0 && BBP_GAMEPAD_STICK_GAIN <= INT32_MAX,
               "Stick gain must fit a positive int32");
_Static_assert(BBP_GAMEPAD_STICK_DEADZONE >= 0 && BBP_GAMEPAD_STICK_DEADZONE < 127,
               "Stick deadzone is in logical axis units");
_Static_assert(BBP_GAMEPAD_STICK_QUIET_MS > 0 && BBP_GAMEPAD_STICK_QUIET_MS <= UINT32_MAX,
               "Stick quiet period must fit a positive uint32");

/* Private implementation state. The report core maps this single active pair
 * to X/Y or Z/Rz; a mode transition resets it rather than replaying old motion. */
struct bbp_gamepad_stick_state {
    int8_t x, y;
    bool active, clock_valid;
    int64_t last_motion_ms, last_observed_ms;
};

static inline bool bbp_gamepad_stick_cancel(struct bbp_gamepad_stick_state *s) {
    if (!s) { return false; }
    bool changed = s->x != 0 || s->y != 0;
    *s = (struct bbp_gamepad_stick_state){0};
    return changed;
}

static inline int8_t bbp_gamepad_stick_limit(int64_t logical) {
    if (logical > 127) { logical = 127; }
    if (logical < -127) { logical = -127; }
    if (logical >= -(int64_t)BBP_GAMEPAD_STICK_DEADZONE &&
        logical <= (int64_t)BBP_GAMEPAD_STICK_DEADZONE) {
        logical = 0;
    }
    return (int8_t)logical;
}

static inline int8_t bbp_gamepad_stick_scale(int32_t raw) {
    /* Widen BEFORE multiplying: even INT32_MIN/MAX and a configured positive
     * int32 gain fit int64, so no raw-input overflow or signed abs(INT_MIN). */
    return bbp_gamepad_stick_limit((int64_t)raw * BBP_GAMEPAD_STICK_GAIN);
}

static inline int8_t bbp_gamepad_stick_filter(int8_t old, int8_t sample) {
    if ((old < 0 && sample > 0) || (old > 0 && sample < 0)) { old = 0; }
    int32_t weighted = 3 * (int32_t)sample + old;
    /* Round nearest, with half units away from zero, symmetrically. */
    int32_t rounded = weighted < 0 ? -((-weighted + 2) / 4) : (weighted + 2) / 4;
    return bbp_gamepad_stick_limit(rounded);
}

static inline bool bbp_gamepad_stick_observe(struct bbp_gamepad_stick_state *s, int64_t now_ms) {
    if (now_ms < 0 || (s->clock_valid && now_ms < s->last_observed_ms)) {
        /* Fail closed and discard this call; a later valid call may rearm. */
        (void)bbp_gamepad_stick_cancel(s);
        return false;
    }
    s->last_observed_ms = now_ms;
    s->clock_valid = true;
    return true;
}

static inline uint32_t bbp_gamepad_stick_remaining_ms(const struct bbp_gamepad_stick_state *s,
                                                     int64_t now_ms) {
    if (!s || !s->active || now_ms < 0 || now_ms < s->last_motion_ms ||
        (s->clock_valid && now_ms < s->last_observed_ms)) {
        return 0U;
    }
    /* Only subtract ordered, nonnegative values. No now + timeout overflow at
     * INT64_MAX and no subtraction of a negative/invalid timestamp. */
    uint64_t elapsed = (uint64_t)now_ms - (uint64_t)s->last_motion_ms;
    if (elapsed >= BBP_GAMEPAD_STICK_QUIET_MS) { return 0U; }
    return (uint32_t)(BBP_GAMEPAD_STICK_QUIET_MS - elapsed);
}

static inline void bbp_gamepad_stick_clear_motion(struct bbp_gamepad_stick_state *s) {
    s->x = s->y = 0;
    s->active = false;
    s->last_motion_ms = 0;
}

static inline bool bbp_gamepad_stick_expire(struct bbp_gamepad_stick_state *s, int64_t now_ms) {
    if (!s) { return false; }
    bool was_nonzero = s->x != 0 || s->y != 0;
    if (!bbp_gamepad_stick_observe(s, now_ms)) { return was_nonzero; }
    if (s->active && bbp_gamepad_stick_remaining_ms(s, now_ms) == 0U) {
        bbp_gamepad_stick_clear_motion(s);
    }
    return was_nonzero && s->x == 0 && s->y == 0;
}

/* Logical-domain update is kept separate so the deadzone/filter can be tested
 * independently of the raw-count gain. A fully quiet packet does not decay
 * axes or refresh last_motion_ms; local expiry, not touch inference, centres.
 * The return value means visible axis change, NOT deadline refresh. */
static inline bool bbp_gamepad_stick_update(struct bbp_gamepad_stick_state *s,
                                          int32_t logical_x, int32_t logical_y, int64_t now_ms) {
    if (!s) { return false; }
    int8_t old_x = s->x, old_y = s->y;
    if (!bbp_gamepad_stick_observe(s, now_ms)) { return old_x != 0 || old_y != 0; }
    if (s->active && bbp_gamepad_stick_remaining_ms(s, now_ms) == 0U) {
        bbp_gamepad_stick_clear_motion(s);
    }
    int8_t x = bbp_gamepad_stick_limit(logical_x), y = bbp_gamepad_stick_limit(logical_y);
    if (x != 0 || y != 0) {
        s->x = bbp_gamepad_stick_filter(s->x, x);
        s->y = bbp_gamepad_stick_filter(s->y, y);
        s->last_motion_ms = now_ms;
        s->active = true;
    }
    return old_x != s->x || old_y != s->y;
}

static inline bool bbp_gamepad_stick_motion(struct bbp_gamepad_stick_state *s,
                                          int32_t dx, int32_t dy, int64_t now_ms) {
    return bbp_gamepad_stick_update(s, bbp_gamepad_stick_scale(dx),
                                   bbp_gamepad_stick_scale(dy), now_ms);
}
