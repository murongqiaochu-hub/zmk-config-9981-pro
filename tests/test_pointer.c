/* Actual production helper + poll/listener regressions, not hardware/HID tests. */
#include "test_env.h"
static uint8_t pointer_brightness = 40;
static int burst_error;
uint8_t indicator_tp_get_last_valid_brightness(void) { return pointer_brightness; }
int tp_sym_ctrl_release(int64_t timestamp) { (void)timestamp; return 0; }
static int pointer_burst(const struct i2c_dt_spec *spec, int reg, uint8_t *buf, size_t n) {
    if (burst_error) { return burst_error; }
    return i2c_burst_read_dt(spec, reg, buf, n);
}
#define i2c_burst_read_dt pointer_burst
#include "../config/boards/arm/bbp9981/custom_driver/a320_0x57.c"
#undef i2c_burst_read_dt

static struct a320_dev_config sensor_cfg = {.i2c = {.bus = &fake_device}};
static struct a320_data sensor_data;
static struct device sensor = {.name = "pointer", .config = &sensor_cfg, .data = &sensor_data};

static void fixture(void) {
    memset(&sensor_data, 0, sizeof(sensor_data)); sensor_data.dev = &sensor;
    memset(&scroll_keys, 0, sizeof(scroll_keys));
    atomic_set(&scroll_mode_snapshot, 0); last_scroll_snapshot = 0;
    atomic_set(&cursor_speed_snapshot, 0); last_cursor_speed_snapshot = 0;
    atomic_set(&touched, 0); tp_pointer_reset(&cursor_motion);
    sum_dx = sum_dy = 0; sample_cnt = 0; last_read_time = 0;
    fake_now = 1000; pointer_brightness = 40;
    fake_gpio_state = 0; fake_i2c_error = 0; burst_error = 0;
    report_count = 0; memset(report_values, 0, sizeof(report_values));
}
static void key(uint32_t position, bool down, int64_t timestamp) {
    zmk_event_t event = {.valid = true, .data = {.position = position, .state = down,
                                               .timestamp = timestamp}};
    assert(ctrl_listener_cb(&event) == ZMK_EV_EVENT_BUBBLE);
}
static void poll(int8_t dx, int8_t dy, uint32_t now) {
    fake_gpio_state = 0; fake_dx = dx; fake_dy = dy; fake_now = now;
    report_count = 0; memset(report_values, 0, sizeof(report_values));
    a320_poll_work_handler(&sensor_data.poll_work.work);
}
static void idle(uint32_t now) {
    fake_gpio_state = 1; fake_now = now; report_count = 0;
    a320_poll_work_handler(&sensor_data.poll_work.work);
    assert(report_count == 0);
}
static void expect_xy(int x, int y) {
    assert(report_count == 2);
    assert(report_values[INPUT_REL_X - 1] == x);
    assert(report_values[INPUT_REL_Y - 1] == y);
    assert(report_values[INPUT_REL_WHEEL - 1] == 0);
    assert(report_values[INPUT_REL_HWHEEL - 1] == 0);
}

static void test_conservation(void) {
    for (int slow = 0; slow <= 1; slow++) {
        fixture(); if (slow) { key(37, true, 900); }
        int total_x = 0, total_y = 0;
        for (unsigned i = 0; i < 1000; i++) {
            poll(1, -1, 1000 + i * 10); assert(report_count == 2);
            total_x += report_values[0]; total_y += report_values[1];
        }
        assert(total_x == (slow ? 600 : 1200) && total_y == -total_x);
    }
    /* Exhaust all sensor input and uint8 brightness values with the real helper. */
    for (unsigned brightness = 0; brightness <= 255; brightness++) {
        for (unsigned slow = 0; slow <= 1; slow++) {
            const int64_t gain = 3 * (40 + brightness) * (slow ? 1 : 2);
            for (int raw = -128; raw <= 127; raw++) {
                struct tp_pointer_state state = {0};
                int other = raw == -128 ? 127 : -raw;
                int64_t sx = 0, sy = 0;
                for (unsigned n = 1; n <= 16; n++) {
                    int16_t x, y;
                    tp_pointer_motion(&state, (int8_t)raw, (int8_t)other,
                                      (uint8_t)brightness, slow != 0, n * 10, &x, &y);
                    sx += x; sy += y;
                    int64_t rx = (int64_t)raw * gain * n - sx * 400;
                    int64_t ry = (int64_t)other * gain * n - sy * 400;
                    assert(rx > -400 && rx < 400 && ry > -400 && ry < 400);
                    assert(state.x.remainder == rx && state.y.remainder == ry);
                    assert(x >= -567 && x <= 562 && y >= -567 && y <= 562);
                    if (raw != -128) { assert(x == -y); }
                }
            }
        }
    }
    puts("PASS Pointer arithmetic: +/-1 x1000, both axes, Ctrl, all 256 brightness x 256 raw deltas x both speeds, exact conservation and bounded residuals");
}

static void test_idle_reverse(void) {
    fixture(); key(37, true, 900);
    poll(1, 0, 1000); expect_xy(0, 0);
    idle(1010); poll(0, 0, 1020); expect_xy(0, 0);
    poll(1, 0, 1030); expect_xy(1, 0); /* Short idle must not discard fractions. */
    for (unsigned i = 1; i <= 20; i++) {
        poll(0, 0, 1030 + i * 10); expect_xy(0, 0);
    }
    idle(1300); poll(1, 0, 1310); expect_xy(0, 0);

    fixture(); pointer_brightness = 10;
    poll(1, 0, 1000); expect_xy(0, 0);
    poll(1, 0, 1050); expect_xy(1, 0); /* Inclusive 50 ms. */
    fixture(); pointer_brightness = 10;
    poll(1, 0, 1000); expect_xy(0, 0);
    poll(1, 0, 1051); expect_xy(0, 0); /* 51 ms starts a new axis gesture. */

    fixture(); pointer_brightness = 10;
    poll(1, 1, 1000); expect_xy(0, 0);
    for (unsigned i = 1; i <= 6; i++) { poll(0, 1, 1000 + i * 10); }
    poll(1, 1, 1070); assert(report_values[0] == 0); /* Y cannot refresh X idle time. */

    fixture(); key(37, true, 900);
    poll(1, 0, 1000); expect_xy(0, 0);
    poll(-1, 0, 1010); expect_xy(0, 0);
    poll(-1, 0, 1020); expect_xy(-1, 0);
    poll(0, 1, 1030); expect_xy(0, 0); /* No X tail while Y starts. */

    fixture(); pointer_brightness = 10;
    poll(1, 0, UINT32_MAX - 20); expect_xy(0, 0);
    poll(1, 0, 9); expect_xy(1, 0); /* 30 ms across uptime rollover. */
    fixture(); pointer_brightness = 10;
    poll(1, 0, UINT32_MAX - 20); poll(1, 0, 30); expect_xy(0, 0); /* 51 ms. */
    puts("PASS Pointer lifecycle: short zero/GPIO gaps, stopping without drift, per-axis 50/51 ms idle, direction reversal and uptime rollover");
}

static void test_transitions_faults(void) {
    fixture(); pointer_brightness = 10;
    poll(1, 0, 1000); expect_xy(0, 0);
    pointer_brightness = 20; idle(1010);
    pointer_brightness = 10; idle(1020);
    poll(1, 0, 1030); expect_xy(0, 0); /* Observed gain round-trip clears old remainder. */

    fixture(); pointer_brightness = 10;
    poll(1, 0, 1000); key(37, true, 1001); key(37, false, 1002);
    poll(1, 0, 1010); expect_xy(0, 0); /* Both transitions between two polls. */
    key(37, true, 1011); poll(1, 0, 1020); expect_xy(0, 0);
    key(37, false, 1021); poll(1, 0, 1030); expect_xy(0, 0);

    fixture(); pointer_brightness = 10;
    poll(1, 0, 1000); key(41, true, 1001); key(41, false, 1002);
    poll(1, 0, 1010); expect_xy(0, 0); /* Entire scroll round-trip within one poll. */

    for (unsigned kind = 0; kind < 3; kind++) {
        fixture(); pointer_brightness = 10; poll(1, 0, 1000); expect_xy(0, 0);
        fake_now = 1010; report_count = 0;
        if (kind == 0) { fake_i2c_error = -EIO; }
        if (kind == 1) { burst_error = -EIO; }
        if (kind == 2) { fake_gpio_state = -EIO; }
        a320_poll_work_handler(&sensor_data.poll_work.work);
        assert(report_count == 0 && !tp_is_touched());
        fake_i2c_error = burst_error = 0;
        poll(1, 0, 1020); expect_xy(0, 0);
    }
    puts("PASS Pointer transitions: no-motion gain round-trip, physical Ctrl/mode epochs, I2C write/burst errors and runtime GPIO errors");
}

static void legacy_wheel(int sx, int sy, int *horizontal, int *vertical) {
    int x = 0, y = 0;
    if (abs(sy) >= 128) { x = -sx / 24; y = -sy / 24; }
    else if (abs(sy) >= 64) { x = -sx / 16; y = -sy / 16; }
    else if (abs(sy) >= 32) { x = -sx / 12; y = -sy / 12; }
    else if (abs(sy) >= 21) { x = -sx / 8; y = -sy / 8; }
    else if (abs(sy) >= 3) { x = sx > 0 ? -1 : sx < 0 ? 1 : 0; y = sy > 0 ? -1 : sy < 0 ? 1 : 0; }
    else { x = sx > 0 ? -1 : sx < 0 ? 1 : 0; }
    *horizontal = -x; *vertical = y;
}
static void test_wheel_unchanged(void) {
    for (int slow = 0; slow <= 1; slow++) {
        for (int raw = -128; raw <= 127; raw++) {
            fixture(); key(41, true, 900); if (slow) { key(37, true, 901); }
            int scaled = slow ? raw / 2 : raw;
            for (unsigned i = 0; i < 4; i++) {
                poll((int8_t)raw, (int8_t)raw, 1000 + i * 10);
                if (scaled == 0 || i < 3) { assert(report_count == 0); }
            }
            if (scaled != 0) {
                int horizontal, vertical;
                legacy_wheel(4 * scaled, 4 * scaled, &horizontal, &vertical);
                assert(report_count == 2);
                assert(report_values[INPUT_REL_HWHEEL - 1] == horizontal);
                assert(report_values[INPUT_REL_WHEEL - 1] == vertical);
                assert(report_values[0] == 0 && report_values[1] == 0);
            }
        }
    }
    /* Exercise exact and adjacent curve boundaries using a final +1/-1 packet. */
    const int totals[] = {2, 3, 4, 20, 21, 22, 31, 32, 33, 63, 64, 65, 127, 128, 129};
    for (unsigned i = 0; i < sizeof(totals) / sizeof(totals[0]); i++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            fixture(); key(41, true, 900);
            poll(1, (int8_t)sign, 1000);
            sum_dx = 9; sum_dy = sign * (totals[i] - 1); sample_cnt = 3; last_read_time = 1000;
            poll(1, (int8_t)sign, 1010);
            int horizontal, vertical;
            legacy_wheel(10, sign * totals[i], &horizontal, &vertical);
            assert(report_count == 2 && report_values[2] == horizontal && report_values[3] == vertical);
        }
    }
    puts("PASS Wheel unchanged: original Ctrl integer halving, zero/window behavior, all sensor deltas and every gain boundary +/-1");
}
int main(void) {
    test_conservation(); test_idle_reverse(); test_transitions_faults(); test_wheel_unchanged();
    return 0;
}
