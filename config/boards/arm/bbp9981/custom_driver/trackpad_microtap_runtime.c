/* SPDX-License-Identifier: MIT
 * Motion-only gesture service. A stationary finger is never claimed to be UP.
 * Owned by the system workqueue; notifications publish epochs/guard activity only.
 * A bounded FIFO preserves click/motion order, including consecutive clicks.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/position_state_changed.h>
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
#define MICRO_NATIVE_GUARD_MS 600U
BUILD_ASSERT(TP_MICROTAP_MIN_PATH == 24U && TP_MICROTAP_QUIET_MS == 120U &&
             TP_MICROTAP_MAX_CANDIDATE_MS == 320U && TP_MICROTAP_MIN_FRAMES == 3U &&
             TP_MICROTAP_MAX_FRAMES == 8U && TP_MICROTAP_MAX_MOTION_SPAN_MS == 120U &&
             TP_MICROTAP_CLICK_INTERVAL_MS == 350U && MICRO_NATIVE_GUARD_MS == 600U &&
             TP_MICROTAP_MAX_PATH == 8192U && TP_MICROTAP_MAX_OFFSET == 4096U,
             "MICROTAP_POLICY must match the runtime ROM marker");
struct micro_event {
    struct tp_microtap_frame motion;
    uint32_t up_before;
    uint32_t native_generation;
    bool click;
    bool started;
};
static struct micro_event events[MICRO_QUEUE_CAPACITY];
static unsigned int head, count;
static struct tp_microtap_state gesture;
static atomic_t enabled;
static atomic_t change_epoch;
/* Deliberately outside gesture/context resets. Only these publication fields
 * are shared with notifications; no SDK/time/log/send call runs under irq_lock.
 * Held bits represent position events, NOT verified tap contact. A captured or
 * missing key-UP can leave auto fail-closed until a matching UP is observed;
 * ordinary motion and native button dispatch remain untouched. */
static struct {
    uint32_t generation;
    uint8_t held;
    bool shoulder_busy;
    bool refs_blocked;
} native_guard;
/* System-workqueue-owned observation clock; callbacks never publish timestamps
 * that could arrive out of order. Power-on zero-init alone clears this state. */
static uint32_t native_seen_generation, native_activity_at;
static bool native_have_activity;
struct native_guard_snapshot { uint32_t generation; bool blocked; };
static struct native_guard_snapshot native_guard_observe(bool refs_blocked) {
    unsigned int key = irq_lock();
    if (native_guard.refs_blocked != refs_blocked) {
        native_guard.refs_blocked = refs_blocked;
        native_guard.generation++;
    }
    uint32_t generation = native_guard.generation;
    bool held = native_guard.held != 0U || native_guard.shoulder_busy || refs_blocked;
    irq_unlock(key);
    uint32_t wall = k_uptime_get_32();
    if (generation != native_seen_generation || held) {
        native_seen_generation = generation;
        native_activity_at = wall;
        native_have_activity = true;
    }
    uint32_t elapsed = (uint32_t)(wall - native_activity_at);
    if (native_have_activity && elapsed >= UINT32_C(0x80000000)) {
        native_activity_at = wall; /* Backwards/ambiguous wall time fails closed. */
        elapsed = 0U;
    }
    if (native_have_activity && elapsed >= MICRO_NATIVE_GUARD_MS) {
        /* Retain the generation for queued-event cancellation, not an expired
         * timestamp that could look backwards again after half a u32 period. */
        native_have_activity = false;
    }
    return (struct native_guard_snapshot){.generation = generation,
        .blocked = held || native_have_activity};
}
/* Single-owner, short-lived bridge spacing. A completed acceptance must not
 * remain a forever modulo-time comparison against the SDK's historical field.
 * This also survives gesture/context resets; only a NEW down_reports identity
 * starts another interval. Normal idle polls retire the interval at 350ms. */
static struct {
    uint32_t reports, anchor;
    bool seen, live, reanchored;
} auto_spacing;
static bool auto_spacing_blocked(struct tp_tap_output_inflight out) {
    uint32_t wall = k_uptime_get_32();
    if (!auto_spacing.seen || out.down_reports != auto_spacing.reports) {
        bool accepted = auto_spacing.seen || out.down_reports != 0U;
        auto_spacing.seen = true;
        auto_spacing.reports = out.down_reports;
        auto_spacing.anchor = out.accepted_at;
        auto_spacing.live = accepted;
        auto_spacing.reanchored = false;
    }
    if (auto_spacing.live) {
        uint32_t elapsed = (uint32_t)(wall - auto_spacing.anchor);
        if (elapsed >= UINT32_C(0x80000000) && !auto_spacing.reanchored) {
            /* Future/ambiguous first observation: one conservative local window,
             * never reload this same stale accepted_at on subsequent polls. */
            auto_spacing.anchor = wall;
            auto_spacing.reanchored = true;
        } else if (elapsed >= TP_MICROTAP_CLICK_INTERVAL_MS) {
            auto_spacing.live = false;
        }
    }
    return auto_spacing.live;
}
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
    unsigned int key = irq_lock();
    /* Repeated false is idle, not new activity. True may refresh, and the
     * true->false edge covers same-poll shoulder flush DOWN/UP with refs==0. */
    if (busy || native_guard.shoulder_busy) { native_guard.generation++; }
    native_guard.shoulder_busy = busy;
    irq_unlock(key);
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
    LOG_INF("MICROTAP_POLICY floor=24 quiet=120 gap=350 native=600 range=8192/4096 frames=3..8 span=120 age=320");
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
static void drain(bool allow_click, tp_microtap_emit_fn emit, void *ctx) {
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
            /* FIFO motion keeps original sample times. Auto expiry instead
             * uses fresh wall time, including waits while earlier items drain. */
            uint32_t click_age = (uint32_t)(k_uptime_get_32() - event->motion.at_ms);
            if (!allow_click || click_age > MICRO_CLICK_MAX_AGE_MS ||
                click_age >= UINT32_C(0x80000000)) {
                pop_event();
                continue;
            }
            bool known;
            bool foreign = foreign_buttons(out, &known);
            struct native_guard_snapshot guard = native_guard_observe(!known || foreign);
            /* Recheck at actual submission, not only sample classification.
             * Generation also kills old queued decisions after the 600ms window
             * expires: native activity must never become a deferred auto click. */
            if (guard.blocked || event->native_generation != guard.generation) {
                pop_event();
                continue;
            }
            if (out.busy) { return; }
            /* A delayed queue must not compress two decisions into a rapid
             * second submission. Drop it now; never wait then catch up.
             * accepted_at is the last bridge/SDK acceptance, NOT host ACK.
             * The sample's now may predate that acceptance; use fresh wall time
             * and fail closed on backwards/half-range ambiguity. */
            if (auto_spacing_blocked(out)) {
                bbp_menu_diag_note(MD_MICRO,3,(int32_t)count,0,0);
                pop_event();
                continue;
            }
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
    (void)auto_spacing_blocked(out); /* Observe/expire even with no queued auto. */
    bool known;
    bool foreign = foreign_buttons(out, &known);
    struct native_guard_snapshot guard = native_guard_observe(!known || foreign);
    bool allowed = tp_microtap_runtime_enabled() && !scroll && !guard.blocked &&
                   valid && !changed;
    struct tp_tap_output_context output = {.epoch = context_epoch, .enabled = allowed,
        .physical_left_known = known, .physical_left_down = foreign};
    (void)tp_tap_output_update_context(output);
    if (scroll) { return; } /* Original driver owns wheel, never replay XY as wheel. */
    drain(allowed, emit, ctx);
    struct tp_microtap_result result = tp_microtap_step(&gesture, now, dx, dy, valid,
        tp_microtap_runtime_enabled(), !allowed);
    /* An old candidate can click while this poll starts a NEW movement burst.
     * Queue the old click first; never reset that newly buffered burst. */
    if (result.click && allowed) {
        bbp_menu_diag_note(MD_MICRO,1,allowed,(int32_t)count,0);
        (void)enqueue((struct micro_event){.click = true, .motion.at_ms = now,
            .native_generation = guard.generation});
    }
    for (unsigned int i = 0; i < result.count; i++) {
        if (!enqueue((struct micro_event){.motion = result.frames[i]})) {
            break;
        }
    }
    drain(allowed, emit, ctx);
}
static int native_position(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event && (event->position == 0U || event->position == 4U)) {
        uint8_t bit = (uint8_t)(1U << event->position);
        unsigned int key = irq_lock();
        if (event->state) { native_guard.held |= bit; }
        else { native_guard.held &= (uint8_t)~bit; }
        native_guard.generation++;
        irq_unlock(key);
    }
    /* Observe only: never consume, filter, or synthesize a physical mouse edge. */
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(tp_microtap_native_position, native_position);
ZMK_SUBSCRIPTION(tp_microtap_native_position, zmk_position_state_changed);
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
