#include "test_env.h"
#include <dt-bindings/zmk/bt.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_scroll_lock.h"

#define DEVICE_DT_NAME(node) "native_bt"
#define BT_SEL 7U

static bool fake_latched, fake_locked, fake_aa_pressed, fake_mouse_reversed;
static uint8_t fake_rotation;
static unsigned orientation_selections, mouse_toggles, invoke_count;
struct invocation {
    uint32_t param1, param2, position;
    int64_t timestamp;
    bool pressed;
};
static struct invocation invokes[32];

bool tp_scroll_lock_is_latched(void) { return fake_latched; }
bool tp_scroll_is_locked(void) { return fake_locked; }
bool tp_scroll_aa_is_pressed(void) { return fake_aa_pressed; }
void tp_scroll_rotation_select(uint8_t orientation) {
    fake_rotation = (uint8_t)(orientation & TP_SCROLL_ROTATION_MASK);
    orientation_selections++;
}
bool tp_mouse_orientation_toggle(void) {
    fake_mouse_reversed = !fake_mouse_reversed;
    mouse_toggles++;
    return fake_mouse_reversed;
}

int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed) {
    assert(invoke_count < sizeof(invokes) / sizeof(invokes[0]));
    assert(strcmp(binding->behavior_dev, "native_bt") == 0);
    invokes[invoke_count++] = (struct invocation){
        .param1 = binding->param1, .param2 = binding->param2,
        .position = event.position, .timestamp = event.timestamp, .pressed = pressed,
    };
    return 0;
}

#include "../config/boards/arm/bbp9981/custom_driver/behavior_aa_scroll_gesture_key.c"

static const struct zmk_behavior_binding wrapper = {.behavior_dev = "aa_scroll_gesture_key"};
static void reset(void) {
    fake_latched = fake_locked = fake_aa_pressed = fake_mouse_reversed = false;
    fake_rotation = 0U; orientation_selections = mouse_toggles = invoke_count = 0U;
    memset(invokes, 0, sizeof(invokes));
    gesture = (struct gesture_state){.sequence_position = UINT32_MAX};
    bt_forwarded_mask = 0U;
}
static void edge(uint32_t position, bool down, int64_t timestamp) {
    struct zmk_behavior_binding_event event = {
        .layer = 2, .position = position, .timestamp = timestamp,
    };
    assert((down ? api.binding_pressed((struct zmk_behavior_binding *)&wrapper, event) :
                   api.binding_released((struct zmk_behavior_binding *)&wrapper, event)) ==
           ZMK_BEHAVIOR_OPAQUE);
}
static void tap(uint32_t position, int64_t at, int64_t duration) {
    edge(position, true, at);
    edge(position, false, at + duration);
}
static void triple(uint32_t position, int64_t at) {
    tap(position, at, 30);
    tap(position, at + 100, 30);
    tap(position, at + 200, 30);
}

static void test_unlocked_bt_selectors_remain_available(void) {
    reset();
    tap(TP_BLACKBERRY_POSITION, 10, 30);
    tap(TP_BACK_POSITION, 100, 30);
    assert(invoke_count == 4U);
    assert(invokes[0].param1 == BT_SEL && invokes[0].param2 == 1U && invokes[0].pressed);
    assert(invokes[1].param1 == BT_SEL && invokes[1].param2 == 1U && !invokes[1].pressed);
    assert(invokes[2].param1 == BT_SEL && invokes[2].param2 == 2U && invokes[2].pressed);
    assert(invokes[3].param1 == BT_SEL && invokes[3].param2 == 2U && !invokes[3].pressed);
}

static void test_locked_blackberry_triple_rotates_left_to_up(void) {
    reset(); fake_latched = fake_locked = true;
    tap(TP_BLACKBERRY_POSITION, 0, 30);
    tap(TP_BLACKBERRY_POSITION, 100, 30);
    assert(fake_rotation == 0U && invoke_count == 0U);
    tap(TP_BLACKBERRY_POSITION, 200, 30);
    assert(fake_rotation == TP_SCROLL_ROTATION_LEFT_TO_UP_FIXED && orientation_selections == 1U);
    assert(invoke_count == 0U);
    triple(TP_BLACKBERRY_POSITION, 500);
    assert(fake_rotation == 1U && orientation_selections == 2U && invoke_count == 0U);
}

static void test_locked_back_triple_rotates_right_to_up_only(void) {
    reset(); fake_latched = fake_locked = true;
    tap(TP_BACK_POSITION, 0, 30);
    tap(TP_BACK_POSITION, 100, 30);
    assert(fake_rotation == 0U && invoke_count == 0U);
    tap(TP_BACK_POSITION, 200, 30);
    assert(fake_rotation == TP_SCROLL_ROTATION_RIGHT_TO_UP_FIXED && orientation_selections == 1U);
    assert(invoke_count == 0U);
}

static void test_fixed_presets_ignore_previous_angle_and_repeats(void) {
    const uint32_t positions[2] = {TP_BLACKBERRY_POSITION, TP_BACK_POSITION};
    const uint8_t target[2] = {TP_SCROLL_ROTATION_LEFT_TO_UP_FIXED,
                               TP_SCROLL_ROTATION_RIGHT_TO_UP_FIXED};
    for (unsigned preset = 0; preset < 2U; ++preset) {
        for (unsigned previous = 0; previous < 4U; ++previous) {
            reset(); fake_latched = fake_locked = true; fake_rotation = (uint8_t)previous;
            for (unsigned repeat = 0; repeat < 3U; ++repeat) {
                triple(positions[preset], repeat * 400);
                assert(fake_rotation == target[preset] && invoke_count == 0U);
            }
            triple(positions[1U-preset], 1300);
            assert(fake_rotation == target[1U-preset] && invoke_count == 0U);
            triple(positions[preset], 1700);
            assert(fake_rotation == target[preset] && invoke_count == 0U);
        }
    }
}

static void test_aA_held_M_double_toggles_mouse_and_suppresses_bt(void) {
    reset(); fake_aa_pressed = true;
    tap(TP_MOUSE_TOGGLE_POSITION, 10, 30);
    assert(!fake_mouse_reversed && mouse_toggles == 0U && invoke_count == 0U);
    tap(TP_MOUSE_TOGGLE_POSITION, 100, 30);
    assert(fake_mouse_reversed && mouse_toggles == 1U && invoke_count == 0U);
    tap(TP_MOUSE_TOGGLE_POSITION, 300, 30);
    tap(TP_MOUSE_TOGGLE_POSITION, 400, 30);
    assert(!fake_mouse_reversed && mouse_toggles == 2U && invoke_count == 0U);
}

static void test_aA_held_always_restores_immediate_bt_slots(void) {
    for (unsigned latched = 0; latched < 2; ++latched) {
        reset(); fake_latched = latched != 0; fake_aa_pressed = true;
        edge(TP_BLACKBERRY_POSITION, true, 0);
        assert(invoke_count == 1U && invokes[0].param2 == 1U && invokes[0].pressed);
        edge(TP_BLACKBERRY_POSITION, false, 500); /* Long press also selects normally. */
        edge(TP_BACK_POSITION, true, 600);
        assert(invoke_count == 3U && invokes[2].param2 == 2U && invokes[2].pressed);
        fake_aa_pressed = false; /* Either release order must retain BT UP ownership. */
        tp_scroll_gesture_cancel();
        edge(TP_BACK_POSITION, false, 620);
        assert(invoke_count == 4U && !invokes[3].pressed && invokes[3].param2 == 2U);
        assert(orientation_selections == 0U && mouse_toggles == 0U);
    }
}

static void test_F_without_aA_and_context_cancellation(void) {
    reset();
    triple(TP_MOUSE_TOGGLE_POSITION, 10);
    assert(mouse_toggles == 0U && invoke_count == 0U);
    fake_latched = fake_locked = true;
    tap(TP_BLACKBERRY_POSITION, 400, 30);
    tap(TP_BLACKBERRY_POSITION, 500, 30);
    zmk_event_t ev = {.endpoint_valid = true};
    assert(gesture_context_listener(&ev) == ZMK_EV_EVENT_BUBBLE);
    tap(TP_BLACKBERRY_POSITION, 600, 30);
    assert(orientation_selections == 0U);
    ev = (zmk_event_t){.layer_valid = true, .layer_data = {.layer = 2, .state = false}};
    assert(gesture_context_listener(&ev) == ZMK_EV_EVENT_BUBBLE);
    tap(TP_BLACKBERRY_POSITION, 700, 30);
    tap(TP_BLACKBERRY_POSITION, 800, 30);
    assert(orientation_selections == 0U);
}

static void test_bt_same_slot_breaks_mouse_double_without_endpoint_event(void) {
    reset(); fake_aa_pressed = true;
    tap(TP_MOUSE_TOGGLE_POSITION, 10, 30);
    tap(TP_BACK_POSITION, 70, 30); /* The native BT mock raises no endpoint event. */
    tap(TP_MOUSE_TOGGLE_POSITION, 130, 30);
    assert(mouse_toggles == 0U && invoke_count == 2U && invokes[0].param2 == 2U);
    tap(TP_BLACKBERRY_POSITION, 190, 30);
    tap(TP_MOUSE_TOGGLE_POSITION, 250, 30);
    assert(mouse_toggles == 0U && invoke_count == 4U && invokes[2].param2 == 1U);
}

static void test_timing_and_explicit_cancellation(void) {
    reset(); fake_latched = fake_locked = true;
    tap(TP_BLACKBERRY_POSITION, 0, 30);
    tap(TP_BLACKBERRY_POSITION, 100, 30);
    tap(TP_BLACKBERRY_POSITION, 351, 30); /* 221ms gap restarts the sequence. */
    assert(orientation_selections == 0U);
    tap(TP_BLACKBERRY_POSITION, 451, 30);
    tap(TP_BLACKBERRY_POSITION, 551, 30);
    assert(orientation_selections == 1U);

    reset(); fake_latched = fake_locked = true;
    edge(TP_BLACKBERRY_POSITION, true, 0);
    tp_scroll_gesture_cancel();
    edge(TP_BLACKBERRY_POSITION, false, 30);
    tap(TP_BLACKBERRY_POSITION, 100, 30);
    tap(TP_BLACKBERRY_POSITION, 200, 30);
    assert(orientation_selections == 0U && invoke_count == 0U);

    edge(TP_BLACKBERRY_POSITION, true, 400);
    edge(TP_BLACKBERRY_POSITION, false, 581); /* 181ms hold is not a tap. */
    tap(TP_BLACKBERRY_POSITION, 700, 30);
    tap(TP_BLACKBERRY_POSITION, 800, 30);
    assert(orientation_selections == 0U);
}

static void test_press_ownership_survives_context_change(void) {
    reset();
    edge(TP_BACK_POSITION, true, 10); /* BT slot 2 forwarded before lock changes. */
    fake_latched = fake_locked = true;
    edge(TP_BACK_POSITION, false, 30);
    assert(invoke_count == 2U && invokes[1].param2 == 2U && !invokes[1].pressed);
}

int main(void) {
    test_unlocked_bt_selectors_remain_available();
    test_locked_blackberry_triple_rotates_left_to_up();
    test_locked_back_triple_rotates_right_to_up_only();
    test_fixed_presets_ignore_previous_angle_and_repeats();
    test_aA_held_M_double_toggles_mouse_and_suppresses_bt();
    test_aA_held_always_restores_immediate_bt_slots();
    test_F_without_aA_and_context_cancellation();
    test_bt_same_slot_breaks_mouse_double_without_endpoint_event();
    test_timing_and_explicit_cancellation();
    test_press_ownership_survives_context_change();
    puts("PASS UPPER gestures: hands-free locked BlackBerry/Back fixed presets (all prior angles/repeats/switches), held-aA immediate BT slots 1/2, aA+M mouse toggle, timing, context cancellation and press ownership");
    return 0;
}
