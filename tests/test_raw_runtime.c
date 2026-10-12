/* Compile the ACTUAL driver and the entire pointer regression suite with raw
 * capture ON. Strict bus mocks reject any write except the vendor pointer0A.
 * Host-only: no board/USB/I2C/HID-device access and no claim of sensor freshness.
 */
#define i2c_write_dt env_i2c_write_dt
#define i2c_burst_read_dt env_i2c_burst_read_dt
#define device_is_ready env_device_is_ready
#include "test_env.h"
#undef i2c_write_dt
#undef i2c_burst_read_dt
#undef device_is_ready
#include <stdarg.h>

#define CONFIG_A320_RAW_CAPTURE 1
#define CONFIG_A320_RAW_CAPTURE_INTERVAL_MS 20
#define CONFIG_A320_RAW_CAPTURE_WINDOW_MS 20000
/* Even a conflicting build must not execute the previous multi-reg probes. */
#define CONFIG_A320_CONTACT_PROBE 1
#define CONFIG_A320_CONTACT_PROBE_INTERVAL_MS 20
#define CONFIG_A320_BUS_COMPARE 1
#define CONFIG_A320_BUS_COMPARE_INTERVAL_MS 20
#define UART_LINE_CTRL_DTR 1

static uint32_t runtime_dtr;
static bool runtime_usb_ready = true, runtime_console_ready = true;
static int runtime_uart_error, runtime_burst_error;
static unsigned runtime_writes, runtime_bursts;
static bool strict_bus, pointer_pending, custom_frame_enabled;
static uint8_t custom_frame[7];
static bool collect_logs;
static unsigned raw_log_count;
static char raw_logs[2048][256];
static bool zmk_usb_is_hid_ready(void) { return runtime_usb_ready; }
static bool device_is_ready(const struct device *dev) {
    return runtime_console_ready && env_device_is_ready(dev);
}
static int uart_line_ctrl_get(const struct device *dev, int ctrl, uint32_t *out) {
    assert(dev == &fake_device && ctrl == UART_LINE_CTRL_DTR);
    *out = runtime_dtr;
    return runtime_uart_error;
}
static int i2c_write_dt(const struct i2c_dt_spec *spec, const void *bytes, size_t n) {
    assert(spec->bus == &fake_device && n == 1 && ((const uint8_t *)bytes)[0] == 0x0A);
    if (strict_bus) { assert(!pointer_pending); }
    runtime_writes++;
    int error = env_i2c_write_dt(spec, bytes, n);
    pointer_pending = error == 0;
    return error;
}
static int i2c_burst_read_dt(const struct i2c_dt_spec *spec, int reg, uint8_t *buf, size_t n) {
    assert(spec->bus == &fake_device && reg == 0x0A && n == 7);
    if (strict_bus) { assert(pointer_pending); }
    pointer_pending = false;
    runtime_bursts++;
    if (runtime_burst_error) {
        /* Dirty partial bytes must still be emitted as na on failure. */
        memset(buf, 0xAA, n);
        return runtime_burst_error;
    }
    int error = env_i2c_burst_read_dt(spec, reg, buf, n);
    if (!error && custom_frame_enabled) { memcpy(buf, custom_frame, n); }
    return error;
}
static void raw_test_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void raw_test_log(const char *format, ...) {
    if (!collect_logs) { return; }
    char line[256];
    va_list args; va_start(args, format);
    int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof(line));
    if (strncmp(line, "TPRAW_MODE ", 11) && strncmp(line, "CAP_BEGIN ", 10) &&
        strncmp(line, "TPR ", 4) && strncmp(line, "CAP_END ", 8)) { return; }
    assert(raw_log_count < sizeof(raw_logs) / sizeof(raw_logs[0]));
    strcpy(raw_logs[raw_log_count++], line);
}
#undef LOG_INF
#define LOG_INF(...) raw_test_log(__VA_ARGS__)
/* No definition is provided: any accidentally retained extra reg API fails
 * compilation with -Werror rather than silently becoming an allowed mock.
 */
#define i2c_reg_read_byte_dt forbidden_extra_register_read
#define i2c_read_dt forbidden_extra_i2c_read
#define main baseline_main
#include "test_pointer.c"
#undef main
#undef i2c_reg_read_byte_dt
#undef i2c_read_dt

static void runtime_fixture(void) {
    fixture();
    memset(&raw_capture, 0, sizeof(raw_capture));
    runtime_dtr = 0; runtime_usb_ready = runtime_console_ready = true;
    runtime_uart_error = runtime_burst_error = 0;
    runtime_writes = runtime_bursts = raw_log_count = 0;
    pointer_pending = custom_frame_enabled = false;
    strict_bus = collect_logs = true;
}
static void expect_line(unsigned index, const char *expected) {
    assert(index < raw_log_count);
    if (strcmp(raw_logs[index], expected)) {
        fprintf(stderr, "schema[%u]\nexpected: %s\nactual:   %s\n", index, expected, raw_logs[index]);
        assert(false);
    }
}
static void expect_last(const char *expected) { expect_line(raw_log_count - 1U, expected); }
static void test_reuse_idle_failure(void) {
    runtime_fixture(); idle(1000);
    assert(runtime_writes == 0 && raw_log_count == 0 && !raw_capture.open);
    runtime_dtr = 1;
    poll(4, -2, 1010); expect_xy(4, -2);
    assert(runtime_writes == 1 && runtime_bursts == 1 && raw_capture.attempt == 1 && raw_capture.seq == 1);
    expect_line(0, "TPRAW_MODE readonly=1 click=0 window_ms=20000 interval_ms=20");
    expect_line(1, "CAP_BEGIN session=1 t=1010");
    expect_line(2, "TPR session=1 attempt=1 seq=1 t=1010 irq=0 obtained=1 err=0 source=motion b=00fe0004000000 raw_dx=4 raw_dy=-2");
    poll(1, 0, 1020); expect_xy(2, 0);
    assert(runtime_writes == 2 && runtime_bursts == 2);
    expect_last("TPR session=1 attempt=2 seq=2 t=1020 irq=0 obtained=1 err=0 source=motion b=00000001000000 raw_dx=1 raw_dy=0");
    idle(1030); assert(runtime_writes == 2);
    const uint8_t bytes[] = {0x91, 0x7F, 0x6C, 0x80, 0xAB, 0xE0, 0x17};
    memcpy(custom_frame, bytes, sizeof(bytes)); custom_frame_enabled = true;
    idle(1040); assert(runtime_writes == 3 && runtime_bursts == 3);
    expect_last("TPR session=1 attempt=3 seq=3 t=1040 irq=1 obtained=1 err=0 source=idle b=917f6c80abe017 raw_dx=-128 raw_dy=127");
    idle(1050); assert(runtime_writes == 3);
    /* Identical bytes are allowed, but each line must have an actual new read. */
    idle(1060); assert(runtime_writes == 4 && runtime_bursts == 4);
    expect_last("TPR session=1 attempt=4 seq=4 t=1060 irq=1 obtained=1 err=0 source=idle b=917f6c80abe017 raw_dx=-128 raw_dy=127");
    fake_i2c_error = -EIO; idle(1080);
    assert(runtime_writes == 5 && runtime_bursts == 4);
    expect_last("TPR session=1 attempt=5 seq=4 t=1080 irq=1 obtained=0 err=-5 source=idle b=na raw_dx=na raw_dy=na");
    fake_i2c_error = 0; runtime_burst_error = -ENXIO;
    idle(1090); assert(runtime_writes == 5);
    idle(1100); assert(runtime_writes == 6 && runtime_bursts == 5);
    expect_last("TPR session=1 attempt=6 seq=4 t=1100 irq=1 obtained=0 err=-6 source=idle b=na raw_dx=na raw_dy=na");
    runtime_burst_error = 0; idle(1110); assert(runtime_writes == 6);
    idle(1120); assert(raw_capture.attempt == 7 && raw_capture.seq == 5);
    fake_i2c_error = -ETIMEDOUT; poll(5, 5, 1130); assert(report_count == 0);
    assert(runtime_writes == 8 && runtime_bursts == 6);
    expect_last("TPR session=1 attempt=8 seq=5 t=1130 irq=0 obtained=0 err=-110 source=motion b=na raw_dx=na raw_dy=na");
    fake_i2c_error = 0; idle(1140); assert(runtime_writes == 8);
    idle(1150); assert(runtime_writes == 9 && runtime_bursts == 7);
    unsigned before_logs = raw_log_count;
    fake_gpio_state = -EIO; fake_now = 1170; report_count = 0;
    a320_poll_work_handler(&sensor_data.poll_work.work);
    assert(runtime_writes == 9 && raw_log_count == before_logs && report_count == 0 && raw_capture.open);
    idle(1180); assert(runtime_writes == 10 && runtime_bursts == 8);
    runtime_dtr = 0; idle(1190);
    expect_last("CAP_END session=1 t=1190 reason=dtr_low attempt=10 seq=7");
    assert(!raw_capture.open && runtime_writes == 10);
    before_logs = raw_log_count; custom_frame_enabled = false;
    poll(4, -2, 1200); expect_xy(4, -2);
    assert(runtime_writes == 11 && runtime_bursts == 9 && raw_log_count == before_logs);
    puts("PASS Raw driver transactions: low single-read reuse before gain, high periodic new reads without cached frames/HID, raw int8 extremes, pointer/burst failure NA and actual attempt counts, GPIO-error missing observations");
}
static void test_timeout_rearm(void) {
    runtime_fixture(); runtime_dtr = 1; idle(1000); idle(20990);
    assert(runtime_writes == 2 && raw_capture.open);
    idle(21000);
    assert(!raw_capture.open && runtime_writes == 2);
    expect_last("CAP_END session=1 t=21000 reason=timeout attempt=2 seq=2");
    idle(41000); assert(runtime_writes == 2 && raw_capture.session == 1);
    unsigned before = raw_log_count;
    poll(6, -6, 41010); expect_xy(7, -7);
    assert(runtime_writes == 3 && raw_log_count == before);
    runtime_dtr = 0; idle(41020); assert(runtime_writes == 3);
    runtime_dtr = 1; idle(41030);
    assert(raw_capture.open && raw_capture.session == 2 && raw_capture.attempt == 1 && raw_capture.seq == 1);
    assert(runtime_writes == 4 && runtime_bursts == 4);
    puts("PASS Raw driver window: 20000ms hard stop at poll begin, continuous high cannot auto-rearm, genuine DTR-low/high restarts; normal motion continues outside capture");
}
static void test_usb_dtr_errors(void) {
    runtime_fixture(); runtime_dtr = 1; idle(1000);
    runtime_usb_ready = false; idle(1010);
    assert(runtime_writes == 1 && !raw_capture.open);
    expect_last("CAP_END session=1 t=1010 reason=usb_off attempt=1 seq=1");
    runtime_usb_ready = true; idle(1020); assert(runtime_writes == 1);
    /* USB off still permits observing a REAL low, not reopening on stale high. */
    runtime_usb_ready = false; runtime_dtr = 0; idle(1030);
    runtime_dtr = 1; idle(1040); runtime_usb_ready = true; idle(1050);
    assert(runtime_writes == 1 && raw_capture.session == 1);
    runtime_dtr = 0; idle(1060); runtime_dtr = 1; idle(1070);
    assert(runtime_writes == 2 && raw_capture.session == 2);
    runtime_uart_error = -EIO; runtime_dtr = 0; idle(1080);
    expect_last("CAP_END session=2 t=1080 reason=dtr_error attempt=1 seq=1");
    runtime_uart_error = 0; runtime_dtr = 1; idle(1090);
    assert(runtime_writes == 2 && !raw_capture.open);
    runtime_dtr = 0; runtime_uart_error = -EIO; idle(1100);
    runtime_dtr = 1; runtime_uart_error = 0; idle(1110); assert(runtime_writes == 2);
    runtime_dtr = 0; idle(1120); runtime_dtr = 1; idle(1130);
    assert(runtime_writes == 3 && raw_capture.session == 3);
    runtime_console_ready = false; idle(1140);
    expect_last("CAP_END session=3 t=1140 reason=dtr_error attempt=1 seq=1");
    runtime_console_ready = true; idle(1150); assert(runtime_writes == 3);
    runtime_dtr = 0; idle(1160); runtime_dtr = 1; idle(1170);
    assert(runtime_writes == 4 && raw_capture.session == 4);
    runtime_usb_ready = false; unsigned before = raw_log_count;
    poll(4, -2, 1180); expect_xy(4, -2);
    assert(runtime_writes == 5 && raw_log_count == before + 1);
    expect_last("CAP_END session=4 t=1180 reason=usb_off attempt=1 seq=1");
    puts("PASS Raw driver USB/DTR: independent debug console DTR, USB/CDC-error/not-ready stop extras, no unknown-as-low rearm, no USB reconnect auto-rearm, normal motion remains available");
}
static void test_rollover(void) {
    runtime_fixture(); runtime_dtr = 1;
    uint32_t start = UINT32_MAX - 10U;
    idle(start); idle(8); assert(runtime_writes == 1);
    idle(9); assert(runtime_writes == 2 && raw_capture.seq == 2);
    idle(start + 20000U); assert(!raw_capture.open && runtime_writes == 2);
    expect_last("CAP_END session=1 t=19989 reason=timeout attempt=2 seq=2");
    idle(20000); assert(runtime_writes == 2);
    runtime_dtr = 0; idle(20010); runtime_dtr = 1; idle(20020);
    assert(runtime_writes == 3 && raw_capture.session == 2);
    puts("PASS Raw driver uint32 rollover: both 20ms transaction cadence and 20000ms window/rearm");
}
static void test_zero_hid(void) {
    runtime_fixture(); runtime_dtr = 1;
    fake_dx = 127; fake_dy = -128;
    int down = key_down_calls, up = key_up_calls, refs = modifier_refs;
    for (uint32_t n = 0; n <= 2000; n++) { idle(1000 + n * 10); }
    assert(runtime_writes == 1000 && runtime_bursts == 1000);
    assert(!raw_capture.open && raw_capture.attempt == 1000 && raw_capture.seq == 1000);
    assert(key_down_calls == down && key_up_calls == up && modifier_refs == refs);
    assert(report_count == 0 && sample_cnt == 0 && sum_dx == 0 && sum_dy == 0);
    assert(cursor_motion.x.remainder == 0 && cursor_motion.y.remainder == 0);
    expect_last("CAP_END session=1 t=21000 reason=timeout attempt=1000 seq=1000");
    /* Capture must not manufacture scroll reports either. */
    runtime_dtr = 0; idle(21010); key(41, true, 21011); key(37, true, 21012);
    runtime_dtr = 1; idle(21020); idle(21040);
    assert(report_count == 0 && sample_cnt == 0 && sum_dx == 0 && sum_dy == 0);
    assert(key_down_calls == down && key_up_calls == up && modifier_refs == refs);
    puts("PASS Raw driver idle zero HID: full 20s/1000 reads with stale nonzero deltas, no cursor/wheel/keyboard/click events; scroll/Ctrl mode also unaffected");
}
int main(void) {
    runtime_dtr = 0;
    baseline_main(); /* Every old pointer/gain/wheel/light/fault test, raw ON. */
    assert(raw_capture.session == 0 && raw_capture.attempt == 0 && raw_capture.seq == 0);
    test_reuse_idle_failure(); test_timeout_rearm(); test_usb_dtr_errors();
    test_rollover(); test_zero_hid();
    puts("PASS Entire test_pointer suite with RAW_CAPTURE compiled ON; exact final TPRAW_MODE/CAP_BEGIN/TPR/CAP_END schema checked against actual driver logs (duration_us omitted)");
    return 0;
}
