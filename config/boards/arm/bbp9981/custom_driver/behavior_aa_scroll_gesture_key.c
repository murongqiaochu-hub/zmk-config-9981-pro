/* SPDX-License-Identifier: MIT
 * UPPER-layer key gestures. The locked-scroll gestures use ordinary key switches,
 * while the unlocked Bluetooth slot actions remain available outside those contexts.
 */
#define DT_DRV_COMPAT zmk_behavior_aa_scroll_gesture_key

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/bt.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zephyr/logging/log.h>

#include "trackpad_scroll_lock.h"

LOG_MODULE_REGISTER(aa_scroll_gesture_key, CONFIG_A320_LOG_LEVEL);

enum gesture_mode {
    GESTURE_NONE = 0,
    GESTURE_LEFT_TO_UP,
    GESTURE_RIGHT_TO_UP,
    GESTURE_MOUSE_TOGGLE,
    GESTURE_SWALLOW,
};

struct gesture_state {
    uint32_t down_mask;
    uint32_t eligible_mask;
    uint32_t sequence_position;
    uint8_t tap_count;
    int64_t press_at;
    int64_t release_at;
    enum gesture_mode mode;
};

/* Key events and the lock-state listener share the serialized ZMK input queue. */
static struct gesture_state gesture = {.sequence_position = UINT32_MAX};
static uint32_t bt_forwarded_mask;

static const struct zmk_behavior_binding bt_slot_1 = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(bt)),
    .param1 = BT_SEL,
    .param2 = 1U,
};
static const struct zmk_behavior_binding bt_slot_2 = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(bt)),
    .param1 = BT_SEL,
    .param2 = 2U,
};

static uint32_t key_bit(uint32_t position) {
    if (position == TP_BLACKBERRY_POSITION) { return 1U; }
    if (position == TP_BACK_POSITION) { return 2U; }
    if (position == TP_MOUSE_TOGGLE_POSITION) { return 4U; }
    return 0U;
}

static void clear_sequence(void) {
    uint32_t down = gesture.down_mask;
    gesture = (struct gesture_state){
        .down_mask = down,
        .sequence_position = UINT32_MAX,
    };
}

void tp_scroll_gesture_cancel(void) {
    /* Retain held edges so their releases are swallowed, not reinterpreted. */
    clear_sequence();
}

static enum gesture_mode current_mode(uint32_t position) {
    if (position == TP_MOUSE_TOGGLE_POSITION) {
        return tp_scroll_aa_is_pressed() ? GESTURE_MOUSE_TOGGLE : GESTURE_SWALLOW;
    }
    /* An explicit aA+BT chord always keeps the original immediate slot action,
     * including during a latched session's temporary aA override.
     */
    if (tp_scroll_aa_is_pressed()) { return GESTURE_NONE; }
    if (tp_scroll_is_locked()) {
        if (position == TP_BLACKBERRY_POSITION) { return GESTURE_LEFT_TO_UP; }
        if (position == TP_BACK_POSITION) { return GESTURE_RIGHT_TO_UP; }
    }
    return GESTURE_NONE;
}

static void apply_gesture(void) {
    switch (gesture.mode) {
    case GESTURE_LEFT_TO_UP:
        tp_scroll_rotation_step(TP_SCROLL_ROTATION_LEFT_TO_UP_STEP);
        break;
    case GESTURE_RIGHT_TO_UP:
        tp_scroll_rotation_step(TP_SCROLL_ROTATION_RIGHT_TO_UP_STEP);
        break;
    case GESTURE_MOUSE_TOGGLE:
        (void)tp_mouse_orientation_toggle();
        LOG_INF("Inverted mouse mode toggled");
        break;
    case GESTURE_NONE:
    case GESTURE_SWALLOW:
    default:
        break;
    }
}

static void update_gesture(uint32_t position, bool pressed, int64_t timestamp,
                           enum gesture_mode mode) {
    uint32_t bit = key_bit(position);
    if (bit == 0U || timestamp < 0) {
        tp_scroll_gesture_cancel();
        return;
    }

    if (pressed) {
        if ((gesture.down_mask & bit) != 0U) { return; }
        if (gesture.down_mask != 0U) {
            /* Overlapping BlackBerry/Back presses cannot form one gesture. */
            clear_sequence();
            gesture.down_mask |= bit;
            return;
        }
        if (gesture.sequence_position != UINT32_MAX &&
            gesture.sequence_position != position) {
            clear_sequence();
        }
        if (gesture.tap_count != 0U &&
            (timestamp < gesture.release_at ||
             timestamp - gesture.release_at > TP_SCROLL_GESTURE_GAP_MS)) {
            clear_sequence();
        }
        if (gesture.sequence_position == UINT32_MAX) {
            gesture.sequence_position = position;
            gesture.mode = mode;
        } else if (gesture.mode != mode) {
            clear_sequence();
            gesture.sequence_position = position;
            gesture.mode = mode;
        }
        gesture.down_mask |= bit;
        gesture.eligible_mask |= bit;
        gesture.press_at = timestamp;
        return;
    }

    if ((gesture.down_mask & bit) == 0U) { return; }
    gesture.down_mask &= ~bit;
    if ((gesture.eligible_mask & bit) == 0U) {
        if (gesture.down_mask == 0U) { clear_sequence(); }
        return;
    }
    gesture.eligible_mask &= ~bit;
    if (gesture.down_mask != 0U || gesture.sequence_position != position ||
        timestamp < gesture.press_at ||
        timestamp - gesture.press_at > TP_SCROLL_GESTURE_TAP_MS) {
        clear_sequence();
        return;
    }

    ++gesture.tap_count;
    gesture.release_at = timestamp;
    if ((gesture.mode == GESTURE_MOUSE_TOGGLE && gesture.tap_count == 2U) ||
        (gesture.mode != GESTURE_MOUSE_TOGGLE && gesture.tap_count == 3U)) {
        apply_gesture();
        clear_sequence();
    }
}

static const struct zmk_behavior_binding *slot_binding(uint32_t position) {
    if (position == TP_BLACKBERRY_POSITION) { return &bt_slot_1; }
    if (position == TP_BACK_POSITION) { return &bt_slot_2; }
    return NULL;
}

static int dispatch_slot(const struct zmk_behavior_binding *child,
                         struct zmk_behavior_binding_event event, bool pressed) {
    int ret = zmk_behavior_invoke_binding(child, event, pressed);
    if (ret != 0) {
        LOG_ERR("Bluetooth slot behavior %s failed: %d", pressed ? "press" : "release", ret);
    }
    return ret;
}

static int pressed(struct zmk_behavior_binding *binding,
                   struct zmk_behavior_binding_event event) {
    (void)binding;
    uint32_t bit = key_bit(event.position);
    const struct zmk_behavior_binding *child = slot_binding(event.position);
    if (bit == 0U) {
        LOG_ERR("Unexpected UPPER gesture key at position %u", event.position);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    enum gesture_mode mode = current_mode(event.position);
    if (mode == GESTURE_NONE) {
        /* Selecting the already-active BT slot raises no endpoint event, but it
         * is still another key and must break an incomplete F double-tap.
         */
        tp_scroll_gesture_cancel();
        if (child != NULL && dispatch_slot(child, event, true) == 0) {
            bt_forwarded_mask |= bit;
        }
        return ZMK_BEHAVIOR_OPAQUE;
    }

    update_gesture(event.position, true, event.timestamp, mode);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int released(struct zmk_behavior_binding *binding,
                    struct zmk_behavior_binding_event event) {
    (void)binding;
    uint32_t bit = key_bit(event.position);
    const struct zmk_behavior_binding *child = slot_binding(event.position);
    if (bit == 0U) {
        LOG_ERR("Unexpected UPPER gesture-key release at position %u", event.position);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if ((bt_forwarded_mask & bit) != 0U) {
        if (child != NULL) { (void)dispatch_slot(child, event, false); }
        bt_forwarded_mask &= ~bit;
        return ZMK_BEHAVIOR_OPAQUE;
    }

    /* The sequence's mode is captured on DOWN; a layer/lock edge cancels it. */
    update_gesture(event.position, false, event.timestamp, gesture.mode);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int gesture_context_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(eh);
    if ((layer != NULL && layer->layer == 2U && !layer->state) ||
        as_zmk_endpoint_changed(eh) != NULL) {
        tp_scroll_gesture_cancel();
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(aa_scroll_gesture_context, gesture_context_listener);
ZMK_SUBSCRIPTION(aa_scroll_gesture_context, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(aa_scroll_gesture_context, zmk_endpoint_changed);

static const struct behavior_driver_api api = {
    .binding_pressed = pressed,
    .binding_released = released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
#define AA_SCROLL_GESTURE_KEY_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
DT_INST_FOREACH_STATUS_OKAY(AA_SCROLL_GESTURE_KEY_INST)
#endif
