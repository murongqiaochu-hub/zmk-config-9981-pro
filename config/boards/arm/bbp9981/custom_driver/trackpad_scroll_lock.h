/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Matrix positions: two shoulders, then Call/BlackBerry/trackpad/Back/End. */
#define TP_LEFT_SHOULDER_POSITION 0u
#define TP_RIGHT_SHOULDER_POSITION 1u
#define TP_CALL_POSITION 2u
#define TP_BLACKBERRY_POSITION 3u
#define TP_TRACKPAD_POSITION 4u
#define TP_BACK_POSITION 5u
#define TP_ENDCALL_POSITION 6u
/* Physical M is freed by shifting the five UPPER light controls one key left. */
#define TP_MOUSE_TOGGLE_POSITION 34u

#define TP_SCROLL_SHOULDER_LEFT_BIT 1u
#define TP_SCROLL_SHOULDER_RIGHT_BIT 2u
#define TP_SCROLL_GESTURE_TAP_MS 180
#define TP_SCROLL_GESTURE_GAP_MS 220
#define TP_SCROLL_ROTATION_MASK 3u
#define TP_SCROLL_ROTATION_LEFT_TO_UP_FIXED 1u
#define TP_SCROLL_ROTATION_RIGHT_TO_UP_FIXED 3u

/** Effective lock: latched scrolling, temporarily overridden while aA is held. */
bool tp_scroll_is_locked(void);
/** Whether the scroll lock is latched, including while aA temporarily overrides it. */
bool tp_scroll_lock_is_latched(void);
/** Whether the physical right-aA key is currently held. */
bool tp_scroll_aa_is_pressed(void);

/** True while a locked-scroll shoulder modifier is held in the aA layer. */
bool tp_scroll_shoulder_slow_active(void);

/** Update a locked-scroll shoulder modifier owner. */
void tp_scroll_shoulder_modifier_update(uint32_t position, bool pressed);

/** Reset the orientation when the scroll lock is unlocked/relocked. */
void tp_scroll_rotation_reset(void);
/** Current locked-scroll orientation (0..3); 1/3 are fixed BlackBerry/Back presets. */
uint8_t tp_scroll_rotation_quadrants(void);
/** Select an absolute orientation. Repeated selection is idempotent, never additive. */
void tp_scroll_rotation_select(uint8_t orientation);
/** Cancel an incomplete BlackBerry/Back/F multi-tap sequence. */
void tp_scroll_gesture_cancel(void);

/** Volatile inverted-mouse mode; applies only to default-layer mouse output. */
bool tp_mouse_orientation_reversed(void);
bool tp_mouse_orientation_toggle(void);

static inline int16_t tp_scroll_negate_saturated(int16_t value) {
    return value == INT16_MIN ? INT16_MAX : (int16_t)-value;
}

/** Apply the selected fixed HID wheel mapping. Preset 1 maps left->up/up->right. */
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
