/* SPDX-License-Identifier: MIT
 * Refactor-only API/adapter contract. Reuse the unchanged runtime host fixture,
 * but make opaque deliberately nonzero so the basic-type API cannot depend on
 * ZMK's behavior result. This is not a device/transport acceptance test.
 */
#include "gamepad_runtime_env.h"
#undef ZMK_BEHAVIOR_OPAQUE
#define ZMK_BEHAVIOR_OPAQUE 37
#undef DT_HAS_COMPAT_STATUS_OKAY
#define DT_HAS_COMPAT_STATUS_OKAY(x) 1
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
};
#define BEHAVIOR_DT_INST_DEFINE(n, init, pm, data, config, level, priority, api) \
    _Static_assert((priority) == 40, "behavior registration priority"); \
    static const struct behavior_driver_api *const test_refactor_gamepad_api = (api)
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define main original_gamepad_runtime_main
#include "test_gamepad_runtime.c"
#undef main

static int input(bool primitive, uint32_t position, uint32_t action, bool down, int64_t timestamp) {
    if (primitive) { return bbp_gamepad_key_event(position, action, down, timestamp); }
    struct zmk_behavior_binding binding = {.param1 = action};
    struct zmk_behavior_binding_event event = {.position = position, .timestamp = timestamp};
    return down ? test_refactor_gamepad_api->binding_pressed(&binding, event) :
                  test_refactor_gamepad_api->binding_released(&binding, event);
}
static int consumed(bool primitive) { return primitive ? 0 : ZMK_BEHAVIOR_OPAQUE; }
static void assert_local_neutral(void) {
    struct bbp_gamepad_report_body body = bbp_gamepad_snapshot(&gamepad);
    assert(bbp_gamepad_is_neutral(&body));
}
static void invalid_and_inactive(bool primitive, bool ble) {
    fixture(ble);
    uint32_t epoch = bbp_gamepad_context_epoch();
    assert(input(primitive, BBP_GAMEPAD_POSITIONS, BBP_GP_A, true, now_ms) == -EINVAL);
    assert(input(primitive, UINT32_MAX, BBP_GP_A, false, now_ms) == -EINVAL);
    assert(input(primitive, 41, BBP_GAMEPAD_ACTION_COUNT, true, now_ms) == -EINVAL);
    assert(input(primitive, 41, UINT32_MAX, true, now_ms) == -EINVAL);
    assert(input(primitive, 41, UINT32_MAX, false, now_ms) == consumed(primitive));
    assert(!count && !accepted_count && epoch == bbp_gamepad_context_epoch());
    assert_local_neutral();
    highest_layer = 0;
    /* Input validation still precedes layer filtering; timestamp validation
     * follows layer filtering and must not invalidate an inactive context. */
    assert(input(primitive, 42, BBP_GP_A, true, -1) == -EINVAL);
    assert(input(primitive, 41, UINT32_MAX, true, -1) == -EINVAL);
    assert(input(primitive, 41, BBP_GP_A, true, -1) == consumed(primitive));
    assert(input(primitive, 41, UINT32_MAX, false, now_ms + 1) == consumed(primitive));
    assert(!count && !accepted_count && epoch == bbp_gamepad_context_epoch());
    assert_local_neutral();
    no_leaks();
}
static void timestamps_and_unavailable(bool primitive, bool ble) {
    for (unsigned i = 0; i < 3; ++i) {
        fixture(ble);
        now_ms = 1000;
        int64_t timestamp = i == 0 ? -1 : i == 1 ? now_ms + 1 : now_ms - GP_FRAME_MAX_AGE_MS - 1;
        uint32_t epoch = bbp_gamepad_context_epoch();
        assert(input(primitive, 41, BBP_GP_A, true, timestamp) == consumed(primitive));
        assert(bbp_gamepad_context_epoch() == epoch + 1 && !count && !accepted_count);
        assert_local_neutral();
        no_leaks();
    }
    fixture(ble);
    now_ms = 1000;
    uint32_t epoch = bbp_gamepad_context_epoch();
    assert(input(primitive, 41, BBP_GP_A, true, now_ms - GP_FRAME_MAX_AGE_MS) == consumed(primitive));
    assert(epoch == bbp_gamepad_context_epoch() && count == 1 && ring[head].born == now_ms);
    run(1, 0);
    assert(accepted_count == 2 && accepted[1].body.buttons_lo == 1);
    no_leaks();
    fixture(ble);
    now_ms = GP_FRAME_MAX_AGE_MS;
    assert(input(primitive, 41, BBP_GP_A, true, 0) == consumed(primitive));
    assert(count == 1); /* Zero timestamp at the exact age boundary is valid. */
    no_leaks();
    fixture(ble);
    epoch = bbp_gamepad_context_epoch();
    if (ble) { peer_a.connected = false; }
    else { usb_ready = false; }
    assert(input(primitive, 41, BBP_GP_A, true, now_ms) == consumed(primitive));
    assert(bbp_gamepad_context_epoch() == epoch + 1 && !count && !accepted_count);
    assert_local_neutral();
    no_leaks();
}
static void captured_owner_and_stale_release(bool primitive, bool ble) {
    fixture(ble);
    assert(input(primitive, 41, BBP_GP_A, true, now_ms) == consumed(primitive));
    assert(count == 1 && gamepad.owners[41] == BBP_GP_A + 1U);
    assert(input(primitive, 41, BBP_GP_B, true, now_ms) == consumed(primitive));
    assert(input(primitive, 41, UINT32_MAX, true, now_ms) == -EINVAL);
    assert(count == 1 && gamepad.owners[41] == BBP_GP_A + 1U);
    assert(input(primitive, 41, UINT32_MAX, false, now_ms) == consumed(primitive));
    assert(count == 2 && !gamepad.owners[41]);
    assert(input(primitive, 41, BBP_GP_B, false, now_ms) == consumed(primitive));
    assert(count == 2);
    run(1, 0);
    assert(accepted_count == 3 && bbp_gamepad_is_neutral(&accepted[0].body) &&
           accepted[1].body.buttons_lo == 1 && bbp_gamepad_is_neutral(&accepted[2].body));
    no_leaks();
    fixture(ble);
    assert(input(primitive, 41, BBP_GP_A, true, now_ms) == consumed(primitive));
    run(1, 0);
    uint32_t epoch = bbp_gamepad_context_epoch();
    assert(input(primitive, 41, UINT32_MAX, false, now_ms + 1) == consumed(primitive));
    assert(bbp_gamepad_context_epoch() == epoch + 1);
    assert_local_neutral();
    run(8, 0);
    assert(!last_valid && !pending_valid && !count);
    assert(bbp_gamepad_is_neutral(&accepted[accepted_count - 1].body));
    no_leaks();
}
static void unnotified_destination_change(bool primitive, bool ble) {
    fixture(ble);
    assert(input(primitive, 41, BBP_GP_A, true, now_ms) == consumed(primitive));
    uint32_t epoch = bbp_gamepad_context_epoch();
    if (ble) { active_peer = &peer_b; }
    else { usb_epoch++; }
    assert(input(primitive, 40, BBP_GP_B, true, now_ms) == consumed(primitive));
    assert(bbp_gamepad_context_epoch() == epoch + 1 && count == 1);
    assert_local_neutral();
    assert(peer_b.refs == 1);
    run(8, 0);
    assert(accepted_count == 1 && bbp_gamepad_is_neutral(&accepted[0].body));
    if (ble) { assert(accepted[0].peer == peer_b.id); }
    assert(!last_valid && !pending_valid && !count);
    no_leaks();
}
struct refactor_trace {
    struct accepted_frame frames[32];
    unsigned frames_count, wake_count;
    uint32_t epoch, pointer_epoch;
};
static struct refactor_trace capture_trace(bool primitive, bool ble) {
    fixture(ble);
    assert(input(primitive, 34, BBP_GP_A, true, now_ms) == consumed(primitive));
    motion(8, -4);
    assert(input(primitive, 3, BBP_GP_RIGHT_STICK_MOD, true, now_ms) == consumed(primitive));
    assert(input(primitive, 4, BBP_GP_STICK_CLICK, true, now_ms) == consumed(primitive));
    motion(6, 5);
    assert(input(primitive, 3, UINT32_MAX, false, now_ms) == consumed(primitive));
    motion(-3, -2);
    assert(input(primitive, 34, UINT32_MAX, false, now_ms) == consumed(primitive));
    assert(input(primitive, 4, UINT32_MAX, false, now_ms) == consumed(primitive));
    run(4, 0);
    highest_layer = 0;
    context_changed(NULL);
    run(8, 10);
    assert(accepted_count <= 32 && !last_valid && !pending_valid && !count);
    struct refactor_trace trace = {.frames_count = accepted_count, .wake_count = schedules,
        .epoch = bbp_gamepad_context_epoch(), .pointer_epoch = bbp_gamepad_pointer_epoch()};
    memcpy(trace.frames, accepted, accepted_count * sizeof(accepted[0]));
    no_leaks();
    return trace;
}
static void compare_traces(bool ble) {
    struct refactor_trace runtime = capture_trace(true, ble), adapter = capture_trace(false, ble);
    assert(runtime.frames_count == adapter.frames_count && runtime.wake_count == adapter.wake_count &&
           runtime.epoch == adapter.epoch && runtime.pointer_epoch == adapter.pointer_epoch);
    for (unsigned i = 0; i < runtime.frames_count; ++i) {
        assert(runtime.frames[i].transport == adapter.frames[i].transport &&
               runtime.frames[i].peer == adapter.frames[i].peer &&
               runtime.frames[i].context == adapter.frames[i].context &&
               !memcmp(&runtime.frames[i].body, &adapter.frames[i].body, sizeof(runtime.frames[i].body)));
    }
}
int main(void) {
    assert(ZMK_BEHAVIOR_OPAQUE != 0);
    assert(test_refactor_gamepad_api == &gamepad_api &&
           test_refactor_gamepad_api->binding_pressed == pressed &&
           test_refactor_gamepad_api->binding_released == released);
    for (unsigned transport = 0; transport < 2; ++transport) {
        bool ble = transport != 0;
        for (unsigned path = 0; path < 2; ++path) {
            bool primitive = path != 0;
            invalid_and_inactive(primitive, ble);
            timestamps_and_unavailable(primitive, ble);
            captured_owner_and_stale_release(primitive, ble);
            unnotified_destination_change(primitive, ble);
        }
        compare_traces(ble);
    }
    puts("PASS gamepad refactor basic-type API + registered ZMK adapter: nonzero opaque mapping, "
         "error/order/age boundaries, captured UP, stale/unavailable/mismatched destination cancellation, "
         "identical USB/BLE FIFO/motion/context/wake traces and balanced refs; host mocks only");
    return 0;
}
