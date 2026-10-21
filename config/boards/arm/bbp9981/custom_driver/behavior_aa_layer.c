/* SPDX-License-Identifier: MIT
 * Arbitrate right-aA's momentary, SDK-sticky and fixed UPPER owners.
 * A delayed SDK sticky UP must not release a subsequently pinned layer.
 * Keep the SDK hold-tap and sticky-key timing/quick-release implementations.
 */
#define DT_DRV_COMPAT zmk_behavior_aa_layer

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zephyr/logging/log.h>

#include "trackpad_key_state.h"

LOG_MODULE_REGISTER(aa_layer, CONFIG_A320_LOG_LEVEL);

#define AA_UPPER_LAYER 2
#define AA_LAYER_MOMENTARY 0U
#define AA_LAYER_STICKY 1U
#define AA_LAYER_FIXED 2U

/* These bindings are restricted to the single physical right-aA position.
 * SDK behavior callbacks and layer events execute on the serialized input queue.
 */
static bool aa_momentary_owned;
static bool aa_sticky_owned;
static bool aa_fixed_owned;
static unsigned aa_internal_transition;

static void aa_layer_clear_owners(void) {
    aa_momentary_owned = aa_sticky_owned = aa_fixed_owned = false;
}

static int aa_layer_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    if (event.position != TP_AA_POSITION || binding->param1 > AA_LAYER_FIXED) {
        LOG_ERR("Invalid aA layer owner: position=%u operation=%u", event.position,
                binding->param1);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (binding->param1 == AA_LAYER_MOMENTARY) {
        aa_momentary_owned = true;
    } else if (binding->param1 == AA_LAYER_STICKY) {
        aa_sticky_owned = true;
    } else {
        /* The SDK will eventually clean up its sticky slot; its late release
         * no longer owns this layer and must become an idempotent no-op.
         */
        aa_sticky_owned = false;
        aa_fixed_owned = true;
    }

    ++aa_internal_transition;
    int ret = binding->param1 == AA_LAYER_FIXED ? zmk_keymap_layer_to(AA_UPPER_LAYER)
                                               : zmk_keymap_layer_activate(AA_UPPER_LAYER);
    --aa_internal_transition;
    if (ret < 0 || !zmk_keymap_layer_active(AA_UPPER_LAYER)) {
        aa_layer_clear_owners();
        LOG_ERR("Cannot activate aA layer: %d", ret);
    }
    return ret;
}

static int aa_layer_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    if (event.position != TP_AA_POSITION) { return ZMK_BEHAVIOR_OPAQUE; }
    bool *owner;
    switch (binding->param1) {
    case AA_LAYER_MOMENTARY: owner = &aa_momentary_owned; break;
    case AA_LAYER_STICKY: owner = &aa_sticky_owned; break;
    default: return ZMK_BEHAVIOR_OPAQUE; /* Fixed owner has no physical UP. */
    }
    if (!*owner) { return ZMK_BEHAVIOR_OPAQUE; }
    *owner = false;
    if (!aa_momentary_owned && !aa_sticky_owned && !aa_fixed_owned) {
        return zmk_keymap_layer_deactivate(AA_UPPER_LAYER);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int aa_layer_state_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev != NULL && ev->layer == AA_UPPER_LAYER && !ev->state &&
        aa_internal_transition == 0U) {
        /* Explicit SYM/to/Studio exit wins. Never reactivate on a late UP. */
        aa_layer_clear_owners();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(aa_layer_owner, aa_layer_state_listener);
ZMK_SUBSCRIPTION(aa_layer_owner, zmk_layer_state_changed);

static const struct behavior_driver_api aa_layer_api = {
    .binding_pressed = aa_layer_pressed,
    .binding_released = aa_layer_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
#define AA_LAYER_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &aa_layer_api);
DT_INST_FOREACH_STATUS_OKAY(AA_LAYER_INST)
#endif
