/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_cursor_speed

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "trackpad_cursor_speed.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
#if IS_ENABLED(CONFIG_BBP9981_CURSOR_SPEED)
/* BBP9981 has 42 positions. Track ownership by captured physical position, not
 * current layer or binding pointer. ZMK retains DOWN's layer state for normal
 * cross-layer UP, so leaving UPPER does not strand the held bit. Live Studio
 * rebinding while held is not covered. No ordinary-key listener, timer, repeat,
 * synthetic keycode, or layer observer is installed here.
 */
#define CURSOR_SPEED_POSITIONS 42U
static struct k_spinlock cursor_speed_held_lock;
static uint64_t cursor_speed_held;

static int cursor_speed_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    (void)binding;
    if (event.position >= CURSOR_SPEED_POSITIONS) {
        return ZMK_BEHAVIOR_OPAQUE;
    }
    uint64_t bit = UINT64_C(1) << event.position;
    k_spinlock_key_t key = k_spin_lock(&cursor_speed_held_lock);
    bool first_down = (cursor_speed_held & bit) == 0U;
    cursor_speed_held |= bit;
    k_spin_unlock(&cursor_speed_held_lock, key);
    if (first_down) {
        (void)bbp_cursor_speed_cycle();
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int cursor_speed_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    (void)binding;
    if (event.position < CURSOR_SPEED_POSITIONS) {
        uint64_t bit = UINT64_C(1) << event.position;
        k_spinlock_key_t key = k_spin_lock(&cursor_speed_held_lock);
        cursor_speed_held &= ~bit;
        k_spin_unlock(&cursor_speed_held_lock, key);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

#else
/* DTS is generated before Kconfig: the normal node always exists. Keep a
 * linkable opaque no-op when disabled, with no service/work/settings refs.
 */
static int cursor_speed_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    (void)binding; (void)event;
    return ZMK_BEHAVIOR_OPAQUE;
}
static int cursor_speed_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    (void)binding; (void)event;
    return ZMK_BEHAVIOR_OPAQUE;
}
#endif /* CONFIG_BBP9981_CURSOR_SPEED */

static const struct behavior_driver_api cursor_speed_api = {
    .binding_pressed = cursor_speed_pressed,
    .binding_released = cursor_speed_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define CURSOR_SPEED_INST(n)                                                                       \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &cursor_speed_api);
DT_INST_FOREACH_STATUS_OKAY(CURSOR_SPEED_INST)
#endif
