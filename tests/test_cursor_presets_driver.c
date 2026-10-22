/* SPDX-License-Identifier: MIT
 * Actual feature-ON A320 + filter + optional actual microtap runtime pipeline.
 * Frozen pointer/micro suites are included verbatim for fixtures/oracles only.
 * Build with TEST_CURSOR_MICRO=0 and =1, using .host-test-build/include.
 * TEST_NEGATIVE_LIVE_GAIN is a compiling counterexample: the I2C interleave
 * oracle must reject bypassing ONLY the real pointer's immutable frame gain.
 */
#define CONFIG_BBP9981_CURSOR_SPEED 1
#define CONFIG_A320_CURSOR_STEADY 1
#define CONFIG_SETTINGS 0
#define i2c_burst_read_dt presets_native_burst
#include "test_env.h"
#undef i2c_burst_read_dt

/* Only missing kernel declarations; no existing mock or fixture is changed. */
typedef int32_t atomic_val_t;
#define ATOMIC_INIT(value) (value)
struct k_spinlock { bool held; };
typedef unsigned k_spinlock_key_t;
static k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held); lock->held = true; return 0U;
}
static void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    (void)key; assert(lock->held); lock->held = false;
}
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_cursor_speed.c"
#ifdef TEST_NEGATIVE_LIVE_GAIN
#define bbp_cursor_speed_motion_gain bbp_cursor_speed_gain
#endif

/* Hook the physical mock only. The real I2C decoding and the frozen
 * pointer_burst fixture still execute unchanged, including error handling.
 */
static void (*presets_after_i2c)(void);
static int i2c_burst_read_dt(const struct i2c_dt_spec *spec, int reg, uint8_t *buf, size_t len) {
    int ret = presets_native_burst(spec, reg, buf, len);
    if (ret == 0 && presets_after_i2c != NULL) {
        void (*hook)(void) = presets_after_i2c;
        presets_after_i2c = NULL;
        hook();
    }
    return ret;
}

static struct { uint32_t wall; int x, y; } cursor_trace[4096];
static unsigned cursor_trace_count;
static int cursor_report(const struct device *dev, int code, int value, bool sync, int timeout) {
    int ret = input_report_rel(dev, code, value, sync, timeout);
    if (code == INPUT_REL_Y && sync) {
        assert(cursor_trace_count < 4096U);
        cursor_trace[cursor_trace_count].wall = fake_now;
        cursor_trace[cursor_trace_count].x = report_values[INPUT_REL_X - 1];
        cursor_trace[cursor_trace_count++].y = value;
    }
    return ret;
}
#define input_report_rel cursor_report
#ifndef TEST_CURSOR_MICRO
#define TEST_CURSOR_MICRO 1
#endif
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
void bbp_gamepad_trackpad_poll(int16_t dx, int16_t dy, bool valid, int64_t now) {
    (void)now; assert(game_mode); ++game_samples; game_dx = dx; game_dy = dy; game_valid = valid;
}
#if TEST_CURSOR_MICRO
#define main frozen_presets_micro_suite
#define fixture runtime_fixture
#include "test_micro_runtime.c"
#undef fixture
#undef main
#define CONFIG_A320_TAP_OUTPUT 1
#define CONFIG_A320_MICROTAP 1
void tp_microtap_shoulder_motion(uint32_t now) { (void)now; }
#endif
#define main frozen_presets_pointer_suite
#include "test_pointer.c"
#undef main
#undef input_report_rel

static const uint16_t expected_gains[5] = {240U, 360U, 480U, 600U, 720U};
static uint8_t selected_index(void) {
    return bbp_cursor_speed_snapshot_index(bbp_cursor_speed_snapshot());
}
static void select_index(uint8_t index) {
    assert(index < 5U);
    for (unsigned step = 0U; step < 5U && selected_index() != index; ++step) {
        assert(bbp_cursor_speed_cycle() == 0);
    }
    assert(selected_index() == index);
}
static int32_t expected_gain(uint8_t index, bool slow) {
    return (int32_t)expected_gains[index] / (slow ? 2 : 1);
}
static void cycle_steps(unsigned steps) {
    for (unsigned step = 0U; step < steps; ++step) { assert(bbp_cursor_speed_cycle() == 0); }
}
static void ownership(bool game) {
    if (game_mode != game) {
        game_mode = game; ++game_pointer_epoch;
#if TEST_CURSOR_MICRO
        tp_microtap_runtime_interrupt();
#endif
    }
}
static void presets_fixture(uint8_t index, bool slow, bool tap) {
    game_mode = false;
    game_pointer_epoch = last_gamepad_pointer_epoch = 0U;
    game_samples = 0U; game_dx = game_dy = 0; game_valid = false;
    presets_after_i2c = NULL;
    fixture();
#if TEST_CURSOR_MICRO
    runtime_fixture(tap);
#else
    (void)tap;
#endif
    select_index(index);
    tp_cursor_filter_reset(&cursor_filter);
    tp_pointer_reset(&cursor_motion);
    atomic_set(&cursor_context_dirty, 0);
    if (slow) { key(37U, true, 0); }
    /* Synchronize through two actual, empty polls: the first consumes the
     * previous context epoch; the second establishes the enabled tap context.
     * Only A320 calls begin_motion_frame. No test writes its selected/latch state.
     */
    idle(0U); idle(0U);
    cursor_trace_count = report_count = 0U;
    memset(report_values, 0, sizeof(report_values));
    assert(last_cursor_preset_snapshot == bbp_cursor_speed_snapshot());
    assert(bbp_cursor_speed_motion_gain(slow) == (uint16_t)expected_gain(index, slow));
}

static void test_actual_filter_all_gains_and_half(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            int32_t gain = expected_gain(index, slow != 0U);
            presets_fixture(index, slow != 0U, false);
            for (unsigned frame = 0U; frame < 1000U; ++frame) {
                int8_t delta = (int8_t)((frame & 1U) != 0U ? -1 : 1);
                poll(delta, (int8_t)-delta, 10U + frame * 10U);
                expect_xy(0, 0);
            }
            presets_fixture(index, slow != 0U, false);
            int64_t sum_x = 0, sum_y = 0;
            for (unsigned frame = 0U; frame < 1000U; ++frame) {
                pointer_brightness = (uint8_t)frame;
                fake_brightness = (uint8_t)(255U - (frame & 255U));
                poll(1, -1, 10U + frame * 10U);
                assert(report_count == 2U);
                sum_x += report_values[0]; sum_y += report_values[1];
                /* Exactly one onset RAW count/axis is retained by Schmitt. */
                assert(sum_x == (int64_t)frame * gain / 400 && sum_y == -sum_x);
                assert(cursor_motion.gain == (uint16_t)gain);
            }
            assert(cursor_filter.jitter.x.remainder == 1 && cursor_filter.jitter.y.remainder == -1);
            assert(cursor_motion.x.remainder == (int64_t)999 * gain - sum_x * 400);
            assert(brightness_reads == 0U);
            for (uint32_t now = 10010U; now <= 10060U; now += 10U) {
                poll(0, 0, now); expect_xy(0, 0);
            }
        }
    }
    puts("PASS presets actual A320: all gains/Ctrl, +/-1 suppression, 1000-frame signed exact filtered sums, LED-independent fractions and no zero-input tail");
}

static void seed_slow_history_with_fraction(void) {
    poll(2, -2, 10U);
    for (uint32_t now = 20U; now <= 50U; now += 10U) { poll(0, 0, now); expect_xy(0, 0); }
    assert(cursor_filter.spike.valid && cursor_filter.spike.count == 4U);
    assert(cursor_filter.jitter.x.remainder == 1 && cursor_filter.jitter.y.remainder == -1);
    assert(cursor_motion.x.remainder != 0 && cursor_motion.y.remainder != 0);
}
static void assert_cursor_epoch_cleared(void) {
    assert(!cursor_filter.spike.valid && cursor_filter.spike.count == 0U);
    assert(!cursor_filter.jitter.x.active && !cursor_filter.jitter.y.active);
    assert(cursor_filter.jitter.x.remainder == 0 && cursor_filter.jitter.y.remainder == 0);
    assert(!cursor_motion.x.active && !cursor_motion.y.active);
    assert(cursor_motion.x.remainder == 0 && cursor_motion.y.remainder == 0);
#if TEST_CURSOR_MICRO
    assert(!tp_microtap_runtime_pending());
#endif
}
static void test_actual_epoch_resets_including_five_cycle_roundtrip(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            for (unsigned steps = 1U; steps <= 5U; steps += 4U) {
                presets_fixture(index, slow != 0U, false);
                seed_slow_history_with_fraction();
                uint32_t before = bbp_cursor_speed_snapshot();
                cycle_steps(steps);
                uint32_t after = bbp_cursor_speed_snapshot();
                assert(after != before);
                assert(bbp_cursor_speed_snapshot_generation(after) ==
                       bbp_cursor_speed_snapshot_generation(before) + steps);
                if (steps == 5U) { assert(selected_index() == index); }
                idle(60U);
                assert_cursor_epoch_cleared();
                assert(last_cursor_preset_snapshot == after);
                int32_t gain = expected_gain(selected_index(), slow != 0U);
                assert(cursor_motion.gain == (uint16_t)gain);
                poll(100, 0, 70U);
                expect_xy(99 * gain / 400, 0); /* No stale four-slow cap or old fraction. */
            }
        }
    }
    puts("PASS presets actual A320: every gain/Ctrl, one-step and five-step same-index epochs clear both fractional axes, spike history and Schmitt state before reuse");
}

static void test_actual_zero_reverse_and_exact_idle_boundary(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            int32_t gain = expected_gain(index, slow != 0U);
            presets_fixture(index, slow != 0U, false);
            poll(1, 0, 10U); expect_xy(0, 0);
            poll(1, 0, 20U); expect_xy(gain / 400, 0);
            poll(0, 0, 30U); expect_xy(0, 0);
            poll(0, 0, 40U); expect_xy(0, 0);
            poll(-1, 0, 50U); expect_xy(0, 0);
            poll(-1, 0, 60U); expect_xy(0, 0);
            poll(-1, 0, 70U); expect_xy(-gain / 400, 0);
            idle(121U); assert_cursor_epoch_cleared();
            poll(-1, 0, 130U); expect_xy(0, 0);
            for (unsigned gap = 50U; gap <= 51U; ++gap) {
                presets_fixture(index, slow != 0U, false);
                poll(1, 0, 10U); expect_xy(0, 0);
                poll(1, 0, 10U + gap);
                expect_xy(gap == 50U ? gain / 400 : 0, 0);
                assert(cursor_motion.x.active == (gap == 50U));
                assert(cursor_motion.x.remainder == (gap == 50U ? gain % 400 : 0));
            }
        }
    }
    puts("PASS presets actual A320: all gains/Ctrl zero/reversal and strict50/51ms filter/fraction expiry");
}

static unsigned interleave_steps;
static void cycle_inside_i2c(void) {
    assert(interleave_steps == 1U || interleave_steps == 5U);
    cycle_steps(interleave_steps);
}
static void test_i2c_interleave_is_one_coherent_motion_frame(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            for (unsigned steps = 1U; steps <= 5U; steps += 4U) {
                presets_fixture(index, slow != 0U, false);
                seed_slow_history_with_fraction();
                int32_t old_gain = expected_gain(index, slow != 0U);
                int32_t old_remainder = cursor_motion.x.remainder;
                uint32_t captured = last_cursor_preset_snapshot;
                interleave_steps = steps;
                presets_after_i2c = cycle_inside_i2c;
                cursor_trace_count = 0U;
                poll(100, 0, 60U);
                /* This already-begun frame must use OLD gain and OLD history.
                 * Four slow RAW samples cap100->50; the old +1 band remainder
                 * makes filtered X50, and its old pointer fraction is retained.
                 */
                expect_xy((50 * old_gain + old_remainder) / 400, 0);
                assert(cursor_trace_count == 1U && cursor_trace[0].wall == 60U);
                assert(presets_after_i2c == NULL);
                assert(bbp_cursor_speed_snapshot() != captured);
                assert(last_cursor_preset_snapshot == captured);
                assert(cursor_motion.gain == (uint16_t)old_gain);
                assert(bbp_cursor_speed_motion_gain(slow != 0U) == (uint16_t)old_gain);
                uint32_t selected = bbp_cursor_speed_snapshot();
                int32_t new_gain = expected_gain(selected_index(), slow != 0U);
                assert(bbp_cursor_speed_gain(slow != 0U) == (uint16_t)new_gain);
                if (steps == 5U) { assert(new_gain == old_gain && selected_index() == index); }
                idle(70U); /* Next complete poll consumes the new generation. */
                assert(last_cursor_preset_snapshot == selected);
                assert_cursor_epoch_cleared();
                assert(cursor_motion.gain == (uint16_t)new_gain);
                poll(100, 0, 80U); expect_xy(99 * new_gain / 400, 0);
            }
        }
    }
    puts("PASS presets I2C interleave: every gain/Ctrl and1/5 cycles retain one old frame, then apply new gain+epoch with no mixed filter/fraction");
}

#if TEST_CURSOR_MICRO
static void quiet_until(uint32_t first, uint32_t last) {
    for (uint32_t now = first; now <= last; now += 10U) { idle(now); }
}
static int minimum_raw_for_tap(int32_t gain) { return (3 * 400 + gain - 1) / gain; }
static void test_actual_raw_shadow_thresholds_follow_gain(void) {
    assert(TP_MICROTAP_MIN_CURSOR_DISTANCE_PX == 3U && TP_MICROTAP_MAX_CURSOR_DISTANCE_PX == 50U);
    assert(TP_MICROTAP_QUIET_MS == 120U && TP_MICROTAP_MAX_FRAMES == 8U);
    assert(TP_MICROTAP_MAX_MOTION_SPAN_MS == 120U && TP_MICROTAP_CLICK_INTERVAL_MS == 300U);
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            int32_t gain = expected_gain(index, slow != 0U);
            for (int sign = -1; sign <= 1; sign += 2) {
                for (unsigned admitted = 0U; admitted < 2U; ++admitted) {
                    presets_fixture(index, slow != 0U, true);
                    int raw = minimum_raw_for_tap(gain) - (admitted == 0U ? 1 : 0);
                    poll((int8_t)(sign * raw), 0, 10U);
                    assert(tp_microtap_runtime_pending() && !cursor_trace_count && !submissions);
                    assert(gesture.measure_pointer.gain == (uint16_t)gain);
                    assert(gesture.cursor_sum_x == sign * raw * gain / 400);
                    assert(gesture.measure_pointer.x.remainder == sign * raw * gain % 400);
                    assert(!cursor_filter.spike.valid && cursor_motion.x.remainder == 0);
                    quiet_until(20U, 120U); assert(submissions == 0U);
                    poll(0, 0, 130U); /* Rejected RAW candidates legitimately replay here. */
                    if (admitted != 0U) {
                        assert(submissions == 1U && cursor_trace_count == 0U);
                        assert(!cursor_filter.spike.valid); complete();
                    } else {
                        assert(submissions == 0U && cursor_trace_count == 1U);
                        assert(cursor_trace[0].x == sign * (raw - 1) * gain / 400);
                    }
                }
                /* Test the 50px/51px boundary even when total RAW exceeds127:
                 * two same-direction signed8 frames stay within the old8/120ms limits.
                 */
                int first_over = (51 * 400 + gain - 1) / gain;
                for (unsigned over = 0U; over < 2U; ++over) {
                    presets_fixture(index, slow != 0U, true);
                    int total = first_over - (over == 0U ? 1 : 0);
                    int first = total / 2, second = total - first;
                    assert(first > 0 && second <= 127);
                    poll((int8_t)(sign * first), 0, 10U);
                    poll((int8_t)(sign * second), 0, 20U);
                    if (over == 0U) {
                        assert(gesture.count == 2U && gesture.cursor_sum_x == sign * total * gain / 400);
                        assert(gesture.measure_pointer.gain == (uint16_t)gain);
                        assert(!cursor_trace_count && !submissions);
                        quiet_until(30U, 130U); idle(140U);
                        assert(submissions == 1U && !cursor_trace_count); complete();
                    } else {
                        assert(submissions == 0U && cursor_trace_count == 2U);
                        assert(!tp_microtap_runtime_pending() && gesture.swipe_rearm);
                        assert(cursor_trace[0].x + cursor_trace[1].x == sign * (total - 1) * gain / 400);
                    }
                }
            }
        }
    }
    puts("PASS presets actual RAW shadow: all gains/Ctrl and signs track selected gain at unchanged3/50px,120ms,8-frame policy; discarded taps never touch filter/fractions");
}

static void test_actual_epoch_cancels_pending_tap_and_queued_replay(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            for (unsigned steps = 1U; steps <= 5U; steps += 4U) {
                presets_fixture(index, slow != 0U, true);
                int raw = minimum_raw_for_tap(expected_gain(index, slow != 0U));
                poll((int8_t)raw, 0, 10U);
                assert(tp_microtap_runtime_pending() && !cursor_trace_count && !submissions);
                cycle_steps(steps); idle(20U);
                assert_cursor_epoch_cleared();
                quiet_until(30U, 200U);
                assert(submissions == 0U && cursor_trace_count == 0U && count == 0U);
                int fresh = minimum_raw_for_tap(expected_gain(selected_index(), slow != 0U));
                poll((int8_t)fresh, 0, 210U); quiet_until(220U, 330U);
                assert(submissions == 1U && !cursor_trace_count); complete();

                /* Naturally queue old motion behind an actual runtime-accepted
                 * click. A new gain epoch cancels that replay and completes only
                 * its own necessary UP; a foreign LEFT reference must survive.
                 */
                presets_fixture(index, slow != 0U, true);
                poll((int8_t)raw, 0, 10U); quiet_until(20U, 130U);
                assert(submissions == 1U && status.owns_left && refs[0] == 1);
                poll(100, 0, 140U);
                assert(count >= 2U && cursor_trace_count == 0U);
                ++refs[0]; /* Independent native owner, not the automatic click. */
                cycle_steps(steps); idle(150U);
                assert(count == 0U && !tp_microtap_runtime_pending() && !status.owns_left);
                assert(refs[0] == 1 && status.up_reports == 1U);
                quiet_until(160U, 300U);
                assert(submissions == 1U && cursor_trace_count == 0U);
            }
        }
    }
    puts("PASS presets actual micro runtime:1/5-cycle epochs cancel old candidates and queued replay, fresh taps rearm, necessary own UP preserves foreign LEFT ownership");
}

static void test_i2c_interleave_keeps_raw_shadow_then_cancels_next_poll(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            for (unsigned steps = 1U; steps <= 5U; steps += 4U) {
                presets_fixture(index, slow != 0U, true);
                int32_t gain = expected_gain(index, slow != 0U);
                int raw = minimum_raw_for_tap(gain);
                poll((int8_t)raw, 0, 10U);
                uint32_t captured = last_cursor_preset_snapshot;
                interleave_steps = steps; presets_after_i2c = cycle_inside_i2c;
                poll(1, 0, 20U);
                assert(last_cursor_preset_snapshot == captured && bbp_cursor_speed_snapshot() != captured);
                assert(gesture.count == 2U && gesture.measure_pointer.gain == (uint16_t)gain);
                assert(gesture.cursor_sum_x == (raw + 1) * gain / 400);
                assert(!cursor_trace_count && !submissions && tp_microtap_runtime_pending());
                idle(30U);
                assert(last_cursor_preset_snapshot == bbp_cursor_speed_snapshot());
                assert_cursor_epoch_cleared();
                quiet_until(40U, 200U);
                assert(!cursor_trace_count && !submissions && count == 0U);
            }
        }
    }
    puts("PASS presets I2C interleave with pending RAW shadow: current frame never mixes gains, next-poll1/5-cycle epoch cancels candidate before old tap/replay");
}
#endif

static void test_frozen_scroll_oracle_at_every_gain(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        presets_fixture(index, false, false);
        test_wheel_unchanged(); /* Unmodified signed8/Ctrl/curve oracle from test_pointer.c. */
        assert(selected_index() == index);
    }
    puts("PASS presets scroll isolation: frozen signed8 xCtrl original wheel oracle and all curve boundaries run verbatim at each of5 cursor gains");
}
static void test_gamepad_handoff_is_unscaled_signed8(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            presets_fixture(index, slow != 0U, false);
            ownership(true);
            for (int raw = -128; raw <= 127; ++raw) {
                int other = -1 - raw; /* Both axes cover their complete signed8 domain. */
                poll((int8_t)raw, (int8_t)other, 10U + (uint32_t)(raw + 128) * 10U);
                assert(game_valid && game_dx == raw && game_dy == other);
                assert(!report_count && !cursor_trace_count);
                assert(!cursor_filter.spike.valid && !cursor_motion.x.active && !cursor_motion.y.active);
                assert(scroll_motion.sample_cnt == 0U && scroll_motion.sum_dx == 0 && scroll_motion.sum_dy == 0);
#if TEST_CURSOR_MICRO
                assert(!submissions && !tp_microtap_runtime_pending());
#endif
            }
            assert(game_samples == 256U);
            ownership(false); poll(1, 0, 2580U); expect_xy(0, 0);
        }
    }
    puts("PASS presets gamepad isolation: all signed8 values on both axes x5 cursor gains xCtrl hand off RAW unchanged; no wheel/cursor/tap and fresh mouse onset on exit");
}

int main(void) {
    test_actual_filter_all_gains_and_half();
    test_actual_epoch_resets_including_five_cycle_roundtrip();
    test_actual_zero_reverse_and_exact_idle_boundary();
    test_i2c_interleave_is_one_coherent_motion_frame();
#if TEST_CURSOR_MICRO
    test_actual_raw_shadow_thresholds_follow_gain();
    test_actual_epoch_cancels_pending_tap_and_queued_replay();
    test_i2c_interleave_keeps_raw_shadow_then_cancels_next_poll();
#endif
    test_frozen_scroll_oracle_at_every_gain();
    test_gamepad_handoff_is_unscaled_signed8();
    assert(fake_flash_writes == 0U && fake_tp_flash_writes == 0U);
    puts("PASS cursor presets actual driver integration; logical report evidence only, not physical/host acceleration acceptance");
    return 0;
}
