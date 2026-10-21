/* SPDX-License-Identifier: MIT
 * Actual AA layer owner + original-byte ZMK v0.3.0 sticky/mo/to regressions.
 *
 * This is NOT a full keymap/hold-tap integration or device test. The helper
 * sticky_tap() explicitly substitutes hold-tap's tap dispatch. Kernel time/work,
 * DT/device lookup, layer bits and the subscription dispatcher are host mocks.
 * In particular, constructor registration is not firmware ELF subscriber order.
 * Real SDK sticky code decides quick-release, forwards keycode once BEFORE the
 * child UP, owns the timeout, and removes/replaces its own sticky instance.
 *
 * Compile from the repository root (metadata intentionally disabled here):
 * cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
 *    -Wno-unused-function -Wno-unused-variable -fsanitize=undefined \
 *    -fno-sanitize-recover=all -I tests/aa_layer_mocks \
 *    tests/test_aa_layer_owner.c -o /tmp/test_aa_layer_owner
 */
#include "aa_layer_mocks/aa_layer_test_env.h"

#include "aa_layer_mocks/sdk_v0_3_0_momentary_layer.c"
#undef DT_DRV_COMPAT
#include "aa_layer_mocks/sdk_v0_3_0_to_layer.c"
#undef DT_DRV_COMPAT
#include "sdk_v0_3_0_sticky_key.c"
#undef DT_DRV_COMPAT
#include "../config/boards/arm/bbp9981/custom_driver/behavior_aa_layer.c"
#undef DT_DRV_COMPAT

/* Same SDK sticky configuration as sl/aa_sl; only their child differs. */
static const struct behavior_sticky_key_config old_sl_config = {
    .release_after_ms = 3000U,
    .quick_release = true,
    .lazy = false,
    .ignore_modifiers = false,
    .behavior = {.behavior_dev = "mo"},
};
static const struct behavior_sticky_key_config aa_sl_config = {
    .release_after_ms = 3000U,
    .quick_release = true,
    .lazy = false,
    .ignore_modifiers = false,
    .behavior = {.behavior_dev = "aa_layer"},
};
static const struct device old_sl_device = {.name = "old_sl", .config = &old_sl_config};
static const struct device aa_sl_device = {.name = "aa_sl", .config = &aa_sl_config};
static const struct device aa_layer_device = {.name = "aa_layer"};
static const struct device mo_device = {.name = "mo"};
static const struct device to_device = {.name = "to"};
static const struct zmk_behavior_binding old_sl_binding = {
    .behavior_dev = "old_sl", .param1 = 2U,
};
static const struct zmk_behavior_binding aa_sl_binding = {
    .behavior_dev = "aa_sl", .param1 = 1U,
};
static unsigned child_press_calls;
static unsigned child_release_calls;
static unsigned sticky_child_release_calls;

const struct device *zmk_behavior_get_binding(const char *name) {
    if (name == NULL) { return NULL; }
    if (strcmp(name, "old_sl") == 0) { return &old_sl_device; }
    if (strcmp(name, "aa_sl") == 0) { return &aa_sl_device; }
    if (strcmp(name, "aa_layer") == 0) { return &aa_layer_device; }
    if (strcmp(name, "mo") == 0) { return &mo_device; }
    if (strcmp(name, "to") == 0) { return &to_device; }
    return NULL;
}

int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *source,
                                struct zmk_behavior_binding_event event, bool down) {
    /* Device lookup/invocation is mocked, but every dispatched handler is real. */
    const struct behavior_driver_api *api = NULL;
    if (strcmp(source->behavior_dev, "old_sl") == 0 ||
        strcmp(source->behavior_dev, "aa_sl") == 0) {
        api = &behavior_sticky_key_driver_api;
    } else if (strcmp(source->behavior_dev, "mo") == 0) {
        api = &behavior_mo_driver_api;
    } else if (strcmp(source->behavior_dev, "to") == 0) {
        api = &behavior_to_driver_api;
    } else if (strcmp(source->behavior_dev, "aa_layer") == 0) {
        api = &aa_layer_api;
    }
    assert(api != NULL);
    if (strcmp(source->behavior_dev, "mo") == 0 ||
        strcmp(source->behavior_dev, "aa_layer") == 0) {
        if (down) { child_press_calls++; }
        else {
            child_release_calls++;
            if (strcmp(source->behavior_dev, "aa_layer") == 0 && source->param1 == 1U) {
                sticky_child_release_calls++;
            }
        }
    }
    struct zmk_behavior_binding copy = *source;
    return down ? api->binding_pressed(&copy, event) : api->binding_released(&copy, event);
}

static struct zmk_behavior_binding_event at_position(uint32_t position, int64_t timestamp) {
    return (struct zmk_behavior_binding_event){
        .position = position, .layer = 2, .timestamp = timestamp,
    };
}
static bool upper(void) { return zmk_keymap_layer_active(2U); }
static unsigned live_sticky_count(void) {
    unsigned count = 0U;
    for (size_t i = 0; i < ARRAY_SIZE(active_sticky_keys); ++i) {
        if (active_sticky_keys[i].position != ZMK_BHV_STICKY_KEY_POSITION_FREE) { count++; }
    }
    return count;
}
static unsigned pending_timer_count(void) {
    unsigned count = 0U;
    for (size_t i = 0; i < ARRAY_SIZE(active_sticky_keys); ++i) {
        if (active_sticky_keys[i].release_timer.pending) { count++; }
    }
    return count;
}
static void reset_fixture(void) {
    /* Test isolation, not a substitute for SDK press/release/lifecycle handlers. */
    assert(mock_dispatch_depth == 0U);
    memset(active_sticky_keys, 0, sizeof(active_sticky_keys));
    for (size_t i = 0; i < ARRAY_SIZE(active_sticky_keys); ++i) {
        active_sticky_keys[i].position = ZMK_BHV_STICKY_KEY_POSITION_FREE;
        k_work_init_delayable(&active_sticky_keys[i].release_timer,
                              behavior_sticky_key_timer_handler);
    }
    aa_momentary_owned = false;
    aa_sticky_owned = false;
    aa_fixed_owned = false;
    aa_internal_transition = 0U;
    mock_now = 0;
    mock_layer_bits = 0U;
    mock_layer_activate_calls = mock_layer_deactivate_calls = mock_layer_to_calls = 0U;
    mock_layer_events = mock_upper_false_events = mock_upper_true_events = 0U;
    mock_keycode_sink_calls = mock_keycode_down_calls = mock_keycode_up_calls = 0U;
    mock_keycode_reraises = 0U;
    mock_keycode_sink_layers = 0U;
    mock_activate_error = mock_to_error = 0;
    child_press_calls = child_release_calls = sticky_child_release_calls = 0U;
}
static void advance_to(int64_t target) {
    assert(target >= mock_now);
    for (;;) {
        struct k_work_delayable *next = NULL;
        for (size_t i = 0; i < ARRAY_SIZE(active_sticky_keys); ++i) {
            struct k_work_delayable *work = &active_sticky_keys[i].release_timer;
            if (work->pending && work->due <= target &&
                (next == NULL || work->due < next->due)) {
                next = work;
            }
        }
        if (next == NULL) { break; }
        assert(next->due >= mock_now);
        mock_now = next->due;
        next->pending = false;
        next->running = true;
        next->work.handler(&next->work);
        next->running = false;
    }
    mock_now = target;
}
static void invoke_sticky(const struct zmk_behavior_binding *binding, bool down,
                           int64_t original_timestamp) {
    assert(zmk_behavior_invoke_binding(binding, at_position(41U, original_timestamp), down) ==
           ZMK_BEHAVIOR_OPAQUE);
}
static void sticky_tap(const struct zmk_behavior_binding *binding, int64_t original_down,
                       int64_t dispatch_wall_time) {
    /* Real hold-tap tap behavior carries original DOWN time on both invocations.
     * Here that dispatch alone is mocked; no real P41 keymap event is claimed. */
    assert(dispatch_wall_time >= original_down);
    advance_to(dispatch_wall_time);
    invoke_sticky(binding, true, original_down);
    invoke_sticky(binding, false, original_down);
}
static void owner_edge(uint32_t mode, bool down, int64_t timestamp) {
    advance_to(timestamp);
    struct zmk_behavior_binding binding = {.behavior_dev = "aa_layer", .param1 = mode};
    struct zmk_behavior_binding_event event = at_position(41U, timestamp);
    assert((down ? aa_layer_api.binding_pressed(&binding, event) :
                   aa_layer_api.binding_released(&binding, event)) == ZMK_BEHAVIOR_OPAQUE);
}
static void keycode(bool down, uint32_t usage, int64_t timestamp) {
    advance_to(timestamp);
    struct zmk_keycode_state_changed_event event = {
        .header = {.type = MOCK_EVENT_zmk_keycode_state_changed},
        .data = {.usage_page = 7U, .keycode = usage, .state = down, .timestamp = timestamp},
    };
    assert(mock_dispatch_from(&event.header, 0U) == 0);
}
static void old_pin(int64_t timestamp) {
    advance_to(timestamp);
    struct zmk_behavior_binding binding = {.behavior_dev = "to", .param1 = 2U};
    assert(zmk_behavior_invoke_binding(&binding, at_position(41U, timestamp), true) == 0);
}

static void test_old_to_does_not_retire_sdk_sticky_owner(void) {
    reset_fixture();
    sticky_tap(&old_sl_binding, 0, 50);
    assert(upper() && live_sticky_count() == 1U && pending_timer_count() == 1U);
    old_pin(100);
    assert(upper() && live_sticky_count() == 1U && pending_timer_count() == 1U);
    assert(mock_upper_false_events == 1U && mock_upper_true_events == 2U);
    advance_to(2999);
    assert(upper());
    advance_to(3000);
    assert(!upper() && live_sticky_count() == 0U && child_release_calls == 1U);
    puts("PASS original SDK control: to(UPPER) does not retire SL2; real sticky timer later releases mo2");
}
static void test_old_quick_release_also_defeats_to_pin(void) {
    reset_fixture();
    sticky_tap(&old_sl_binding, 0, 50);
    old_pin(100);
    keycode(true, 4U, 200);
    assert(mock_keycode_sink_calls == 1U && mock_keycode_reraises == 1U);
    assert((mock_keycode_sink_layers & BIT(2)) != 0U); /* forwarded BEFORE child UP */
    assert(!upper() && live_sticky_count() == 0U);
}
static void test_new_pin_survives_real_sticky_timeout(void) {
    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    assert(upper() && aa_sticky_owned && !aa_fixed_owned);
    owner_edge(2U, true, 100);
    assert(upper() && aa_fixed_owned && !aa_sticky_owned);
    assert(mock_upper_false_events == 1U && mock_upper_true_events == 2U);
    assert(aa_internal_transition == 0U); /* synchronous false -> true did not clear fixed */
    unsigned deactivations = mock_layer_deactivate_calls;
    advance_to(3000);
    assert(live_sticky_count() == 0U && sticky_child_release_calls == 1U);
    assert(upper() && aa_fixed_owned && !aa_sticky_owned);
    assert(mock_layer_deactivate_calls == deactivations);
    owner_edge(2U, false, 3100);
    owner_edge(2U, false, 3200);
    assert(upper() && aa_fixed_owned && mock_layer_deactivate_calls == deactivations);
}
static void test_new_pin_survives_real_sdk_keycode_quick_release(void) {
    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    owner_edge(2U, true, 100);
    unsigned deactivations = mock_layer_deactivate_calls;
    keycode(true, 4U, 200);
    assert(mock_keycode_sink_calls == 1U && mock_keycode_reraises == 1U);
    assert((mock_keycode_sink_layers & BIT(2)) != 0U);
    assert(live_sticky_count() == 0U && pending_timer_count() == 0U);
    assert(upper() && aa_fixed_owned && !aa_sticky_owned);
    assert(mock_layer_deactivate_calls == deactivations);
    keycode(false, 4U, 230);
    assert(mock_keycode_sink_calls == 2U && mock_keycode_down_calls == 1U &&
           mock_keycode_up_calls == 1U);
    advance_to(4000);
    assert(upper() && aa_fixed_owned && mock_layer_deactivate_calls == deactivations);
}
static void test_momentary_owner_outlives_old_sticky_child_up(void) {
    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    owner_edge(0U, true, 1000);
    assert(upper() && aa_momentary_owned && aa_sticky_owned);
    advance_to(3000);
    assert(upper() && aa_momentary_owned && !aa_sticky_owned && !aa_fixed_owned);
    assert(sticky_child_release_calls == 1U);
    owner_edge(0U, false, 3100);
    assert(!upper() && !aa_momentary_owned);
    unsigned deactivations = mock_layer_deactivate_calls;
    owner_edge(0U, false, 3200);
    assert(mock_layer_deactivate_calls == deactivations);
}
static void test_external_exit_and_stale_sdk_release_are_non_destructive(void) {
    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    assert(zmk_keymap_layer_to(0U) == 0);
    assert(!upper() && !aa_momentary_owned && !aa_sticky_owned && !aa_fixed_owned);
    /* An externally reactivated layer has no AA owner. The old SDK release
     * must not deactivate somebody else's new layer merely because all=false. */
    assert(zmk_keymap_layer_to(2U) == 0);
    unsigned deactivations = mock_layer_deactivate_calls;
    advance_to(3000);
    assert(live_sticky_count() == 0U && sticky_child_release_calls == 1U);
    assert(upper() && mock_layer_deactivate_calls == deactivations);

    reset_fixture();
    owner_edge(0U, true, 0);
    owner_edge(1U, true, 10);
    owner_edge(2U, true, 20);
    assert(zmk_keymap_layer_to(1U) == 0);
    assert(!aa_momentary_owned && !aa_sticky_owned && !aa_fixed_owned && !upper());
    deactivations = mock_layer_deactivate_calls;
    owner_edge(0U, false, 30);
    owner_edge(1U, false, 40);
    owner_edge(2U, false, 50);
    assert(zmk_keymap_highest_layer_active() == 1U &&
           mock_layer_deactivate_calls == deactivations);
}
static void test_unrelated_sym_layer_event_does_not_clear_aa_owners(void) {
    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    assert(zmk_keymap_layer_activate(1U) == 0);
    assert(zmk_keymap_layer_deactivate(1U) == 0);
    assert(upper() && aa_sticky_owned);
    owner_edge(2U, true, 100);
    assert(zmk_keymap_layer_activate(1U) == 0);
    assert(zmk_keymap_layer_deactivate(1U) == 0);
    assert(upper() && aa_fixed_owned && aa_internal_transition == 0U);
}
static void test_ordinary_sticky_three_seconds_and_quick_release_match_sdk(void) {
    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    assert(aa_sticky_owned && upper() && pending_timer_count() == 1U);
    advance_to(2999);
    assert(aa_sticky_owned && upper());
    advance_to(3000);
    assert(!aa_sticky_owned && !upper() && live_sticky_count() == 0U);

    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    keycode(true, 4U, 100);
    assert(!upper() && !aa_sticky_owned && live_sticky_count() == 0U);
    assert(mock_keycode_sink_calls == 1U && mock_keycode_reraises == 1U &&
           (mock_keycode_sink_layers & BIT(2)) != 0U);
    keycode(false, 4U, 130);
    assert(mock_keycode_sink_calls == 2U);

    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    keycode(true, 0xE0U, 100); /* sl has ignore_modifiers=false: Ctrl also consumes it. */
    assert(!upper() && !aa_sticky_owned && mock_keycode_sink_calls == 1U);
}
static void test_timer_starts_on_binding_up_and_held_keycode_is_not_duplicated(void) {
    reset_fixture();
    invoke_sticky(&aa_sl_binding, true, 0);
    advance_to(500);
    assert(upper() && pending_timer_count() == 0U);
    invoke_sticky(&aa_sl_binding, false, 500);
    advance_to(3499);
    assert(upper());
    advance_to(3500);
    assert(!upper() && !aa_sticky_owned);

    reset_fixture();
    invoke_sticky(&aa_sl_binding, true, 0);
    keycode(true, 4U, 100); /* While sticky binding itself is still held. */
    assert(upper() && aa_sticky_owned && mock_keycode_reraises == 0U);
    assert(mock_keycode_sink_calls == 1U && pending_timer_count() == 0U);
    advance_to(150);
    invoke_sticky(&aa_sl_binding, false, 150);
    assert(!upper() && !aa_sticky_owned && pending_timer_count() == 0U);
    keycode(false, 4U, 180);
    assert(mock_keycode_sink_calls == 2U && mock_keycode_reraises == 0U);
}
static void test_sdk_repress_replaces_same_sticky_instance(void) {
    reset_fixture();
    sticky_tap(&aa_sl_binding, 0, 50);
    assert(live_sticky_count() == 1U);
    sticky_tap(&aa_sl_binding, 1000, 1050);
    assert(live_sticky_count() == 1U && pending_timer_count() == 1U);
    assert(sticky_child_release_calls == 1U && aa_sticky_owned && upper());
    advance_to(3000); /* The first tap's former deadline must not release the second. */
    assert(upper() && aa_sticky_owned);
    advance_to(3999);
    assert(upper());
    advance_to(4000);
    assert(!upper() && live_sticky_count() == 0U && sticky_child_release_calls == 2U);
}
static void test_release_idempotence_and_position_validation(void) {
    for (uint32_t mode = 0U; mode <= 1U; ++mode) {
        reset_fixture();
        owner_edge(mode, true, 0);
        assert(upper());
        owner_edge(mode, false, 10);
        assert(!upper());
        unsigned deactivations = mock_layer_deactivate_calls;
        owner_edge(mode, false, 20);
        assert(mock_layer_deactivate_calls == deactivations);
    }
    reset_fixture();
    owner_edge(2U, true, 0);
    unsigned deactivations = mock_layer_deactivate_calls;
    owner_edge(2U, false, 10);
    owner_edge(2U, false, 20);
    assert(aa_fixed_owned && upper() && mock_layer_deactivate_calls == deactivations);

    reset_fixture();
    assert(zmk_keymap_layer_to(2U) == 0);
    deactivations = mock_layer_deactivate_calls;
    unsigned activations = mock_layer_activate_calls;
    for (uint32_t mode = 0U; mode < 3U; ++mode) {
        struct zmk_behavior_binding binding = {.behavior_dev = "aa_layer", .param1 = mode};
        struct zmk_behavior_binding_event wrong = at_position(3U, 0);
        assert(aa_layer_api.binding_pressed(&binding, wrong) == ZMK_BEHAVIOR_OPAQUE);
        assert(aa_layer_api.binding_released(&binding, wrong) == ZMK_BEHAVIOR_OPAQUE);
        owner_edge(mode, false, 0); /* P41 release without any current owner. */
    }
    assert(upper() && !aa_momentary_owned && !aa_sticky_owned && !aa_fixed_owned);
    assert(mock_layer_activate_calls == activations &&
           mock_layer_deactivate_calls == deactivations);
}
static void test_failed_activation_does_not_leave_a_phantom_owner(void) {
    for (uint32_t mode = 0U; mode < 2U; ++mode) {
        reset_fixture();
        mock_activate_error = -EIO;
        struct zmk_behavior_binding binding = {.behavior_dev = "aa_layer", .param1 = mode};
        (void)aa_layer_api.binding_pressed(&binding, at_position(41U, 0));
        assert(!aa_momentary_owned && !aa_sticky_owned && !aa_fixed_owned && !upper());
        unsigned deactivations = mock_layer_deactivate_calls;
        owner_edge(mode, false, 0);
        assert(mock_layer_deactivate_calls == deactivations);
    }
}

int main(void) {
    assert(behavior_sticky_key_init(NULL) == 0);
    bool have_keycode = false, have_layer = false;
    for (size_t i = 0; i < mock_subscription_count; ++i) {
        if (mock_subscriptions[i].type == MOCK_EVENT_zmk_keycode_state_changed) { have_keycode = true; }
        if (mock_subscriptions[i].type == MOCK_EVENT_zmk_layer_state_changed) { have_layer = true; }
    }
    assert(have_keycode && have_layer); /* Listener macros are not erased. */
    test_old_to_does_not_retire_sdk_sticky_owner();
    test_old_quick_release_also_defeats_to_pin();
    test_new_pin_survives_real_sticky_timeout();
    test_new_pin_survives_real_sdk_keycode_quick_release();
    test_momentary_owner_outlives_old_sticky_child_up();
    test_external_exit_and_stale_sdk_release_are_non_destructive();
    test_unrelated_sym_layer_event_does_not_clear_aa_owners();
    test_ordinary_sticky_three_seconds_and_quick_release_match_sdk();
    test_timer_starts_on_binding_up_and_held_keycode_is_not_duplicated();
    test_sdk_repress_replaces_same_sticky_instance();
    test_release_idempotence_and_position_validation();
    test_failed_activation_does_not_leave_a_phantom_owner();
    puts("PASS actual AA owner + pinned SDK sticky/mo/to: original to/SL2 failure, fixed and held owner protection, synchronous layer transitions, external exit, stale/duplicate UP, 3s/quick-release/held-keycode/repress; host mocks only, no full keymap/hold-tap/device claim");
    return 0;
}
