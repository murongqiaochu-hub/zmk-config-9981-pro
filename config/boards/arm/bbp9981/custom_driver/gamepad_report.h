/* SPDX-License-Identifier: MIT
 * Standard HID Xbox-layout Game Pad, Report ID4/layer3. Not XInput, no rumble.
 * Nine-byte body: 13 implemented button usages in 16 bits, null hat8,
 * signed8 X/Y/Z/Rz and unsigned8 Brake/Accelerator (LT/RT).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gamepad_stick.h"

#define BBP_GAMEPAD_REPORT_ID 4U
#define BBP_GAMEPAD_LAYER 3U
#define BBP_GAMEPAD_POSITIONS 42U
#define BBP_GAMEPAD_ACTION_COUNT 19U
#define BBP_GAMEPAD_HAT_NEUTRAL 8U

enum bbp_gamepad_action {
    BBP_GP_UP = 0, BBP_GP_DOWN = 1, BBP_GP_LEFT = 2, BBP_GP_RIGHT = 3,
    BBP_GP_A = 4, BBP_GP_B = 5, BBP_GP_X = 6, BBP_GP_Y = 7,
    BBP_GP_L = 8, BBP_GP_R = 9, BBP_GP_LT = 10, BBP_GP_RT = 11,
    BBP_GP_SELECT = 12, BBP_GP_START = 13, BBP_GP_GUIDE = 14,
    BBP_GP_L3 = 15, BBP_GP_R3 = 16,
    BBP_GP_RIGHT_STICK_MOD = 17, BBP_GP_STICK_CLICK = 18,
};
#define BBP_GP_LB BBP_GP_L
#define BBP_GP_RB BBP_GP_R
_Static_assert(BBP_GP_STICK_CLICK + 1U == BBP_GAMEPAD_ACTION_COUNT, "Gamepad action ABI");

struct bbp_gamepad_report_body {
    uint8_t buttons_lo;
    uint8_t buttons_hi;
    uint8_t hat;
    int8_t x, y, z, rz;
    uint8_t lt, rt;
};
struct bbp_gamepad_report {
    uint8_t report_id;
    struct bbp_gamepad_report_body body;
};
_Static_assert(sizeof(struct bbp_gamepad_report_body) == 9U, "Gamepad body ABI");
_Static_assert(sizeof(struct bbp_gamepad_report) == 10U, "Gamepad USB report ABI");

static inline struct bbp_gamepad_report_body bbp_gamepad_neutral(void) {
    return (struct bbp_gamepad_report_body){.hat = BBP_GAMEPAD_HAT_NEUTRAL};
}
static inline bool bbp_gamepad_is_neutral(const struct bbp_gamepad_report_body *r) {
    return r && !r->buttons_lo && !r->buttons_hi && r->hat == BBP_GAMEPAD_HAT_NEUTRAL &&
           !r->x && !r->y && !r->z && !r->rz && !r->lt && !r->rt;
}

/* Separate Game Pad top-level collection. Adapters preserve original IDs1..3
 * verbatim. Button bits 3/6/16 are Constant, not phantom host controls.
 * Trigger globals explicitly reset the signed-axis/hat logical, physical,
 * unit and exponent state; LT/RT are Simulation Controls, not extra sticks. */
#define BBP_GAMEPAD_DESCRIPTOR_BYTES \
    0x05,0x01, 0x09,0x05, 0xa1,0x01, 0x85,BBP_GAMEPAD_REPORT_ID, \
    0x05,0x09, 0x15,0x00, 0x25,0x01, 0x35,0x00, 0x45,0x00, \
    0x55,0x00, 0x65,0x00, 0x75,0x01, \
    0x19,0x01, 0x29,0x02, 0x95,0x02, 0x81,0x02, \
    0x95,0x01, 0x81,0x03, \
    0x19,0x04, 0x29,0x05, 0x95,0x02, 0x81,0x02, \
    0x95,0x01, 0x81,0x03, \
    0x19,0x07, 0x29,0x0f, 0x95,0x09, 0x81,0x02, \
    0x95,0x01, 0x81,0x03, \
    0x05,0x01, 0x09,0x39, 0x15,0x00, 0x25,0x07, \
    0x35,0x00, 0x46,0x3b,0x01, 0x55,0x00, 0x65,0x14, \
    0x75,0x04, 0x95,0x01, 0x81,0x42, \
    0x75,0x04, 0x95,0x01, 0x81,0x03, \
    0x35,0x00, 0x45,0x00, 0x55,0x00, 0x65,0x00, \
    0x09,0x30, 0x09,0x31, 0x09,0x32, 0x09,0x35, \
    0x15,0x81, 0x25,0x7f, 0x75,0x08, 0x95,0x04, 0x81,0x02, \
    0x05,0x02, 0x09,0xc5, 0x09,0xc4, \
    0x15,0x00, 0x26,0xff,0x00, 0x35,0x00, 0x45,0x00, \
    0x55,0x00, 0x65,0x00, 0x75,0x08, 0x95,0x02, 0x81,0x02, 0xc0

/* One captured owner per position avoids duplicate-binding releases and
 * repeated DOWN remaps. STICK_CLICK is resolved on DOWN to L3/R3 and remains
 * that owner until UP even if BB changes. Only the final any-BB mode transition
 * cancels motion; keys, Dpad and other owners are never cleared by a mode edge.
 * Treat stick as private; use motion/expire/remaining_ms/cancel below. */
struct bbp_gamepad_state {
    uint8_t owners[BBP_GAMEPAD_POSITIONS];
    struct bbp_gamepad_stick_state stick;
};

static inline uint32_t bbp_gamepad_actions(const struct bbp_gamepad_state *s) {
    uint32_t actions = 0U;
    if (!s) { return actions; }
    for (size_t p = 0; p < BBP_GAMEPAD_POSITIONS; ++p) {
        if (s->owners[p] && s->owners[p] <= BBP_GAMEPAD_ACTION_COUNT) {
            actions |= UINT32_C(1) << (s->owners[p] - 1U);
        }
    }
    return actions;
}
static inline bool bbp_gamepad_right_stick_mode(const struct bbp_gamepad_state *s) {
    return (bbp_gamepad_actions(s) & (UINT32_C(1) << BBP_GP_RIGHT_STICK_MOD)) != 0U;
}
static inline void bbp_gamepad_reset(struct bbp_gamepad_state *s) {
    if (s) { *s = (struct bbp_gamepad_state){0}; }
}
/* Local sensor-fault cancellation: no button, Dpad or BB-owner release. */
static inline bool bbp_gamepad_motion_cancel(struct bbp_gamepad_state *s) {
    return s && bbp_gamepad_stick_cancel(&s->stick);
}
static inline bool bbp_gamepad_key(struct bbp_gamepad_state *s, uint32_t position,
                                  uint32_t action, bool pressed) {
    if (!s || position >= BBP_GAMEPAD_POSITIONS) { return false; }
    bool was_right = bbp_gamepad_right_stick_mode(s);
    if (!pressed) {
        if (!s->owners[position]) { return false; }
        s->owners[position] = 0U; /* Ignore action: release the captured owner. */
    } else {
        if (action >= BBP_GAMEPAD_ACTION_COUNT || s->owners[position]) { return false; }
        if (action == BBP_GP_STICK_CLICK) { action = was_right ? BBP_GP_R3 : BBP_GP_L3; }
        s->owners[position] = (uint8_t)(action + 1U);
    }
    if (was_right != bbp_gamepad_right_stick_mode(s)) { (void)bbp_gamepad_motion_cancel(s); }
    return true;
}
/* bool means visible axis change. Same-value valid motion still refreshes the
 * deadline: a caller must recheck remaining_ms even when motion returns false.
 * Quiet/invalid/backward expiry and cancel only affect virtual axes. */
static inline bool bbp_gamepad_motion(struct bbp_gamepad_state *s,
                                     int32_t dx, int32_t dy, int64_t now_ms) {
    return s && bbp_gamepad_stick_motion(&s->stick, dx, dy, now_ms);
}
static inline bool bbp_gamepad_expire(struct bbp_gamepad_state *s, int64_t now_ms) {
    return s && bbp_gamepad_stick_expire(&s->stick, now_ms);
}
static inline uint32_t bbp_gamepad_motion_remaining_ms(const struct bbp_gamepad_state *s,
                                                     int64_t now_ms) {
    return s ? bbp_gamepad_stick_remaining_ms(&s->stick, now_ms) : 0U;
}
static inline struct bbp_gamepad_report_body bbp_gamepad_snapshot(const struct bbp_gamepad_state *s) {
    if (!s) { return bbp_gamepad_neutral(); }
    uint32_t actions = bbp_gamepad_actions(s);
    uint16_t buttons = 0U;
    /* A/B/X/Y, LB/RB, digital LT/RT, View/Menu/Guide, L3/R3. */
    static const uint8_t usages[13] = {1U,2U,4U,5U,7U,8U,9U,10U,11U,12U,13U,14U,15U};
    for (uint8_t a = BBP_GP_A; a <= BBP_GP_R3; ++a) {
        if (actions & (UINT32_C(1) << a)) {
            buttons |= (uint16_t)(UINT32_C(1) << (usages[a - BBP_GP_A] - 1U));
        }
    }
    int x = !!(actions & (UINT32_C(1) << BBP_GP_RIGHT)) -
            !!(actions & (UINT32_C(1) << BBP_GP_LEFT));
    int y = !!(actions & (UINT32_C(1) << BBP_GP_DOWN)) -
            !!(actions & (UINT32_C(1) << BBP_GP_UP));
    uint8_t hat = y < 0 ? (x < 0 ? 7U : x > 0 ? 1U : 0U) :
                  y > 0 ? (x < 0 ? 5U : x > 0 ? 3U : 4U) :
                          (x < 0 ? 6U : x > 0 ? 2U : BBP_GAMEPAD_HAT_NEUTRAL);
    struct bbp_gamepad_report_body r = {.buttons_lo = (uint8_t)buttons,
        .buttons_hi = (uint8_t)(buttons >> 8), .hat = hat,
        .lt = (actions & (UINT32_C(1) << BBP_GP_LT)) ? 255U : 0U,
        .rt = (actions & (UINT32_C(1) << BBP_GP_RT)) ? 255U : 0U};
    if (actions & (UINT32_C(1) << BBP_GP_RIGHT_STICK_MOD)) {
        r.z = s->stick.x;
        r.rz = s->stick.y;
    } else {
        r.x = s->stick.x;
        r.y = s->stick.y;
    }
    return r;
}
