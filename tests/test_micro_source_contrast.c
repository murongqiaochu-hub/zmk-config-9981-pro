/* SPDX-License-Identifier: MIT
 * One public-input driver, compiled against two ORIGINAL source variants by
 * test_micro_source_contrast.py. Do not include test_micro_runtime.c here:
 * no native_guard/gesture/FIFO fields are manufactured or reset by this test.
 *
 * Real: runtime, gesture core, shoulder core and shoulder behavior adapter.
 * Mock: SDK/event fan-out, stock native input enqueue, tap-output bridge/clock.
 * "accepted" below is simulated enqueue acceptance, NOT HOG/USB or host ACK.
 */
#include "test_env.h"
#define CONFIG_ZMK_USB 0
#define CONFIG_ZMK_BLE 0
#define DEVICE_DT_NAME(node) "mkp"
#define LCLK 1U

#ifndef MICRO_RUNTIME_C
#error "Compile through test_micro_source_contrast.py with absolute source paths"
#endif
#ifndef MICRO_SHOULDER_C
#error "Missing actual shoulder behavior source"
#endif
#ifndef MICRO_RUNTIME_H
#error "Missing matching runtime header"
#endif
#ifndef HAS_NATIVE_GUARD
#error "Declare whether this actual runtime has the native position listener"
#endif

struct k_mutex { bool locked; };
#define K_MUTEX_DEFINE(name) static struct k_mutex name
static int k_mutex_lock(struct k_mutex *m, int timeout) {
    (void)timeout;
    assert(!m->locked);
    m->locked = true;
    return 0;
}
static int k_mutex_unlock(struct k_mutex *m) {
    assert(m->locked);
    m->locked = false;
    return 0;
}
static int32_t atomic_inc(atomic_t *p) {
    int32_t old = *p;
    (*p)++;
    return old;
}
static bool zmk_endpoint_instance_eq(struct zmk_endpoint_instance a,
                                    struct zmk_endpoint_instance b) {
    return a.transport == b.transport;
}
static unsigned int irq_depth;
static unsigned int irq_lock(void) {
    assert(irq_depth == 0U);
    return irq_depth++;
}
static void irq_unlock(unsigned int key) {
    assert(key == 0U && irq_depth == 1U);
    irq_depth = 0U;
}

#include MICRO_RUNTIME_H
#include "trackpad_tap_output.h"
static struct tp_tap_output_inflight bridge;
static struct tp_tap_output_context output_context;
static int refs[5];
static bool native_down;
static unsigned int native_down_accepted, native_up_accepted;
static unsigned int auto_submit_accepted, auto_down_accepted, auto_up_accepted;
static unsigned int motion_frames;
static int32_t native_down_wall = -1, native_up_wall = -1;
static int32_t native_down_event = -1, native_up_event = -1;
static int32_t auto_down_wall = -1, auto_up_wall = -1;
static int32_t motion_at = -1;
static bool raw_enabled, raw_physical_down, raw_known;
static int raw_refs;

/* Both center and actual shoulder replay use this SAME stock-input mock. */
static int stock_native_input(bool down, uint32_t event_at) {
    assert(irq_depth == 0U && native_down != down);
    native_down = down;
    if (down) {
        refs[0]++;
        native_down_accepted++;
        native_down_wall = (int32_t)fake_now;
        native_down_event = (int32_t)event_at;
    } else {
        assert(refs[0] > 0);
        refs[0]--;
        native_up_accepted++;
        native_up_wall = (int32_t)fake_now;
        native_up_event = (int32_t)event_at;
    }
    return 0; /* Simulated stock input enqueue, never transport delivery. */
}
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event, bool down) {
    assert(strcmp(binding->behavior_dev, "mkp") == 0 && binding->param1 == LCLK);
    assert(event.position == 0U);
    return stock_native_input(down, (uint32_t)event.timestamp);
}
int bbp9981_hid_mouse_button_references(unsigned int button) {
    assert(irq_depth == 0U && button < 5U);
    return refs[button];
}
int tp_tap_output_init(void) {
    assert(irq_depth == 0U);
    bridge.initialized = true;
    return 0;
}
struct tp_tap_output_inflight tp_tap_output_read_inflight(void) {
    assert(irq_depth == 0U);
    bridge.busy = bridge.phase != TP_TAP_OUTPUT_IDLE || bridge.owns_left;
    return bridge;
}
int tp_tap_output_submit(void) {
    assert(irq_depth == 0U);
    if (bridge.phase != TP_TAP_OUTPUT_IDLE) { return -EBUSY; }
    assert(output_context.enabled && output_context.physical_left_known &&
           !output_context.physical_left_down && refs[0] == 0);
    auto_submit_accepted++;
    auto_down_accepted++;
    auto_down_wall = (int32_t)fake_now;
    bridge.phase = TP_TAP_OUTPUT_HOLDING;
    bridge.owns_left = true;
    bridge.down_accepted = true;
    bridge.accepted_at = fake_now;
    bridge.down_reports++;
    refs[0]++;
    return 0; /* Immediate mock bridge/backend acceptance, not host ACK. */
}
static void bridge_release(void) {
    assert(irq_depth == 0U && bridge.owns_left && refs[0] > 0);
    refs[0]--;
    bridge.owns_left = false;
    bridge.phase = TP_TAP_OUTPUT_IDLE;
    bridge.busy = false;
    bridge.up_reports++;
    auto_up_accepted++;
    auto_up_wall = (int32_t)fake_now;
}
int tp_tap_output_cancel(void) {
    assert(irq_depth == 0U);
    if (bridge.owns_left) { bridge_release(); }
    bridge.phase = TP_TAP_OUTPUT_IDLE;
    return 0;
}
int tp_tap_output_update_context(struct tp_tap_output_context context) {
    assert(irq_depth == 0U);
    output_context = context;
    if (!context.enabled || !context.physical_left_known || context.physical_left_down) {
        return tp_tap_output_cancel();
    }
    return 0;
}
/* Model a 12ms bridge hold independent of the next poll's sampling time. */
static void advance_to(uint32_t at) {
    assert(at >= fake_now);
    if (bridge.owns_left && at >= bridge.accepted_at + TP_TAP_OUTPUT_HOLD_MS) {
        fake_now = bridge.accepted_at + TP_TAP_OUTPUT_HOLD_MS;
        bridge_release();
    }
    fake_now = at;
}

/* The two actual C files each have a private static "initialized". Preserve
 * that original TU separation by namespacing only that identifier on inclusion.
 * All public headers with initialized members were included before the macro.
 */
#define initialized contrast_runtime_initialized
#include MICRO_RUNTIME_C
#undef initialized
#define initialized contrast_shoulder_initialized
#include MICRO_SHOULDER_C
#undef initialized

static void emit_motion(void *ctx, int16_t dx, int16_t dy, uint32_t at) {
    (void)ctx;
    assert(irq_depth == 0U && !bridge.owns_left);
    assert(dx == 8 && dy == 0);
    motion_frames++;
    motion_at = (int32_t)at;
}
static void poll_at(uint32_t at, int16_t dx) {
    advance_to(at);
    if (dx != 0) {
        /* Same actual adapter flush ordering as a320_0x57.c: native D/U is
         * replayed before the SAME poll's raw sample enters the runtime. */
        tp_microtap_shoulder_motion(at);
        raw_refs = refs[0];
    }
    tp_microtap_runtime_poll(at, dx, 0, true, false, 0, 0, emit_motion, NULL);
    if (dx != 0) {
        raw_enabled = output_context.enabled;
        raw_physical_down = output_context.physical_left_down;
        raw_known = output_context.physical_left_known;
    }
}
static void tap_at(uint32_t at) { poll_at(at, 8); poll_at(at, 8); poll_at(at, 8); }
static void zeros(uint32_t first, uint32_t last) {
    for (uint32_t at = first; at <= last; at += 10U) { poll_at(at, 0); }
}
static void physical_edge(uint32_t at, uint32_t position, bool down) {
    advance_to(at);
    zmk_event_t event = {.valid = true,
        .data = {.position = position, .state = down, .timestamp = at}};
#if HAS_NATIVE_GUARD
    /* ONLY variant-specific driving: this listener does not exist in BASE.
     * Never reproduce the new guard algorithm for the old runtime. */
    assert(native_position(&event) == ZMK_EV_EVENT_BUBBLE);
#endif
    /* Original eventManager shoulder other-key listener exists in BOTH. */
    assert(other_key(&event) == ZMK_EV_EVENT_BUBBLE);
    if (position == 0U) {
        struct zmk_behavior_binding binding = {0};
        struct zmk_behavior_binding_event binding_event = {
            .position = position, .timestamp = at, .layer = 0};
        assert((down ? pressed(&binding, binding_event) :
                       released(&binding, binding_event)) == ZMK_BEHAVIOR_OPAQUE);
    } else {
        assert(position == 4U);
        assert(stock_native_input(down, at) == 0);
    }
}
static void start(void) {
    /* Each case runs in a fresh process. No private runtime state resets. */
    tp_microtap_runtime_init();
    assert(!tp_microtap_runtime_enabled());
    tp_microtap_runtime_toggle();
    assert(tp_microtap_runtime_enabled());
    poll_at(0, 0);
}
static void scenario_a(void) {
    physical_edge(10, 4, true);
    assert(refs[0] == 1);
    poll_at(20, 0);
    physical_edge(30, 4, false);
    assert(refs[0] == 0);
    tap_at(40);
    zeros(50, 400);
}
static void scenario_b(void) {
    physical_edge(0, 0, true);
    zeros(10, 20); /* Consume the real pressed interrupt during busy time. */
    physical_edge(30, 0, false);
    assert(native_down_accepted == 0U && native_up_accepted == 0U);
    assert(tp_shoulder_busy(&shoulder));
    zeros(30, 40);
    tap_at(50); /* Adapter flushes D/U, then runtime consumes a 3-frame candidate. */
    assert(native_down_accepted == 1U && native_up_accepted == 1U);
    assert(refs[0] == 0 && !native_down && !tp_shoulder_busy(&shoulder));
    assert(!timeout_work.pending && !forwarded_down && !release_pending);
    assert(native_down_wall == 50 && native_up_wall == 50);
    assert(native_down_event == 0 && native_up_event == 30);
    zeros(60, 400); /* Well beyond raw50+NEW quiet120; not just waiting120. */
}
static void scenario_c(void) {
    tap_at(10);
    zeros(20, 190);
    /* REQUIRE auto D/U already finished BEFORE native at200/220. Do not hide
     * the residual double click by squeezing native into auto's held ref. */
    assert(auto_down_accepted == 1U && auto_up_accepted == 1U);
    assert(auto_up_wall <= 190 && !bridge.owns_left && refs[0] == 0);
    physical_edge(200, 4, true);
    assert(refs[0] == 1);
    poll_at(210, 0);
    physical_edge(220, 4, false);
    assert(refs[0] == 0);
    zeros(220, 400);
}
int main(int argc, char **argv) {
    assert(argc == 2 && strlen(argv[1]) == 1U);
    start();
    switch (argv[1][0]) {
    case 'A': scenario_a(); break;
    case 'B': scenario_b(); break;
    case 'C': scenario_c(); break;
    default: assert(!"Unknown scenario"); return 2;
    }
    assert(native_down_accepted == 1U && native_up_accepted == 1U);
    assert(!native_down && refs[0] == 0 && !bridge.owns_left);
    assert(auto_submit_accepted == auto_down_accepted &&
           auto_down_accepted == auto_up_accepted && auto_down_accepted <= 1U);
    assert(!tp_microtap_runtime_pending());
    printf("{\"case\":\"%c\",\"quiet_ms\":%u,\"gap_ms\":%u,"
           "\"nativeDownAccepted\":%u,\"nativeUpAccepted\":%u,"
           "\"autoSubmitAccepted\":%u,\"autoDownAccepted\":%u,\"autoUpAccepted\":%u,"
           "\"nativeDownAt\":%d,\"nativeUpAt\":%d,"
           "\"nativeEventDownAt\":%d,\"nativeEventUpAt\":%d,"
           "\"autoDownAt\":%d,\"autoUpAt\":%d,"
           "\"motionFrames\":%u,\"motionAt\":%d,\"rawEnabled\":%d,"
           "\"rawRefs\":%d,\"rawKnown\":%d,\"rawPhysicalDown\":%d}\n",
           argv[1][0], TP_MICROTAP_QUIET_MS, TP_MICROTAP_CLICK_INTERVAL_MS,
           native_down_accepted, native_up_accepted,
           auto_submit_accepted, auto_down_accepted, auto_up_accepted,
           native_down_wall, native_up_wall, native_down_event, native_up_event,
           auto_down_wall, auto_up_wall, motion_frames, motion_at,
           raw_enabled, raw_refs, raw_known, raw_physical_down);
    return 0;
}
