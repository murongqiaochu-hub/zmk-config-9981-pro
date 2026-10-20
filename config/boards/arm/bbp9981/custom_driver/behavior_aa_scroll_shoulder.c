/* SPDX-License-Identifier: MIT
 * aA-layer shoulders: native wheel when aA is held, half-speed modifier when locked.
 * The native msc behavior owns repeat/acceleration; this adapter only gates lock mode.
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
        return ZMK_BEHAVIOR_OPAQUE;
    }

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
        atomic_set(&locked_modifier_mask, (int32_t)(locked & ~bit));
        return ZMK_BEHAVIOR_OPAQUE;
    }

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
