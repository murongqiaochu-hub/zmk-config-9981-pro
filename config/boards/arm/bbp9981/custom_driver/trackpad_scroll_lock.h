/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TP_LEFT_SHOULDER_POSITION 0u
#define TP_RIGHT_SHOULDER_POSITION 6u
#define TP_SCROLL_SHOULDER_LEFT_BIT 1u
#define TP_SCROLL_SHOULDER_RIGHT_BIT 2u
#define TP_SCROLL_SHOULDER_TAP_MS 180
#define TP_SCROLL_SHOULDER_GAP_MS 220
#define TP_SCROLL_ROTATION_MASK 3u
#define TP_SCROLL_ROTATION_RIGHT_STEP 1u
#define TP_SCROLL_ROTATION_LEFT_STEP 3u

/** Effective lock: latched scrolling, temporarily overridden while aA is held. */
bool tp_scroll_is_locked(void);

/** True while a locked-scroll shoulder modifier is held in the aA layer. */
bool tp_scroll_shoulder_slow_active(void);

/** Update a locked-scroll shoulder modifier owner. */
void tp_scroll_shoulder_modifier_update(uint32_t position, bool pressed);

/** Reset the orientation and any incomplete shoulder triple-tap sequence. */
void tp_scroll_rotation_reset(void);

/** Current locked-scroll orientation step (0..3); step 1 maps left->up and up->right. */
uint8_t tp_scroll_rotation_quadrants(void);

/** Cancel an incomplete shoulder triple-tap without changing orientation. */
void tp_scroll_shoulder_tap_cancel(void);

/** Feed a completed wrapper edge to the locked shoulder rotation recognizer. */
void tp_scroll_shoulder_tap_update(uint32_t position, bool pressed, int64_t timestamp);

static inline int16_t tp_scroll_negate_saturated(int16_t value) {
    return value == INT16_MIN ? INT16_MAX : (int16_t)-value;
}

/** Rotate HID (horizontal, vertical) wheel deltas. Step 1 maps left->up/up->right. */
static inline void tp_scroll_apply_rotation(uint8_t quadrants, int16_t *hwheel,
                                            int16_t *wheel) {
    if (hwheel == NULL || wheel == NULL) { return; }
    const int16_t x = *hwheel;
    const int16_t y = *wheel;
    switch (quadrants & TP_SCROLL_ROTATION_MASK) {
    case 1u:
        *hwheel = y;
        *wheel = tp_scroll_negate_saturated(x);
        break;
    case 2u:
        *hwheel = tp_scroll_negate_saturated(x);
        *wheel = tp_scroll_negate_saturated(y);
        break;
    case 3u:
        *hwheel = tp_scroll_negate_saturated(y);
        *wheel = x;
        break;
    default:
        break;
    }
}
