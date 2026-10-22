/* Enabled production A320, with both legacy-direct and real micro-runtime
 * builds. Existing pointer/gamepad/micro oracles are included, never edited.
 * Only the physical sensor and transport are mocked; not a screen-speed test. */
#define CONFIG_A320_CURSOR_STEADY 1
#include "test_env.h"
#ifdef TEST_NEGATIVE_BYPASS
/* A compiling counterexample, not a replacement production implementation.
 * Include once first, then bypass ONLY the actual driver's shaping call. */
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_cursor_filter.h"
#define tp_cursor_filter_apply(state, dx, dy, now, x, y) \
    do { (void)(state); (void)(now); *(x)=(dx); *(y)=(dy); } while (0)
#endif
static struct { uint32_t wall; int x, y; } cursor_trace[4096];
static unsigned cursor_trace_count;
static int cursor_report(const struct device *dev, int code, int value, bool sync, int timeout) {
    int result = input_report_rel(dev, code, value, sync, timeout);
    if (code == INPUT_REL_Y && sync) {
        assert(cursor_trace_count < 4096);
        cursor_trace[cursor_trace_count].wall = fake_now;
        cursor_trace[cursor_trace_count].x = report_values[INPUT_REL_X-1];
        cursor_trace[cursor_trace_count++].y = value;
    }
    return result;
}
#define input_report_rel cursor_report
#ifndef TEST_CURSOR_MICRO
#define TEST_CURSOR_MICRO 1
#endif
#if TEST_CURSOR_MICRO
#define CONFIG_BBP9981_GAMEPAD 1
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h"
static bool game_mode;
static uint32_t game_pointer_epoch;
static unsigned game_samples;
static int16_t game_dx, game_dy;
static bool game_valid;
static int64_t k_uptime_get(void) { return fake_now; }
bool bbp_gamepad_layer_active(void) { return game_mode; }
uint32_t bbp_gamepad_pointer_epoch(void) { return game_pointer_epoch; }
void bbp_gamepad_trackpad_poll(int16_t dx,int16_t dy,bool valid,int64_t now) {
    (void)now; assert(game_mode); game_samples++; game_dx=dx; game_dy=dy; game_valid=valid;
}
#define main frozen_micro_suite
#define fixture runtime_fixture
#include "test_micro_runtime.c"
#undef fixture
#undef main
#define CONFIG_A320_TAP_OUTPUT 1
#define CONFIG_A320_MICROTAP 1
void tp_microtap_shoulder_motion(uint32_t now) { (void)now; }
#endif
#define main frozen_pointer_suite
#include "test_pointer.c"
#undef main
#if TEST_CURSOR_MICRO
static void ownership(bool game) {
    if (game_mode != game) { game_mode=game; game_pointer_epoch++; tp_microtap_runtime_interrupt(); }
}
static void bridge_fixture(bool tap) {
    game_mode=false; game_pointer_epoch=last_gamepad_pointer_epoch=game_samples=0;
    game_dx=game_dy=0; game_valid=false;
    fixture(); runtime_fixture(tap);
}
#endif
#undef input_report_rel

static void steady_fixture(bool tap) {
#if TEST_CURSOR_MICRO
    bridge_fixture(tap);
#else
    (void)tap;
    fixture();
#endif
    tp_cursor_filter_reset(&cursor_filter);
    atomic_set(&cursor_context_dirty, 0);
    cursor_trace_count = 0;
}

static void jitter_and_continuous(void) {
    steady_fixture(false);
    for (unsigned i=0; i<1000; i++) {
        int8_t d = i%2 ? -1 : 1;
        poll(d, (int8_t)-d, 1000+i*10);
        expect_xy(0,0);
    }
    for (int slow=0; slow<2; slow++) {
        steady_fixture(false);
        if (slow) key(37,true,900);
        int sx=0,sy=0;
        for (unsigned i=0; i<1000; i++) {
            poll(1,-1,1000+i*10);
            sx+=report_values[0]; sy+=report_values[1];
            if (!i) expect_xy(0,0);
        }
        assert(sx==(slow ? 599 : 1198) && sy==-sx);
        assert(cursor_filter.jitter.x.remainder==1 && cursor_filter.jitter.y.remainder==-1);
    }
    steady_fixture(false);
    poll(1,0,1000); expect_xy(0,0);
    poll(1,0,1010); expect_xy(1,0);
    poll(-1,0,1020); expect_xy(0,0);
    poll(-1,0,1030); expect_xy(0,0);
    poll(-1,0,1040); expect_xy(-1,0);
    for (uint32_t t=1050;t<1150;t+=10) { poll(0,0,t); expect_xy(0,0); }
    poll(-1,0,1160); expect_xy(0,0); /* Per-axis expiry, not accumulated debt. */
    puts("PASS enabled cursor: alternating +/-1 suppression, 1000-frame tiny diagonal/Ctrl, bounded onset loss, reversal with no old-direction output and no zero-input tail");
}

static void spikes_and_idle(void) {
    steady_fixture(false);
    for (uint32_t t=10;t<=40;t+=10) poll(0,0,t);
    poll(100,0,50); expect_xy(58,0); /* RAW100 -> cap50 -> band49 -> gain58. */
    poll(100,0,60); expect_xy(120,0); /* RAW history immediately disarms limiter. */
    idle(111); assert(!cursor_filter.spike.valid && !cursor_filter.jitter.x.active);
    poll(100,0,120); expect_xy(118,0); /* Fresh onset only loses the band count. */
    steady_fixture(false);
    poll(1,3,UINT32_MAX-20); expect_xy(0,2);
    poll(1,3,9); expect_xy(1,4);
    idle(60); poll(1,0,70); expect_xy(0,0);
    puts("PASS enabled cursor: historical one-frame spike cap, sustained fast pass, real idle and uptime rollover");
}

static void transitions_and_faults(void) {
    for (unsigned kind=0;kind<7;kind++) {
        steady_fixture(false); poll(1,0,1000); expect_xy(0,0);
        if (kind==0) { key(37,true,1001); key(37,false,1002); }
        if (kind==1) { key(41,true,1001); key(41,false,1502); }
        if (kind==2) { assert(tp_mouse_orientation_toggle()); }
        if (kind==3) { assert(cursor_context_changed(NULL)==ZMK_EV_EVENT_BUBBLE); }
        if (kind>=4) {
            fake_now=1010; report_count=0;
            if (kind==4) fake_i2c_error=-EIO;
            if (kind==5) burst_error=-EIO;
            if (kind==6) fake_gpio_state=-EIO;
            a320_poll_work_handler(&sensor_data.poll_work.work);
            assert(!report_count && !tp_is_touched());
            fake_i2c_error=burst_error=0;
        }
        poll(1,0,1520); expect_xy(0,0);
    }
    /* Isolate a transition reset from the >50ms expiry used above. */
    steady_fixture(false); poll(1,0,1000); cursor_context_changed(NULL);
    poll(1,0,1010); expect_xy(0,0);
    steady_fixture(false); poll(1,0,1000); key(37,true,1001); key(37,false,1002);
    poll(1,0,1010); expect_xy(0,0);
    steady_fixture(false); poll(3,3,1000); expect_xy(2,2);
    assert(tp_mouse_orientation_toggle()); poll(1,1,1010); expect_xy(0,0);
    poll(3,3,1020); expect_xy(3,-3);
    steady_fixture(false); poll(1,5,1000);
    for (uint32_t t=1010;t<=1060;t+=10) poll(0,5,t);
    poll(1,5,1070); assert(report_values[0]==0); /* Independent X idle. */
    puts("PASS enabled cursor lifecycle: speed/mode/orientation/endpoint epochs, per-axis expiry and GPIO/I2C faults discard state");
}

#if TEST_CURSOR_MICRO
static void tap_raw_and_gamepad(void) {
    for (unsigned fault=0;fault<3;fault++) {
        steady_fixture(true);
        for (uint32_t t=10;t<=40;t+=10) poll(1,0,t);
        assert(tp_microtap_runtime_pending() && !cursor_trace_count);
        fake_now=50; report_count=0;
        if (fault==0) fake_i2c_error=-EIO;
        if (fault==1) burst_error=-EIO;
        if (fault==2) fake_gpio_state=-EIO;
        a320_poll_work_handler(&sensor_data.poll_work.work);
        assert(!report_count && !submissions && !tp_microtap_runtime_pending());
        assert(!cursor_filter.spike.valid && !cursor_filter.jitter.x.active);
        fake_i2c_error=burst_error=0;
        poll(120,0,60); expect_xy(142,0); /* Fresh119*1.2, NOT stale slow-history cap. */
    }
    puts("PASS pending microtap sensor fault: discard old undecided motion before drain; fresh cursor epoch cannot inherit four slow samples, necessary-UP bridge unchanged");
    steady_fixture(true); poll(3,0,10); assert(!report_count && !submissions);
    for (uint32_t t=20;t<=120;t+=10) idle(t);
    idle(130); assert(submissions==1 && !report_count && !cursor_trace_count);
    assert(!cursor_filter.spike.valid && cursor_filter.jitter.x.remainder==0);
    complete(); /* RAW3 remains a qualifying tap; filtered2 would not qualify. */

    steady_fixture(true); poll(1,0,10);
    for (uint32_t t=20;t<=120;t+=10) idle(t);
    poll(0,0,130); assert(!submissions && report_count==2 && cursor_trace_count==1);
    assert(cursor_trace[0].x==0 && cursor_trace[0].y==0); /* Rejected drift is filtered. */

    steady_fixture(true);
    for (uint32_t t=10;t<=90;t+=10) poll(1,0,t);
    assert(!submissions && cursor_trace_count==9 && report_count==18);
    int sum=0;
    for (unsigned i=0;i<9;i++) {
        assert(cursor_trace[i].wall==90 && cursor_trace[i].y==0);
        sum+=cursor_trace[i].x;
    }
    assert(sum==9 && cursor_trace[0].x==0);
    /* Original sample times are 10..90, NOT nine equal callback wall times:
     * history must retain all four slow samples rather than reset on every emit. */
    assert(cursor_filter.spike.count==4);

    steady_fixture(false); poll(1,0,10); ownership(true); poll(-128,127,20);
    assert(game_samples==1 && game_valid && game_dx==-128 && game_dy==127);
    assert(!report_count && !cursor_filter.spike.valid && !cursor_filter.jitter.x.active);
    ownership(false); poll(1,0,30); expect_xy(0,0);
    puts("PASS actual micro/runtime and gamepad: RAW3 tap eligibility unchanged, rejected RAW1 drift filtered, ninth-frame timestamped replay, signed8 gamepad untouched and mode exit starts fresh");
}
#endif

int main(void) {
    jitter_and_continuous(); spikes_and_idle(); transitions_and_faults();
    /* Existing exhaustive wheel expectations run unchanged with feature ON. */
    steady_fixture(false); test_wheel_unchanged();
#if TEST_CURSOR_MICRO
    tap_raw_and_gamepad();
#endif
    puts("PASS enabled actual driver integration; logical reports only, NOT physical jitter/host acceleration acceptance");
    return 0;
}
