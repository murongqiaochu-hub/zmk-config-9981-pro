/* SPDX-License-Identifier: Apache-2.0
 * Physical-key listener and mode publication extracted from the A320 driver.
 * Event ordering, independent atomics and the aA/SYM owner protocol deliberately
 * match v2.2.9. Sensor I/O and motion accumulation do not belong in this service.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include "a320_0x57.h"
#include "trackpad_key_state.h"
#include "trackpad_scroll_lock.h"
#include "trackpad_mode.h"
#include "aa_sym_ctrl.h"

/* Retain the original logging identity as well as listener registration name. */
LOG_MODULE_DECLARE(a320, CONFIG_A320_LOG_LEVEL);

static struct tp_key_state scroll_keys;
/* Coherent mode + transition epoch, also safe for future cross-thread readers. */
static atomic_t scroll_mode_snapshot;
/* Mouse orientation is a volatile mouse-only flag; other layers ignore it. */
static atomic_t mouse_orientation_snapshot;
/* Shoulder modifiers are owned only by the conditional UPPER-layer behavior. */
static atomic_t scroll_shoulder_modifier_snapshot;

/* Bit 0 is physical Ctrl; upper bits record every speed transition.
 * Detect press+release between polls without clearing pointer state in the listener.
 */
static atomic_t cursor_speed_snapshot;
static bool sym_physically_pressed;
#define TP_SYM_POSITION 40

/* ==== position_state_changed event listener ==== */
static int ctrl_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->position == 37) {
        /* Keep original physical Ctrl slow-pointer behavior, separate from SYM Ctrl. */
        uint32_t speed = (uint32_t)atomic_get(&cursor_speed_snapshot);
        if (((speed & 1U) != 0) != ev->state) {
            uint32_t next = ((speed + 2U) & ~1U) | (ev->state ? 1U : 0U);
            atomic_set(&cursor_speed_snapshot, (int32_t)next);
        }
    }
    bool was_latched = scroll_keys.latched;
    tp_key_state_update(&scroll_keys, ev->position, ev->state, ev->timestamp);
    atomic_set(&scroll_mode_snapshot, scroll_keys.snapshot);
    if (was_latched != scroll_keys.latched) {
        /* Each lock session starts unrotated; a held aA override does not reset it. */
        tp_scroll_rotation_reset();
        tp_scroll_gesture_cancel();
        LOG_INF("Scroll lock %s (aA double tap)", scroll_keys.latched ? "ON" : "OFF");
    }
    if ((ev->state && ev->position != TP_BLACKBERRY_POSITION &&
         ev->position != TP_BACK_POSITION && ev->position != TP_MOUSE_TOGGLE_POSITION) ||
        ev->position == TP_AA_POSITION) {
        /* Other keys and either aA edge invalidate a partial gesture/shortcut. */
        tp_scroll_gesture_cancel();
    }
    if (ev->position == TP_SYM_POSITION) { sym_physically_pressed = ev->state; }
    if (ev->position == TP_AA_POSITION || ev->position == TP_SYM_POSITION) {
        if (scroll_keys.pressed && sym_physically_pressed) {
            /* SYM-first is also a chord, never an aA double-tap scroll toggle. */
            scroll_keys.interrupted = true;
            scroll_keys.tap_pending = false;
            scroll_keys.double_candidate = false;
        }
        int err = tp_sym_ctrl_update(scroll_keys.pressed, sym_physically_pressed, ev->timestamp);
        if (err < 0) {
            LOG_ERR("aA/SYM Ctrl state update failed: %d", err);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(a320_ctrl_listener, ctrl_listener_cb);
ZMK_SUBSCRIPTION(a320_ctrl_listener, zmk_position_state_changed);

bool tp_scroll_mode_active(void) {
    return ((uint32_t)atomic_get(&scroll_mode_snapshot) & TP_MODE_FLAGS) != 0;
}
bool tp_scroll_lock_is_latched(void) {
    return ((uint32_t)atomic_get(&scroll_mode_snapshot) & TP_MODE_LATCHED) != 0U;
}
bool tp_scroll_aa_is_pressed(void) { return tp_aa_is_pressed(); }
bool tp_scroll_is_locked(void) {
    /* A held aA temporarily restores the original scroll mode over a latched lock. */
    return tp_scroll_lock_is_latched() && !tp_scroll_aa_is_pressed();
}
bool tp_mouse_orientation_reversed(void) {
    return atomic_get(&mouse_orientation_snapshot) != 0;
}
bool tp_mouse_orientation_toggle(void) {
    int32_t next = tp_mouse_orientation_reversed() ? 0 : 1;
    atomic_set(&mouse_orientation_snapshot, next);
    return next != 0;
}
static uint32_t scroll_shoulder_bit(uint32_t position) {
    if (position == TP_LEFT_SHOULDER_POSITION) { return TP_SCROLL_SHOULDER_LEFT_BIT; }
    if (position == TP_RIGHT_SHOULDER_POSITION) { return TP_SCROLL_SHOULDER_RIGHT_BIT; }
    return 0U;
}
void tp_scroll_shoulder_modifier_update(uint32_t position, bool pressed) {
    uint32_t bit = scroll_shoulder_bit(position);
    if (bit == 0U) { return; }
    uint32_t current = (uint32_t)atomic_get(&scroll_shoulder_modifier_snapshot);
    uint32_t next = pressed ? (current | bit) : (current & ~bit);
    atomic_set(&scroll_shoulder_modifier_snapshot, (int32_t)next);
}
bool tp_scroll_shoulder_slow_active(void) {
    return tp_scroll_is_locked() &&
           ((uint32_t)atomic_get(&scroll_shoulder_modifier_snapshot) != 0U);
}
bool tp_aa_is_pressed(void) {
    return ((uint32_t)atomic_get(&scroll_mode_snapshot) & TP_MODE_PRESSED) != 0;
}

uint32_t tp_mode_scroll_snapshot(void) {
    return (uint32_t)atomic_get(&scroll_mode_snapshot);
}
uint32_t tp_mode_speed_snapshot(void) {
    return (uint32_t)atomic_get(&cursor_speed_snapshot);
}
uint32_t tp_mode_shoulder_snapshot(void) {
    return (uint32_t)atomic_get(&scroll_shoulder_modifier_snapshot);
}
void tp_mode_init(void) {
    atomic_set(&scroll_shoulder_modifier_snapshot, 0);
}
