/* SPDX-License-Identifier: MIT
 * Default-layer left shoulder: stock single/double/drag, consumed triple toggle.
 * Keymap and timeout/driver contexts are serialized by a mutex. This adapter is
 * thread-context only, like the stock mouse behavior's K_FOREVER input enqueue.
 */
#define DT_DRV_COMPAT zmk_behavior_microtap_shoulder
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/mouse.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/events/usb_conn_state_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/events/ble_active_profile_changed.h>
#endif
#include <zephyr/logging/log.h>
#include "trackpad_shoulder.h"
#include "trackpad_microtap_runtime.h"

LOG_MODULE_REGISTER(tp_shoulder, CONFIG_A320_LOG_LEVEL);
K_MUTEX_DEFINE(shoulder_lock);
static struct tp_shoulder_state shoulder;
static struct k_work_delayable timeout_work;
static struct zmk_behavior_binding_event origin;
static bool initialized, forwarded_down, release_pending;
static unsigned int release_attempts;
static atomic_t lifecycle_epoch;
static uint32_t observed_epoch;
static const struct zmk_behavior_binding stock_left = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(mkp)), .param1 = LCLK,
};
static void tick(struct k_work *work);
static void ensure_init(void) {
    if (!initialized) {
        initialized = true;
        tp_shoulder_reset(&shoulder);
        k_work_init_delayable(&timeout_work, tick);
        observed_epoch = (uint32_t)atomic_get(&lifecycle_epoch);
    }
}
/* The pinned stock mkp handler returns 0 after enqueueing input. A negative
 * invoke return means it did not reach that handler; do not emit an orphan UP.
 * Input acceptance is NOT a claim of transport/host acknowledgement. */
static void dispatch(struct tp_shoulder_edge edge) {
    if (edge.pressed == forwarded_down) { return; }
    struct zmk_behavior_binding_event event = origin;
    event.timestamp = (int64_t)edge.at_ms;
    int ret = zmk_behavior_invoke_binding(&stock_left, event, edge.pressed);
    if (ret < 0) {
        LOG_ERR("Left shoulder replay failed: %d", ret);
        if (!edge.pressed && forwarded_down) {
            release_pending = true;
            release_attempts++;
        }
        return;
    }
    forwarded_down = edge.pressed;
    if (!edge.pressed) { release_pending = false; release_attempts = 0; }
}
static void observe_lifecycle(uint32_t now) {
    uint32_t epoch = (uint32_t)atomic_get(&lifecycle_epoch);
    if (epoch == observed_epoch) { return; }
    observed_epoch = epoch;
    bool held = shoulder.down;
    /* Cancel delayed clicks, NOT flush them onto a different host/layer.
     * Revoke only this adapter's accepted DOWN; other mouse owners are stock refs. */
    if (forwarded_down) { dispatch((struct tp_shoulder_edge){now, false}); }
    shoulder = (struct tp_shoulder_state){.down = held || forwarded_down,
        .output_down = forwarded_down, .passthrough = held || forwarded_down};
}
static void schedule_next(void) {
    bool busy = tp_shoulder_busy(&shoulder) || release_pending;
    tp_microtap_runtime_shoulder_busy(busy);
    if (tp_shoulder_busy(&shoulder) || (release_pending && release_attempts < 20U)) {
        (void)k_work_reschedule(&timeout_work, K_MSEC(10));
    } else {
        (void)k_work_cancel_delayable(&timeout_work);
    }
}
static void apply(struct tp_shoulder_result result) {
    for (unsigned int i = 0; i < result.count; i++) { dispatch(result.edges[i]); }
    if (result.toggle) { tp_microtap_runtime_toggle(); }
    schedule_next();
}
static void tick(struct k_work *work) {
    (void)work;
    (void)k_mutex_lock(&shoulder_lock, K_FOREVER);
    uint32_t now = k_uptime_get_32();
    observe_lifecycle(now);
    if (release_pending && release_attempts < 20U) {
        dispatch((struct tp_shoulder_edge){now, false});
    }
    apply(tp_shoulder_step(&shoulder, now, false, false));
    (void)k_mutex_unlock(&shoulder_lock);
}
/* Commit the real shoulder DOWN before XY for drag; release-only candidates are
 * committed before new pointer movement. This also ends triple recognition. */
void tp_microtap_shoulder_motion(uint32_t now) {
    (void)k_mutex_lock(&shoulder_lock, K_FOREVER);
    if (initialized) {
        observe_lifecycle(now);
        if (tp_shoulder_busy(&shoulder)) { apply(tp_shoulder_flush(&shoulder, now)); }
    }
    (void)k_mutex_unlock(&shoulder_lock);
}
static int other_key(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event && event->state && event->position != 0U) {
        tp_microtap_runtime_interrupt();
        (void)k_mutex_lock(&shoulder_lock, K_FOREVER);
        if (initialized) {
            observe_lifecycle(k_uptime_get_32());
            if (tp_shoulder_busy(&shoulder)) {
                apply(tp_shoulder_flush(&shoulder, k_uptime_get_32()));
            }
        }
        (void)k_mutex_unlock(&shoulder_lock);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
static int shoulder_lifecycle(const zmk_event_t *eh) {
    (void)eh;
    /* No mutex or input sends from lifecycle callback/possible interrupt context. */
    (void)atomic_inc(&lifecycle_epoch);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(tp_shoulder_other_key, other_key);
ZMK_SUBSCRIPTION(tp_shoulder_other_key, zmk_position_state_changed);
ZMK_LISTENER(tp_shoulder_lifecycle, shoulder_lifecycle);
ZMK_SUBSCRIPTION(tp_shoulder_lifecycle, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(tp_shoulder_lifecycle, zmk_layer_state_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(tp_shoulder_lifecycle, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(tp_shoulder_lifecycle, zmk_ble_active_profile_changed);
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)k_mutex_lock(&shoulder_lock, K_FOREVER);
    ensure_init();
    observe_lifecycle(k_uptime_get_32());
    origin = event;
    tp_microtap_runtime_interrupt();
    apply(tp_shoulder_step(&shoulder, (uint32_t)event.timestamp, true, true));
    (void)k_mutex_unlock(&shoulder_lock);
    return ZMK_BEHAVIOR_OPAQUE;
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)k_mutex_lock(&shoulder_lock, K_FOREVER);
    ensure_init();
    observe_lifecycle(k_uptime_get_32());
    origin = event;
    apply(tp_shoulder_step(&shoulder, (uint32_t)event.timestamp, true, false));
    (void)k_mutex_unlock(&shoulder_lock);
    return ZMK_BEHAVIOR_OPAQUE;
}
static const struct behavior_driver_api api = {
    .binding_pressed = pressed,
    .binding_released = released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};
#define SHOULDER_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
DT_INST_FOREACH_STATUS_OKAY(SHOULDER_INST)
#endif
