/* SPDX-License-Identifier: MIT
 * Actual feature-ON service + pure pointer helper, every signed8 X/Y pair.
 * No frozen fixture/oracle edits and no shadow implementation of the service.
 */
#define CONFIG_BBP9981_CURSOR_SPEED 1
#define CONFIG_SETTINGS 0
#include "test_env.h"

/* Only the kernel declarations missing from the read-only legacy test_env. */
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
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_pointer.h"

static const uint16_t expected_gains[5] = {240U, 360U, 480U, 600U, 720U};
static void select_index(uint8_t target) {
    assert(target < 5U);
    for (unsigned step = 0U; step < 5U; ++step) {
        if (bbp_cursor_speed_snapshot_index(bbp_cursor_speed_snapshot()) == target) {
            assert(bbp_cursor_speed_begin_motion_frame() == bbp_cursor_speed_snapshot());
            return;
        }
        assert(bbp_cursor_speed_cycle() == 0);
        assert(bbp_cursor_speed_begin_motion_frame() == bbp_cursor_speed_snapshot());
    }
    assert(bbp_cursor_speed_snapshot_index(bbp_cursor_speed_snapshot()) == target);
    assert(bbp_cursor_speed_begin_motion_frame() == bbp_cursor_speed_snapshot());
}

static void test_all_signed8_xy_exact_fractions(void) {
    assert(bbp_cursor_speed_snapshot_index(bbp_cursor_speed_snapshot()) == 2U);
    assert(bbp_cursor_speed_gain(false) == 480U && bbp_cursor_speed_gain(true) == 240U);
    assert(TP_POINTER_DENOMINATOR == 400 && BBP_CURSOR_SPEED_DENOMINATOR == 400U);
    for (uint8_t index = 0U; index < 5U; ++index) {
        select_index(index);
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            const int64_t gain = (int64_t)expected_gains[index] / (slow != 0U ? 2 : 1);
            assert(bbp_cursor_speed_gain(slow != 0U) == (uint16_t)gain);
            for (int raw_x = -128; raw_x <= 127; ++raw_x) {
                for (int raw_y = -128; raw_y <= 127; ++raw_y) {
                    struct tp_pointer_state state = {0};
                    int64_t sum_x = 0, sum_y = 0;
                    for (unsigned frame = 1U; frame <= 16U; ++frame) {
                        int16_t x, y;
                        tp_pointer_motion(&state, (int8_t)raw_x, (int8_t)raw_y,
                                          slow != 0U, frame * 10U, &x, &y);
                        sum_x += x; sum_y += y;
                        const int64_t exact_x = (int64_t)raw_x * gain * frame;
                        const int64_t exact_y = (int64_t)raw_y * gain * frame;
                        assert(sum_x == exact_x / 400 && sum_y == exact_y / 400);
                        assert(state.x.remainder == exact_x - sum_x * 400);
                        assert(state.y.remainder == exact_y - sum_y * 400);
                        assert(state.x.remainder > -400 && state.x.remainder < 400);
                        assert(state.y.remainder > -400 && state.y.remainder < 400);
                        assert(x >= -231 && x <= 229 && y >= -231 && y <= 229);
                        if (raw_x == 0) { assert(x == 0 && !state.x.active); }
                        if (raw_y == 0) { assert(y == 0 && !state.y.active); }
                        assert(state.gain_valid && state.gain == (uint16_t)gain);
                    }
                }
            }
        }
    }
    puts("PASS cursor presets pure pointer: all 65536 signed8 XY pairs x5 gains xCtrl x16 frames, exact quotient/remainder and bounded int16 output");
}

static void test_zero_reverse_idle_and_wrap(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        select_index(index);
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            const int32_t gain = (int32_t)expected_gains[index] / (slow != 0U ? 2 : 1);
            struct tp_pointer_state state = {0};
            int16_t x, y;
            tp_pointer_motion(&state, 1, -1, slow != 0U, 1000U, &x, &y);
            assert(x == gain / 400 && y == -gain / 400);
            const int32_t remainder = gain % 400;
            for (uint32_t now = 1001U; now <= 1050U; ++now) {
                tp_pointer_motion(&state, 0, 0, slow != 0U, now, &x, &y);
                assert(x == 0 && y == 0);
                assert(state.x.remainder == remainder && state.y.remainder == -remainder);
                assert(state.x.last_motion_time == 1000U && state.y.last_motion_time == 1000U);
            }
            tp_pointer_motion(&state, 1, -1, slow != 0U, 1050U, &x, &y);
            assert(x == (gain + remainder) / 400 && y == -(gain + remainder) / 400);

            tp_pointer_reset(&state);
            tp_pointer_motion(&state, 1, -1, slow != 0U, 1000U, &x, &y);
            tp_pointer_motion(&state, 0, 0, slow != 0U, 1051U, &x, &y);
            assert(x == 0 && y == 0 && !state.x.active && !state.y.active);
            assert(state.x.remainder == 0 && state.y.remainder == 0);
            tp_pointer_motion(&state, 1, -1, slow != 0U, 1051U, &x, &y);
            assert(x == gain / 400 && y == -gain / 400);

            tp_pointer_reset(&state);
            tp_pointer_motion(&state, 1, -1, slow != 0U, 1000U, &x, &y);
            tp_pointer_motion(&state, -1, 1, slow != 0U, 1010U, &x, &y);
            assert(x == -gain / 400 && y == gain / 400);
            assert(state.x.remainder == -remainder && state.y.remainder == remainder);
            tp_pointer_motion(&state, -1, 1, slow != 0U, 1020U, &x, &y);
            assert(x == (-gain - remainder) / 400 && y == (gain + remainder) / 400);
            assert(x <= 0 && y >= 0); /* Never spend an old-direction remainder. */

            /* X expires independently while Y continues to conserve its fraction. */
            tp_pointer_reset(&state);
            int64_t sum_y = 0;
            tp_pointer_motion(&state, 1, 1, slow != 0U, 1000U, &x, &y); sum_y += y;
            for (uint32_t step = 1U; step <= 6U; ++step) {
                tp_pointer_motion(&state, 0, 1, slow != 0U, 1000U + step * 10U, &x, &y);
                sum_y += y; assert(x == 0);
            }
            assert(!state.x.active && state.y.active);
            assert(sum_y == (int64_t)gain * 7 / 400);
            tp_pointer_motion(&state, 1, 1, slow != 0U, 1070U, &x, &y); sum_y += y;
            assert(x == gain / 400 && state.x.remainder == remainder);
            assert(sum_y == (int64_t)gain * 8 / 400);

            const uint32_t anchor = UINT32_MAX - 20U;
            for (unsigned gap = 50U; gap <= 51U; ++gap) {
                tp_pointer_reset(&state);
                tp_pointer_motion(&state, 1, -1, slow != 0U, anchor, &x, &y);
                tp_pointer_motion(&state, 1, -1, slow != 0U, anchor + gap, &x, &y);
                int32_t expected = (gain + (gap == 50U ? remainder : 0)) / 400;
                assert(x == expected && y == -expected);
            }
        }
    }
    puts("PASS cursor presets pure pointer: zero/no tail, reversal, per-axis exact50/51ms expiry and uint32 rollover at every gain/Ctrl");
}

static void test_gain_transitions_and_roundtrip_identity(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            select_index(index);
            struct tp_pointer_state state = {0};
            int16_t x, y;
            tp_pointer_motion(&state, 1, -1, slow != 0U, 1000U, &x, &y);
            uint32_t before = bbp_cursor_speed_snapshot();
            uint16_t old_gain = state.gain;
            for (unsigned n = 0U; n < 5U; ++n) { assert(bbp_cursor_speed_cycle() == 0); }
            uint32_t after = bbp_cursor_speed_snapshot();
            assert(after != before && bbp_cursor_speed_snapshot_index(after) == index);
            assert(bbp_cursor_speed_snapshot_generation(after) ==
                   bbp_cursor_speed_snapshot_generation(before) + 5U);
            assert(bbp_cursor_speed_gain(slow != 0U) == old_gain);
            /* A gain-only helper cannot see that roundtrip. The actual A320
             * epoch/reset obligation is tested in test_cursor_presets_driver.c.
             */
            assert(bbp_cursor_speed_cycle() == 0);
            assert(bbp_cursor_speed_begin_motion_frame() == bbp_cursor_speed_snapshot());
            tp_pointer_prepare(&state, slow != 0U);
            assert(state.gain_valid && state.gain == bbp_cursor_speed_gain(slow != 0U));
            assert(state.gain != old_gain && state.x.remainder == 0 && state.y.remainder == 0);
            assert(!state.x.active && !state.y.active);
            tp_pointer_motion(&state, 1, -1, slow != 0U, 1010U, &x, &y);
            assert(x == state.gain / 400 && y == -(int32_t)state.gain / 400);
            tp_pointer_prepare(&state, slow == 0U); /* Ctrl transition also clears. */
            assert(state.x.remainder == 0 && state.y.remainder == 0);
            assert(!state.x.active && !state.y.active);
        }
    }
    puts("PASS cursor presets pure pointer: every gain/Ctrl transition resets fractions; 5-cycle same-gain snapshot remains a distinct caller epoch");
}

static void test_lights_are_not_gain_inputs(void) {
    for (uint8_t index = 0U; index < 5U; ++index) {
        select_index(index);
        for (unsigned slow = 0U; slow < 2U; ++slow) {
            struct tp_pointer_state state = {0};
            int64_t sum_x = 0, sum_y = 0;
            int64_t gain = (int64_t)expected_gains[index] / (slow != 0U ? 2 : 1);
            for (unsigned brightness = 0U; brightness <= 255U; ++brightness) {
                fake_brightness = (uint8_t)brightness;
                fake_tp_brightness = (uint8_t)(255U - brightness);
                int16_t x, y;
                tp_pointer_motion(&state, 1, -1, slow != 0U, 1000U + brightness * 10U, &x, &y);
                sum_x += x; sum_y += y;
                assert(sum_x == (int64_t)(brightness + 1U) * gain / 400 && sum_y == -sum_x);
            }
        }
    }
    assert(fake_flash_writes == 0U && fake_tp_flash_writes == 0U);
    puts("PASS cursor presets pure pointer: both light values0..255 do not alter gain/fraction or write settings");
}

int main(void) {
    test_all_signed8_xy_exact_fractions();
    test_zero_reverse_idle_and_wrap();
    test_gain_transitions_and_roundtrip_identity();
    test_lights_are_not_gain_inputs();
    return 0;
}
