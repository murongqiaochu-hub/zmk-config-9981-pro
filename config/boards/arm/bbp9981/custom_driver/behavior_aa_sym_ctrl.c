/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_aa_sym_ctrl

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/events/keycode_state_changed.h>

#include "a320_0x57.h"
#include "aa_sym_ctrl.h"

/* Key bindings and position events run on ZMK's system workqueue. */
static bool ctrl_owned;

int tp_sym_ctrl_release(int64_t timestamp) {
    if (!ctrl_owned) {
        return 0;
    }
    /* Keep ownership on failure so a later physical/binding edge can retry.
     * Never decrement another physical/sticky Ctrl key's reference. */
    int ret = raise_zmk_keycode_state_changed_from_encoded(LCTRL, false, timestamp);
    if (ret >= 0) { ctrl_owned = false; }
    return ret;
}

static int tp_sym_ctrl_press(int64_t timestamp) {
    if (ctrl_owned) { return ZMK_BEHAVIOR_OPAQUE; }
    int ret = raise_zmk_keycode_state_changed_from_encoded(LCTRL, true, timestamp);
    if (ret >= 0) { ctrl_owned = true; }
    return ret;
}

int tp_sym_ctrl_update(bool aa_down, bool sym_down, int64_t timestamp) {
    if (!aa_down || !sym_down) { return tp_sym_ctrl_release(timestamp); }
    /* Normal/default, SYM and UPPER layers only; don't add a Ctrl chord to MEDIA.
     * Once owned, only physical release ends it, even if another layer changes. */
    if (!ctrl_owned && zmk_keymap_highest_layer_active() > 2) { return 0; }
    return tp_sym_ctrl_press(timestamp);
}

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static int sym_pressed(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    (void)binding;
    if (!tp_aa_is_pressed()) {
        /* Sticky Layer 2 with aA already released: preserve &to DEFAULT. */
        return zmk_keymap_layer_to(0);
    }
    return tp_sym_ctrl_press(event.timestamp);
}

static int sym_released(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    (void)binding;
    return tp_sym_ctrl_release(event.timestamp);
}

static const struct behavior_driver_api sym_ctrl_api = {
    .binding_pressed = sym_pressed,
    .binding_released = sym_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define SYM_CTRL_INST(n)                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sym_ctrl_api);
DT_INST_FOREACH_STATUS_OKAY(SYM_CTRL_INST)
#endif
