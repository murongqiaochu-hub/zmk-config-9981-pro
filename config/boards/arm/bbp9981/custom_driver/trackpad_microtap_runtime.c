/* SPDX-License-Identifier: MIT
 * Motion-only gesture service. A stationary finger is never claimed to be UP.
 * Owned by the system workqueue; event notifications publish atomic epochs only.
 * A bounded FIFO preserves click/motion order, including consecutive clicks.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/hid.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/events/usb_conn_state_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/events/ble_active_profile_changed.h>
#endif
#include "trackpad_microtap_runtime.h"
#include "trackpad_microtap.h"
#include "trackpad_tap_output.h"
#include "menu_click_diag.h"

LOG_MODULE_REGISTER(tp_microtap, CONFIG_A320_LOG_LEVEL);
extern int bbp9981_hid_mouse_button_references(unsigned int button);

#define MICRO_QUEUE_CAPACITY 128U
#define MICRO_CLICK_MAX_AGE_MS 500U
struct micro_event {
    struct tp_microtap_frame motion;
    uint32_t up_before;
    bool click;
    bool started;
};
static struct micro_event events[MICRO_QUEUE_CAPACITY];
static unsigned int head, count;
static struct tp_microtap_state gesture;
static atomic_t enabled;
static atomic_t change_epoch;
static atomic_t shoulder_busy;
static uint32_t seen_epoch, seen_mode, seen_speed, usb_generation;
static struct zmk_endpoint_instance seen_endpoint;
static bool initialized, have_context;
static uint32_t context_epoch = 1;

bool tp_microtap_runtime_enabled(void) { return atomic_get(&enabled) != 0; }
void tp_microtap_runtime_interrupt(void) { (void)atomic_inc(&change_epoch); }
void tp_microtap_runtime_toggle(void) {
    atomic_set(&enabled, tp_microtap_runtime_enabled() ? 0 : 1);
    tp_microtap_runtime_interrupt();
    LOG_INF("Motion click %s", tp_microtap_runtime_enabled() ? "ON" : "OFF");
}
void tp_microtap_runtime_shoulder_busy(bool busy) {
    atomic_set(&shoulder_busy, busy ? 1 : 0);
}
bool tp_microtap_runtime_pending(void) {
    return count != 0U || tp_microtap_pending(&gesture);
}
void tp_microtap_runtime_init(void) {
    if (initialized) { return; }
    initialized = true;
    tp_microtap_reset(&gesture);
    /* Runtime default is OFF; never save a guessed gesture policy in settings. */
    atomic_set(&enabled, 0);
    (void)tp_tap_output_init();
}
static void pop_event(void) {
    head = (head + 1U) % MICRO_QUEUE_CAPACITY;
    count--;
}
static bool enqueue(struct micro_event event) {
    if (count >= MICRO_QUEUE_CAPACITY) {
        /* No overwrite/coalescing of clicks. Fail the experimental mode closed.
         * A stalled transport cannot guarantee lossless unbounded motion. */
        atomic_set(&enabled, 0);
        tp_microtap_reset(&gesture);
        (void)tp_tap_output_cancel();
        LOG_ERR("Motion-click queue overflow; feature disabled");
        return false;
    }
    events[(head + count) % MICRO_QUEUE_CAPACITY] = event;
    count++;
    return true;
}
static bool foreign_buttons(struct tp_tap_output_inflight out, bool *known) {
    *known = true;
    bool foreign = false;
    for (unsigned int i = 0; i < 5U; i++) {
        int refs = bbp9981_hid_mouse_button_references(i);
        int ours = (i == 0U && out.owns_left) ? 1 : 0;
        if (refs < ours) { *known = false; }
        if (refs > ours) { foreign = true; }
    }
    return foreign;
}
static void drain(uint32_t now, bool allow_click, tp_microtap_emit_fn emit, void *ctx) {
    while (count != 0U) {
        struct micro_event *event = &events[head];
        struct tp_tap_output_inflight out = tp_tap_output_read_inflight();
        if (out.phase == TP_TAP_OUTPUT_FAULT) {
            atomic_set(&enabled, 0);
            allow_click = false;
            /* Never release a foreign ref, nor move while our ref is held.
             * Once our ref is gone, ordinary zero-button movement can resume. */
            if (out.owns_left) { return; }
        }
        if (event->click) {
            if (event->started) {
                if (out.busy && out.phase != TP_TAP_OUTPUT_FAULT) { return; }
                /* Both successful and explicitly failed clicks finish here;
                 * a failed DOWN is never counted as a successful host click. */
                if (out.up_reports == event->up_before && out.last_error != 0) {
                    LOG_ERR("Motion click failed: %d", out.last_error);
                }
                pop_event();
                continue;
            }
            if (!allow_click || now - event->motion.at_ms > MICRO_CLICK_MAX_AGE_MS) {
                pop_event();
                continue;
            }
            if (out.busy) { return; }
            int ret = tp_tap_output_submit();
            if (ret == -EBUSY) { return; }
            if (ret != 0) {
                LOG_ERR("Motion click rejected: %d", ret);
                pop_event();
                continue;
            }
            event->started = true;
            event->up_before = out.up_reports;
            return;
        }
        if (out.owns_left || (out.busy && out.phase != TP_TAP_OUTPUT_FAULT)) { return; }
        emit(ctx, event->motion.dx, event->motion.dy, event->motion.at_ms);
        pop_event();
    }
}
void tp_microtap_runtime_poll(uint32_t now, int16_t dx, int16_t dy, bool valid,
                              bool scroll, uint32_t mode_epoch, uint32_t speed_epoch,
                              tp_microtap_emit_fn emit, void *ctx) {
    if (!initialized) { tp_microtap_runtime_init(); }
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    uint32_t usb = 0;
#if IS_ENABLED(CONFIG_ZMK_USB)
    usb = bbp9981_usb_hid_connection_epoch();
#endif
    uint32_t epoch = (uint32_t)atomic_get(&change_epoch);
    bool changed = have_context && (epoch != seen_epoch || mode_epoch != seen_mode ||
        speed_epoch != seen_speed || usb != usb_generation ||
        !zmk_endpoint_instance_eq(endpoint, seen_endpoint));
    if (changed) {
        bbp_menu_diag_note(MD_MICRO,2,(int32_t)epoch,(int32_t)mode_epoch,(int32_t)speed_epoch);
        /* Do not replay old pointer/click candidates across a layer/host/mode
         * switch. Runtime changes deliberately cancel undecided motion. */
        tp_microtap_reset(&gesture);
        head = count = 0;
        context_epoch++;
        (void)tp_tap_output_cancel();
    }
    have_context = true;
    seen_epoch = epoch; seen_mode = mode_epoch; seen_speed = speed_epoch;
    seen_endpoint = endpoint; usb_generation = usb;
    struct tp_tap_output_inflight out = tp_tap_output_read_inflight();
    bool known;
    bool foreign = foreign_buttons(out, &known);
    bool allowed = tp_microtap_runtime_enabled() && !scroll && known && !foreign &&
                   atomic_get(&shoulder_busy) == 0 && valid && !changed;
    struct tp_tap_output_context output = {.epoch = context_epoch, .enabled = allowed,
        .physical_left_known = known, .physical_left_down = foreign};
    (void)tp_tap_output_update_context(output);
    if (scroll) { return; } /* Original driver owns wheel, never replay XY as wheel. */
    drain(now, allowed, emit, ctx);
    struct tp_microtap_result result = tp_microtap_step(&gesture, now, dx, dy, valid,
        tp_microtap_runtime_enabled(), !allowed);
    /* An old candidate can click while this poll starts a NEW movement burst.
     * Queue the old click first; never reset that newly buffered burst. */
    if (result.click && allowed) {
        bbp_menu_diag_note(MD_MICRO,1,allowed,(int32_t)count,0);
        (void)enqueue((struct micro_event){.click = true, .motion.at_ms = now});
    }
    for (unsigned int i = 0; i < result.count; i++) {
        if (!enqueue((struct micro_event){.motion = result.frames[i]})) {
            break;
        }
    }
    drain(now, allowed, emit, ctx);
}
static int lifecycle(const zmk_event_t *event) {
    (void)event;
    tp_microtap_runtime_interrupt();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(tp_microtap_lifecycle, lifecycle);
ZMK_SUBSCRIPTION(tp_microtap_lifecycle, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(tp_microtap_lifecycle, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(tp_microtap_lifecycle, zmk_ble_active_profile_changed);
#endif
