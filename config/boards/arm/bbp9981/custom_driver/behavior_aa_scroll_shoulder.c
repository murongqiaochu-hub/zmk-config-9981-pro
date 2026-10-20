/* SPDX-License-Identifier: MIT
 * aA-layer shoulders: native wheel while unlocked, half-speed modifier and one
 * quarter-turn per completed triple tap while locked. Native msc owns acceleration.
 */
#define DT_DRV_COMPAT zmk_behavior_aa_scroll_shoulder

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/mouse.h>
#include <zmk/behavior.h>
#include <zephyr/logging/log.h>

#include "trackpad_scroll_lock.h"

LOG_MODULE_REGISTER(aa_scroll_shoulder, CONFIG_A320_LOG_LEVEL);

static atomic_t locked_modifier_mask;
static atomic_t native_scroll_mask;
static atomic_t scroll_rotation_snapshot;

/* Serialized with the position listener on the ZMK system workqueue. */
struct scroll_shoulder_taps {
    uint32_t down_mask;
    uint32_t eligible_mask;
    uint32_t sequence_position;
    uint8_t tap_count;
    int64_t press_at;
    int64_t release_at;
};
static struct scroll_shoulder_taps shoulder_taps = {.sequence_position = UINT32_MAX};

static const struct zmk_behavior_binding native_scroll_up = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(msc)),
    .param1 = SCRL_UP,
};
static const struct zmk_behavior_binding native_scroll_down = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(msc)),
    .param1 = SCRL_DOWN,
};

static uint32_t shoulder_bit(uint32_t position) {
    if (position == TP_LEFT_SHOULDER_POSITION) { return TP_SCROLL_SHOULDER_LEFT_BIT; }
    if (position == TP_RIGHT_SHOULDER_POSITION) { return TP_SCROLL_SHOULDER_RIGHT_BIT; }
    return 0U;
}

void tp_scroll_shoulder_tap_cancel(void) {
    uint32_t down = shoulder_taps.down_mask;
    shoulder_taps = (struct scroll_shoulder_taps){
        .down_mask = down,
        .sequence_position = UINT32_MAX,
    };
}

void tp_scroll_rotation_reset(void) {
    atomic_set(&scroll_rotation_snapshot, 0);
    shoulder_taps = (struct scroll_shoulder_taps){.sequence_position = UINT32_MAX};
}

uint8_t tp_scroll_rotation_quadrants(void) {
    return (uint8_t)atomic_get(&scroll_rotation_snapshot) & TP_SCROLL_ROTATION_MASK;
}

static void scroll_rotation_step(uint32_t position) {
    uint8_t current = tp_scroll_rotation_quadrants();
    if (position == TP_RIGHT_SHOULDER_POSITION) {
        current = (current + TP_SCROLL_ROTATION_RIGHT_STEP) & TP_SCROLL_ROTATION_MASK;
    } else if (position == TP_LEFT_SHOULDER_POSITION) {
        current = (current + TP_SCROLL_ROTATION_LEFT_STEP) & TP_SCROLL_ROTATION_MASK;
    } else {
        return;
    }
    atomic_set(&scroll_rotation_snapshot, (int32_t)current);
}

void tp_scroll_shoulder_tap_update(uint32_t position, bool pressed, int64_t timestamp) {
    uint32_t bit = shoulder_bit(position);
    if (bit == 0U || timestamp < 0 || !tp_scroll_is_locked()) {
        tp_scroll_shoulder_tap_cancel();
        return;
    }

    if (pressed) {
        if ((shoulder_taps.down_mask & bit) != 0U) { return; }
        if (shoulder_taps.down_mask != 0U) {
            /* Overlapping shoulders cannot be one clean triple-tap gesture. */
            tp_scroll_shoulder_tap_cancel();
            shoulder_taps.down_mask |= bit;
            return;
        }
        if (shoulder_taps.sequence_position != UINT32_MAX &&
            shoulder_taps.sequence_position != position) {
            tp_scroll_shoulder_tap_cancel();
        }
        if (shoulder_taps.tap_count != 0U &&
            (timestamp < shoulder_taps.release_at ||
             timestamp - shoulder_taps.release_at > TP_SCROLL_SHOULDER_GAP_MS)) {
            tp_scroll_shoulder_tap_cancel();
        }
        if (shoulder_taps.sequence_position == UINT32_MAX) {
            shoulder_taps.sequence_position = position;
        }
        shoulder_taps.down_mask |= bit;
        shoulder_taps.eligible_mask |= bit;
        shoulder_taps.press_at = timestamp;
        return;
    }

    if ((shoulder_taps.down_mask & bit) == 0U) { return; }
    shoulder_taps.down_mask &= ~bit;
    if ((shoulder_taps.eligible_mask & bit) == 0U) {
        if (shoulder_taps.down_mask == 0U) { tp_scroll_shoulder_tap_cancel(); }
        return;
    }
    shoulder_taps.eligible_mask &= ~bit;
    if (shoulder_taps.down_mask != 0U || shoulder_taps.sequence_position != position ||
        timestamp < shoulder_taps.press_at ||
        timestamp - shoulder_taps.press_at > TP_SCROLL_SHOULDER_TAP_MS) {
        tp_scroll_shoulder_tap_cancel();
        return;
    }

    ++shoulder_taps.tap_count;
    shoulder_taps.release_at = timestamp;
    if (shoulder_taps.tap_count == 3U) {
        scroll_rotation_step(position);
        tp_scroll_shoulder_tap_cancel();
    }
}

static const struct zmk_behavior_binding *native_binding(
    const struct zmk_behavior_binding *binding) {
    if (binding->param1 == SCRL_UP) { return &native_scroll_up; }
    if (binding->param1 == SCRL_DOWN) { return &native_scroll_down; }
    return NULL;
}

/* Always consume the wrapper event, including native dispatch failure: returning
 * TRANSPARENT would also activate the lower-layer shoulder binding. */
static int dispatch_native(const struct zmk_behavior_binding *child,
                           struct zmk_behavior_binding_event event, bool pressed) {
    int ret = zmk_behavior_invoke_binding(child, event, pressed);
    if (ret != 0) {
        LOG_ERR("Native shoulder scroll %s failed: %d", pressed ? "press" : "release", ret);
    }
    return ret;
}

static int pressed(struct zmk_behavior_binding *binding,
                   struct zmk_behavior_binding_event event) {
    uint32_t bit = shoulder_bit(event.position);
    const struct zmk_behavior_binding *child = native_binding(binding);
    if (bit == 0U || child == NULL) {
        LOG_ERR("Unexpected aA scroll shoulder binding at position %u", event.position);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    uint32_t locked = (uint32_t)atomic_get(&locked_modifier_mask);
    uint32_t forwarded = (uint32_t)atomic_get(&native_scroll_mask);
    if ((locked & bit) != 0U) { return ZMK_BEHAVIOR_OPAQUE; }
    if ((forwarded & bit) != 0U) {
        /* Retry a release that failed on the previous physical edge before re-pressing. */
        if (dispatch_native(child, event, false) != 0) { return ZMK_BEHAVIOR_OPAQUE; }
        forwarded &= ~bit;
        atomic_set(&native_scroll_mask, (int32_t)forwarded);
    }

    if (tp_scroll_is_locked()) {
        atomic_set(&locked_modifier_mask, (int32_t)(locked | bit));
        tp_scroll_shoulder_modifier_update(event.position, true);
        tp_scroll_shoulder_tap_update(event.position, true, event.timestamp);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    tp_scroll_shoulder_tap_cancel();
    if (dispatch_native(child, event, true) == 0) {
        atomic_set(&native_scroll_mask, (int32_t)(forwarded | bit));
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int released(struct zmk_behavior_binding *binding,
                    struct zmk_behavior_binding_event event) {
    uint32_t bit = shoulder_bit(event.position);
    const struct zmk_behavior_binding *child = native_binding(binding);
    if (bit == 0U || child == NULL) {
        LOG_ERR("Unexpected aA scroll shoulder release at position %u", event.position);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    uint32_t locked = (uint32_t)atomic_get(&locked_modifier_mask);
    if ((locked & bit) != 0U) {
        tp_scroll_shoulder_modifier_update(event.position, false);
        tp_scroll_shoulder_tap_update(event.position, false, event.timestamp);
        atomic_set(&locked_modifier_mask, (int32_t)(locked & ~bit));
        return ZMK_BEHAVIOR_OPAQUE;
    }

    tp_scroll_shoulder_tap_cancel();
    uint32_t forwarded = (uint32_t)atomic_get(&native_scroll_mask);
    if ((forwarded & bit) != 0U && dispatch_native(child, event, false) == 0) {
        atomic_set(&native_scroll_mask, (int32_t)(forwarded & ~bit));
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api api = {
    .binding_pressed = pressed,
    .binding_released = released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
#define AA_SCROLL_SHOULDER_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
DT_INST_FOREACH_STATUS_OKAY(AA_SCROLL_SHOULDER_INST)
#endif
