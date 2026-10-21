/* Real AA owner + A320 position listener/poll + gesture + shoulder rotation TUs.
 * The keymap/hold-tap dispatcher, SDK sticky expiration, kernel and HID sink are
 * host models here. test_aa_layer_owner.c separately runs the byte-exact SDK
 * sticky TU. This is NOT a device, full ZMK event-manager or radio test.
 */
#include "test_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_scroll_lock.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_key_state.h"

#define DEVICE_DT_NAME(node) "native"
#define BT_SEL 7U
#define SCRL_UP 10U
#define SCRL_DOWN 11U
#undef BEHAVIOR_DT_INST_DEFINE
#define BEHAVIOR_DT_INST_DEFINE(...)

static int pipeline_layer_to(int layer);
static int pipeline_layer_activate(int layer);
static int pipeline_layer_deactivate(int layer);
static bool pipeline_layer_active(int layer);
#define zmk_keymap_layer_to pipeline_layer_to
#define zmk_keymap_layer_activate pipeline_layer_activate
#define zmk_keymap_layer_deactivate pipeline_layer_deactivate
#define zmk_keymap_layer_active pipeline_layer_active

static unsigned bt_down[4], bt_up[4], native_wheel_edges;
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool down) {
    (void)event;
    if (binding->param1 == BT_SEL) {
        assert(binding->param2 < 4U);
        (down ? bt_down : bt_up)[binding->param2]++;
    } else {
        assert(binding->param1 == SCRL_UP || binding->param1 == SCRL_DOWN);
        native_wheel_edges++;
    }
    return 0;
}
uint8_t indicator_tp_get_last_valid_brightness(void) { return 40; }
int tp_sym_ctrl_release(int64_t timestamp) { (void)timestamp; return 0; }
int tp_sym_ctrl_update(bool aa, bool sym, int64_t timestamp) {
    (void)aa; (void)sym; (void)timestamp; return 0;
}
#include "../config/boards/arm/bbp9981/custom_driver/a320_0x57.c"
#undef DT_DRV_COMPAT
#define api shoulder_api
#define pressed shoulder_pressed
#define released shoulder_released
#include "../config/boards/arm/bbp9981/custom_driver/behavior_aa_scroll_shoulder.c"
#undef released
#undef pressed
#undef api
#undef DT_DRV_COMPAT
#define api gesture_api
#define pressed gesture_pressed
#define released gesture_released
#include "../config/boards/arm/bbp9981/custom_driver/behavior_aa_scroll_gesture_key.c"
#undef released
#undef pressed
#undef api
#undef DT_DRV_COMPAT
#include "../config/boards/arm/bbp9981/custom_driver/behavior_aa_layer.c"

static uint32_t active_layers;
static int pipeline_layer_event(int layer, bool active) {
    uint32_t bit = 1U << layer;
    if (((active_layers & bit) != 0) == active) { return 0; }
    if (active) { active_layers |= bit; } else { active_layers &= ~bit; }
    fake_layer = (active_layers & (1U << 2)) ? 2 : 0;
    zmk_event_t ev = {.layer_valid = true, .layer_data = {.layer = layer, .state = active}};
    assert(aa_layer_state_listener(&ev) == 0);
    assert(gesture_context_listener(&ev) == 0);
    return 0;
}
static int pipeline_layer_activate(int layer) { return pipeline_layer_event(layer, true); }
static int pipeline_layer_deactivate(int layer) { return pipeline_layer_event(layer, false); }
static bool pipeline_layer_active(int layer) { return (active_layers & (1U << layer)) != 0; }
static int pipeline_layer_to(int layer) {
    for (int i = 1; i < 4; ++i) { pipeline_layer_event(i, false); }
    if (layer != 0) { pipeline_layer_event(layer, true); }
    return 0;
}

static struct a320_dev_config sensor_cfg = {.i2c = {.bus = &fake_device}};
static struct a320_data sensor_data;
static struct device sensor = {.name = "pipeline", .config = &sensor_cfg, .data = &sensor_data};
static bool listener_first;
static bool pending_hold;
static int64_t pending_down, sticky_deadline;
static unsigned press_binding[42];
enum { B_NONE, B_AA_PENDING, B_AA_MO, B_AA_FIXED, B_GESTURE, B_SHOULDER };

static void owner(unsigned op, bool down, int64_t at) {
    struct zmk_behavior_binding binding = {.param1 = op};
    struct zmk_behavior_binding_event ev = {.layer = 2, .position = 41, .timestamp = at};
    assert((down ? aa_layer_api.binding_pressed(&binding, ev) :
                   aa_layer_api.binding_released(&binding, ev)) == 0);
}
static void dispatch(uint32_t position, bool down, int64_t at) {
    if (down && position != 41 && pending_hold) {
        owner(0, true, pending_down); pending_hold = false; press_binding[41] = B_AA_MO;
    }
    if (down) {
        if (position == 41) {
            if (pipeline_layer_active(2)) {
                press_binding[position] = B_AA_FIXED; owner(2, true, at);
            } else {
                press_binding[position] = B_AA_PENDING; pending_hold = true; pending_down = at;
            }
        } else if (pipeline_layer_active(2) && (position == 3 || position == 5 || position == 20)) {
            press_binding[position] = B_GESTURE;
        } else if (pipeline_layer_active(2) && position < 2) {
            press_binding[position] = B_SHOULDER;
        } else { press_binding[position] = B_NONE; }
    }
    struct zmk_behavior_binding_event ev = {.layer = 2, .position = position, .timestamp = at};
    struct zmk_behavior_binding binding = {.param1 = position == 0 ? SCRL_UP : SCRL_DOWN};
    switch (press_binding[position]) {
    case B_GESTURE:
        assert((down ? gesture_api.binding_pressed(&binding, ev) :
                       gesture_api.binding_released(&binding, ev)) == 0);
        break;
    case B_SHOULDER:
        assert((down ? shoulder_api.binding_pressed(&binding, ev) :
                       shoulder_api.binding_released(&binding, ev)) == 0);
        break;
    case B_AA_PENDING:
        if (!down) {
            owner(1, true, pending_down); /* SDK tap child DOWN; UP starts its timer. */
            sticky_deadline = pending_down + 3000; pending_hold = false;
        }
        break;
    case B_AA_MO: if (!down) { owner(0, false, at); } break;
    case B_AA_FIXED: if (!down) { owner(2, false, at); } break;
    default: break;
    }
    if (!down) { press_binding[position] = B_NONE; }
}
static void key(uint32_t position, bool down, int64_t at) {
    fake_now = (uint32_t)at;
    zmk_event_t ev = {.valid = true, .data = {.position = position, .state = down, .timestamp = at}};
    if (listener_first) { assert(ctrl_listener_cb(&ev) == 0); }
    dispatch(position, down, at);
    if (!listener_first) { assert(ctrl_listener_cb(&ev) == 0); }
}
static void tap(uint32_t position, int64_t at) { key(position, true, at); key(position, false, at + 30); }
static void triple(uint32_t position, int64_t at) { for (unsigned i = 0; i < 3; ++i) { tap(position, at + i * 100); } }
static void advance(uint32_t at) {
    fake_now = at;
    if (sticky_deadline >= 0 && at >= (uint64_t)sticky_deadline) {
        int64_t deadline = sticky_deadline; sticky_deadline = -1;
        owner(1, false, deadline);
    }
}
static void fixture(void) {
    memset(&scroll_keys, 0, sizeof(scroll_keys));
    memset(&sensor_data, 0, sizeof(sensor_data)); sensor_data.dev = &sensor;
    memset(press_binding, 0, sizeof(press_binding));
    memset(bt_down, 0, sizeof(bt_down)); memset(bt_up, 0, sizeof(bt_up));
    native_wheel_edges = 0; pending_hold = false; sticky_deadline = -1;
    active_layers = 1U; fake_layer = 0;
    aa_layer_clear_owners(); aa_internal_transition = 0;
    atomic_set(&scroll_mode_snapshot, 0); last_scroll_snapshot = 0;
    atomic_set(&mouse_orientation_snapshot, 0); last_mouse_orientation_reversed = false;
    atomic_set(&scroll_shoulder_modifier_snapshot, 0); last_scroll_locked = last_shoulder_slow = false;
    atomic_set(&locked_modifier_mask, 0); atomic_set(&native_scroll_mask, 0);
    tp_scroll_rotation_reset(); last_scroll_rotation = 0;
    gesture = (struct gesture_state){.sequence_position = UINT32_MAX}; bt_forwarded_mask = 0;
    atomic_set(&cursor_speed_snapshot, 0); last_cursor_speed_snapshot = 0;
    atomic_set(&touched, 0); tp_pointer_reset(&cursor_motion);
    sum_dx = sum_dy = 0; sample_cnt = 0; last_read_time = 0;
    shoulder_scroll_remainder_x = shoulder_scroll_remainder_y = 0;
    fake_gpio_state = 0; fake_i2c_error = 0; fake_now = 0; sym_physically_pressed = false;
}
static void lock_then_expire_old_sticky(void) {
    tap(41, 0); tap(41, 100);
    assert(tp_scroll_is_locked() && aa_fixed_owned && pipeline_layer_active(2));
    advance(3300);
    assert(tp_scroll_is_locked() && aa_fixed_owned && pipeline_layer_active(2));
}
static void scroll_window(int8_t dx, int8_t dy, uint32_t at, int horizontal, int vertical) {
    for (unsigned i = 0; i < 4; ++i) {
        fake_now = at + i * 10; fake_dx = dx; fake_dy = dy; report_count = 0;
        memset(report_values, 0, sizeof(report_values));
        a320_poll_work_handler(&sensor_data.poll_work.work);
    }
    assert(report_count == 2 && report_values[2] == horizontal && report_values[3] == vertical);
}
static void run_pipeline_cases(void) {
    fixture(); lock_then_expire_old_sticky();
    triple(3, 3400); assert(tp_scroll_rotation_quadrants() == 1);
    scroll_window(-10, 0, 4000, 0, 1);
    assert(!bt_down[1] && !bt_down[2]);
    key(0, true, 4100); assert(tp_scroll_shoulder_slow_active() && !native_wheel_edges);
    key(0, false, 4200); assert(!tp_scroll_shoulder_slow_active());

    fixture(); lock_then_expire_old_sticky();
    triple(5, 3400); assert(tp_scroll_rotation_quadrants() == 3);
    scroll_window(10, 0, 4000, 0, 1);
    assert(!bt_down[1] && !bt_down[2]);

    for (unsigned latched = 0; latched < 2; ++latched) {
        for (unsigned release_aA_first = 0; release_aA_first < 2; ++release_aA_first) {
            fixture(); if (latched) { lock_then_expire_old_sticky(); }
            key(41, true, 5000);
            tap(3, 5100); assert(bt_down[1] == 1 && bt_up[1] == 1);
            key(5, true, 5200); assert(bt_down[2] == 1 && !bt_up[2]);
            if (release_aA_first) { key(41, false, 5300); key(5, false, 5400); }
            else { key(5, false, 5300); key(41, false, 5400); }
            assert(bt_up[2] == 1 && !tp_mouse_orientation_reversed());
            assert(tp_scroll_rotation_quadrants() == 0);
        }
    }
    fixture(); lock_then_expire_old_sticky(); triple(3, 3400);
    assert(tp_scroll_rotation_quadrants() == 1);
    tap(41, 4100); tap(41, 4200);
    assert(!tp_scroll_lock_is_latched() && tp_scroll_rotation_quadrants() == 0);
    tap(41, 5000); tap(41, 5100);
    assert(tp_scroll_is_locked() && tp_scroll_rotation_quadrants() == 0);
    pipeline_layer_to(0); /* Explicit SYM/to exits layer, not the independent latch. */
    assert(!pipeline_layer_active(2) && tp_scroll_lock_is_latched());

    fixture(); key(41, true, 10); tap(20, 20); tap(20, 120);
    assert(tp_mouse_orientation_reversed() && !bt_down[1] && !bt_down[2]);
    key(41, false, 200);
    assert(!pipeline_layer_active(2));
    fixture(); lock_then_expire_old_sticky();
    key(41, true, 3400); tap(20, 3500); tap(20, 3600); key(41, false, 3700);
    assert(tp_mouse_orientation_reversed() && pipeline_layer_active(2) && tp_scroll_is_locked());
    assert(!bt_down[1] && !bt_down[2]);
}
static void fixed_presets_all_directions(void) {
    const uint32_t positions[2] = {TP_BLACKBERRY_POSITION, TP_BACK_POSITION};
    const uint8_t targets[2] = {TP_SCROLL_ROTATION_LEFT_TO_UP_FIXED,
                                TP_SCROLL_ROTATION_RIGHT_TO_UP_FIXED};
    /* Actual sensor vectors: left/right/up/down. Keep the original unequal
     * horizontal/vertical wheel gains; only direction mapping is selected. */
    const int8_t raw[4][2] = {{-10,0},{10,0},{0,-10},{0,10}};
    const int16_t expected[2][4][2] = {
        {{0,1},{0,-1},{3,0},{-3,0}},
        {{0,-1},{0,1},{-3,0},{3,0}},
    };
    for (unsigned preset = 0; preset < 2U; ++preset) {
        for (unsigned previous = 0; previous < 4U; ++previous) {
            fixture(); lock_then_expire_old_sticky();
            tp_scroll_rotation_select((uint8_t)previous);
            for (unsigned repeat = 0; repeat < 3U; ++repeat) {
                triple(positions[preset], 3400 + repeat * 500);
                assert(tp_scroll_rotation_quadrants() == targets[preset]);
            }
            for (unsigned i = 0; i < 4U; ++i) {
                scroll_window(raw[i][0], raw[i][1], 4700 + i * 40,
                              expected[preset][i][0], expected[preset][i][1]);
            }
            triple(positions[1U-preset], 5000);
            assert(tp_scroll_rotation_quadrants() == targets[1U-preset]);
            triple(positions[1U-preset], 5400);
            assert(tp_scroll_rotation_quadrants() == targets[1U-preset]);
            triple(positions[preset], 5800);
            assert(tp_scroll_rotation_quadrants() == targets[preset]);
            key(41, true, 6200);
            scroll_window(0, -10, 6300, 0, 3); /* Held aA temporarily uses original direction. */
            key(41, false, 6500);
            assert(tp_scroll_rotation_quadrants() == targets[preset]);
            scroll_window(0, -10, 6600, expected[preset][2][0], expected[preset][2][1]);
            assert(!bt_down[1] && !bt_down[2] && !tp_mouse_orientation_reversed());
        }
    }
}
int main(void) {
    for (unsigned order = 0; order < 2; ++order) {
        listener_first = order != 0; run_pipeline_cases(); fixed_presets_all_directions();
    }
    puts("PASS scroll pipeline: real A320/AA-owner/gesture/rotation, both position-listener orders, delayed sticky UP beyond 3s, P3/P5 fixed presets -> all four actual wheel directions, repeated/switch selection without accumulation, held-aA override/restore, shoulder half-speed, immediate BT with both release orders, aA+F mouse; keymap/hold-tap and kernel are host models, not device acceptance");
    return 0;
}
