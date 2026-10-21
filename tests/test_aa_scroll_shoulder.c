#include "test_env.h"
#include <zephyr/device.h>
#include <dt-bindings/zmk/mouse.h>
#include <zmk/behavior.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_scroll_lock.h"

#define DEVICE_DT_NAME(node) "native_msc"

static bool fake_locked;
static uint32_t fake_modifier_owners;
static int invoke_result;
static unsigned int invoke_count;
static uint32_t invoke_params[16];
static uint32_t invoke_positions[16];
static int invoke_layers[16];
static int64_t invoke_timestamps[16];
static bool invoke_pressed[16];

bool tp_scroll_is_locked(void) { return fake_locked; }
void tp_scroll_shoulder_modifier_update(uint32_t position, bool pressed) {
    uint32_t bit = position == TP_LEFT_SHOULDER_POSITION ? TP_SCROLL_SHOULDER_LEFT_BIT :
                   position == TP_RIGHT_SHOULDER_POSITION ? TP_SCROLL_SHOULDER_RIGHT_BIT : 0U;
    fake_modifier_owners = pressed ? (fake_modifier_owners | bit) : (fake_modifier_owners & ~bit);
}

int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed) {
    assert(invoke_count < 16U);
    assert(strcmp(binding->behavior_dev, "native_msc") == 0);
    invoke_params[invoke_count] = binding->param1;
    invoke_positions[invoke_count] = event.position;
    invoke_layers[invoke_count] = event.layer;
    invoke_timestamps[invoke_count] = event.timestamp;
    invoke_pressed[invoke_count] = pressed;
    invoke_count++;
    return invoke_result;
}

#include "../config/boards/arm/bbp9981/custom_driver/behavior_aa_scroll_shoulder.c"

static struct zmk_behavior_binding wrapper_up = {.behavior_dev = "aa_shoulder", .param1 = SCRL_UP};
static struct zmk_behavior_binding wrapper_down = {.behavior_dev = "aa_shoulder", .param1 = SCRL_DOWN};
static struct zmk_behavior_binding_event edge(uint32_t position, int64_t timestamp) {
    return (struct zmk_behavior_binding_event){.layer = 2, .position = position, .timestamp = timestamp};
}
static void reset(void) {
    fake_locked = false;
    fake_modifier_owners = 0U;
    invoke_result = 0;
    invoke_count = 0U;
    atomic_set(&locked_modifier_mask, 0);
    atomic_set(&native_scroll_mask, 0);
    tp_scroll_rotation_reset();
}
static void test_locked_shoulders_are_modifier_owners(void) {
    reset(); fake_locked = true;
    struct zmk_behavior_binding_event left = edge(TP_LEFT_SHOULDER_POSITION, 101);
    struct zmk_behavior_binding_event right = edge(TP_RIGHT_SHOULDER_POSITION, 202);
    assert(api.binding_pressed(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(fake_modifier_owners == TP_SCROLL_SHOULDER_LEFT_BIT && invoke_count == 0U);
    assert(api.binding_pressed(&wrapper_down, right) == ZMK_BEHAVIOR_OPAQUE);
    assert(fake_modifier_owners == (TP_SCROLL_SHOULDER_LEFT_BIT | TP_SCROLL_SHOULDER_RIGHT_BIT));
    fake_locked = false; /* A release still belongs to the locked press. */
    assert(api.binding_released(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(fake_modifier_owners == TP_SCROLL_SHOULDER_RIGHT_BIT && invoke_count == 0U);
    assert(api.binding_released(&wrapper_down, right) == ZMK_BEHAVIOR_OPAQUE);
    assert(fake_modifier_owners == 0U && invoke_count == 0U);
}
static void test_temporary_scroll_forwards_native_behavior(void) {
    reset();
    struct zmk_behavior_binding_event left = edge(TP_LEFT_SHOULDER_POSITION, 303);
    struct zmk_behavior_binding_event right = edge(TP_RIGHT_SHOULDER_POSITION, 404);
    assert(api.binding_pressed(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(invoke_count == 1U && invoke_params[0] == SCRL_UP && invoke_pressed[0]);
    assert(invoke_positions[0] == left.position && invoke_layers[0] == left.layer && invoke_timestamps[0] == left.timestamp);
    assert(api.binding_released(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(invoke_count == 2U && invoke_params[1] == SCRL_UP && !invoke_pressed[1]);
    assert(api.binding_pressed(&wrapper_down, right) == ZMK_BEHAVIOR_OPAQUE);
    assert(api.binding_released(&wrapper_down, right) == ZMK_BEHAVIOR_OPAQUE);
    assert(invoke_count == 4U && invoke_params[2] == SCRL_DOWN && invoke_params[3] == SCRL_DOWN);
    assert(invoke_positions[2] == TP_RIGHT_SHOULDER_POSITION &&
           invoke_positions[3] == TP_RIGHT_SHOULDER_POSITION);
    assert(invoke_pressed[2] && !invoke_pressed[3]);
}
static void test_failed_native_press_never_creates_orphan_release(void) {
    reset(); invoke_result = 1; /* ZMK transparent/missing-device result. */
    struct zmk_behavior_binding_event left = edge(TP_LEFT_SHOULDER_POSITION, 505);
    assert(api.binding_pressed(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(atomic_get(&native_scroll_mask) == 0 && invoke_count == 1U);
    invoke_result = 0;
    assert(api.binding_released(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(invoke_count == 1U);
}
static void test_native_release_failure_is_retried_before_new_press(void) {
    reset();
    struct zmk_behavior_binding_event left = edge(TP_LEFT_SHOULDER_POSITION, 606);
    assert(api.binding_pressed(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    invoke_result = -EIO;
    assert(api.binding_released(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(atomic_get(&native_scroll_mask) == (int32_t)TP_SCROLL_SHOULDER_LEFT_BIT);
    invoke_result = 0;
    left.timestamp++;
    assert(api.binding_pressed(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(invoke_count == 4U && !invoke_pressed[1] && !invoke_pressed[2] && invoke_pressed[3]);
    assert(atomic_get(&native_scroll_mask) == (int32_t)TP_SCROLL_SHOULDER_LEFT_BIT);
    assert(api.binding_released(&wrapper_up, left) == ZMK_BEHAVIOR_OPAQUE);
    assert(atomic_get(&native_scroll_mask) == 0);
}
static void test_rotation_step_api_and_direction_math(void) {
    reset();
    assert(TP_RIGHT_SHOULDER_POSITION == 1U); /* P1 is the physical right shoulder, not Endcall P6. */
    tp_scroll_rotation_step(TP_SCROLL_ROTATION_LEFT_TO_UP_STEP);
    assert(tp_scroll_rotation_quadrants() == 1U);
    tp_scroll_rotation_step(TP_SCROLL_ROTATION_RIGHT_TO_UP_STEP);
    assert(tp_scroll_rotation_quadrants() == 0U);

    const int16_t input[4][2] = {{-1, 0}, {0, 1}, {1, 0}, {0, -1}};
    const int16_t expected[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
    for (unsigned int i = 0; i < 4U; ++i) {
        int16_t x = input[i][0], y = input[i][1];
        tp_scroll_apply_rotation(1U, &x, &y);
        assert(x == expected[i][0] && y == expected[i][1]);
    }
    int16_t x = INT16_MIN, y = 0;
    tp_scroll_apply_rotation(1U, &x, &y);
    assert(x == 0 && y == INT16_MAX);
}
int main(void) {
    test_locked_shoulders_are_modifier_owners();
    test_temporary_scroll_forwards_native_behavior();
    test_failed_native_press_never_creates_orphan_release();
    test_native_release_failure_is_retried_before_new_press();
    test_rotation_step_api_and_direction_math();
    puts("PASS UPPER shoulder wrapper: P0/P1 native/locked ownership and independent scroll-rotation API");
    return 0;
}
