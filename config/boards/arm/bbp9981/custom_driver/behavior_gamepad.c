/* SPDX-License-Identifier: MIT
 * Thin ZMK behavior adapter; runtime owns state, FIFO and transport work.
 */
#define DT_DRV_COMPAT zmk_behavior_gamepad
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include "gamepad_runtime.h"

static int binding_key(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event, bool pressed) {
    int ret = bbp_gamepad_key_event(event.position, binding->param1, pressed, event.timestamp);
    return ret < 0 ? ret : ZMK_BEHAVIOR_OPAQUE;
}
static int pressed(struct zmk_behavior_binding *b, struct zmk_behavior_binding_event e) {
    return binding_key(b, e, true);
}
static int released(struct zmk_behavior_binding *b, struct zmk_behavior_binding_event e) {
    return binding_key(b, e, false);
}
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static const struct behavior_driver_api gamepad_api = {.binding_pressed=pressed, .binding_released=released};
#define GAMEPAD_INST(n) BEHAVIOR_DT_INST_DEFINE(n,NULL,NULL,NULL,NULL,POST_KERNEL,CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,&gamepad_api);
DT_INST_FOREACH_STATUS_OKAY(GAMEPAD_INST)
#endif
