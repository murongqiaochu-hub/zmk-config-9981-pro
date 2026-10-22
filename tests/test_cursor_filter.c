/* Pure C11 core tests; no SDK/test_env and no hardware/HID claims.
 * SPDX-License-Identifier: Apache-2.0
 * Example host invocation (execution belongs to the parent runner):
 * cc -std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -pedantic \
 *    -fsanitize=undefined -fno-sanitize-recover=all tests/test_cursor_filter.c -o /tmp/tp-filter
 */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_cursor_filter.h"

#ifdef NDEBUG
#error "cursor filter tests require enabled assertions"
#endif

_Static_assert(CHAR_BIT == 8 && INT8_MIN == -128 && INT8_MAX == 127, "raw sensor domain");
_Static_assert(TP_CURSOR_SPIKE_HISTORY == 4U && TP_CURSOR_SPIKE_PERIOD_MS == 10U &&
               TP_CURSOR_SPIKE_SLOW_SPEED == 2U && TP_CURSOR_SPIKE_CAP_SPEED == 6U &&
               TP_CURSOR_IDLE_MS == 50U, "historical limiter / jitter contract");

static int magnitude(int value) { return value < 0 ? -value : value; }
static int direction(int value) { return (value > 0) - (value < 0); }
static int64_t magnitude64(int64_t value) { return value < 0 ? -value : value; }
static uint32_t random_state = UINT32_C(0x998132);
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static int random_raw(void) { return (int)(next_random() & UINT32_C(255)) - 128; }

static void check_component(int raw, int output) {
    assert(raw >= -128 && raw <= 127);
    assert(output >= -128 && output <= 127);
    if (raw == 0) { assert(output == 0); }
    else if (raw > 0) { assert(output >= 0 && output <= raw); }
    else { assert(output <= 0 && output >= raw); }
}
static void check_state(const struct tp_cursor_filter *state) {
    assert(state->spike.count <= 4U && state->spike.index < 4U);
    for (unsigned i = 0; i < 4U; ++i) { assert(state->spike.speeds[i] <= 1280U); }
    assert(state->jitter.x.remainder >= -1 && state->jitter.x.remainder <= 1);
    assert(state->jitter.y.remainder >= -1 && state->jitter.y.remainder <= 1);
}
static void expect(struct tp_cursor_filter *state, int dx, int dy, uint32_t now,
                   int expected_x, int expected_y) {
    assert(dx >= -128 && dx <= 127 && dy >= -128 && dy <= 127);
    int16_t x, y;
    tp_cursor_filter_apply(state, (int8_t)dx, (int8_t)dy, now, &x, &y);
    assert(x == expected_x && y == expected_y);
    check_component(dx, x); check_component(dy, y); check_state(state);
}

/* Executable compatibility oracle transcribed from the historical
 * 20261011 trackpad_spike.h, with its original int16 component interface.
 * Literal constants intentionally do not depend on the new header.
 */
struct legacy_spike {
    uint16_t speeds[4];
    uint8_t count, index;
    uint32_t last_sample;
    bool valid;
};
static uint32_t legacy_abs(int16_t value) {
    return value < 0 ? (uint32_t)(-(int32_t)value) : (uint32_t)value;
}
static int16_t legacy_component(int16_t value, uint32_t cap, uint32_t norm) {
    uint32_t scaled = (legacy_abs(value) * cap + norm / 2U) / norm;
    return (int16_t)(value < 0 ? -(int32_t)scaled : (int32_t)scaled);
}
static bool legacy_apply(struct legacy_spike *state, int16_t *x, int16_t *y,
                          uint32_t now) {
    uint32_t dt = state->valid ? (uint32_t)(now - state->last_sample) : 10U;
    if (!state->valid || dt == 0U || dt > 50U) {
        *state = (struct legacy_spike){0};
        dt = 10U;
    }
    uint32_t ax = legacy_abs(*x), ay = legacy_abs(*y);
    uint32_t norm = ax > ay ? ax : ay;
    uint32_t speed = (norm * 10U + dt - 1U) / dt;
    bool slow_history = state->count == 4U;
    for (unsigned i = 0; i < state->count; ++i) {
        if (state->speeds[i] > 2U) { slow_history = false; }
    }
    uint32_t cap = (6U * dt + 10U - 1U) / 10U;
    uint32_t half = (norm + 1U) / 2U;
    if (cap < half) { cap = half; }
    bool clipped = slow_history && norm > cap;
    if (clipped) {
        *x = legacy_component(*x, cap, norm);
        *y = legacy_component(*y, cap, norm);
    }
    state->speeds[state->index] = speed > UINT16_MAX ? UINT16_MAX : (uint16_t)speed;
    state->index = (uint8_t)((state->index + 1U) % 4U);
    if (state->count < 4U) { ++state->count; }
    state->last_sample = now;
    state->valid = true;
    return clipped;
}
static struct legacy_spike legacy_copy(const struct tp_cursor_spike *source) {
    struct legacy_spike result = {.count = source->count, .index = source->index,
        .last_sample = source->last_sample, .valid = source->valid};
    for (unsigned i = 0; i < 4U; ++i) { result.speeds[i] = source->speeds[i]; }
    return result;
}
static void check_legacy_state(const struct tp_cursor_spike *actual,
                                const struct legacy_spike *old) {
    assert(actual->count == old->count && actual->index == old->index);
    assert(actual->valid == old->valid && actual->last_sample == old->last_sample);
    for (unsigned i = 0; i < 4U; ++i) { assert(actual->speeds[i] == old->speeds[i]); }
}
static void spike_expect(struct tp_cursor_spike *state, int dx, int dy, uint32_t now,
                         bool clipped, int expected_x, int expected_y) {
    assert(dx >= -128 && dx <= 127 && dy >= -128 && dy <= 127);
    int16_t x, y;
    assert(tp_cursor_spike_apply(state, (int8_t)dx, (int8_t)dy, now, &x, &y) == clipped);
    assert(x == expected_x && y == expected_y);
    check_component(dx, x); check_component(dy, y);
    assert(2 * magnitude(x) >= magnitude(dx) && 2 * magnitude(y) >= magnitude(dy));
}
static void prime_spike(struct tp_cursor_spike *state, int dx, int dy) {
    tp_cursor_spike_reset(state);
    for (unsigned i = 0; i < 4U; ++i) {
        spike_expect(state, dx, dy, 10U + i * 10U, false, dx, dy);
    }
    assert(state->count == 4U && state->index == 0U);
}

static void test_spike_contract(void) {
    struct tp_cursor_spike state = {0};
    for (unsigned i = 0; i < 3U; ++i) {
        spike_expect(&state, 2, 0, 10U + i * 10U, false, 2, 0);
    }
    spike_expect(&state, 120, 0, 40U, false, 120, 0); /* Three lows are insufficient. */
    prime_spike(&state, 2, -2);
    spike_expect(&state, 120, -5, 50U, true, 60, -3);
    assert(state.speeds[0] == 120U); /* Store RAW 120, not limited 60. */
    spike_expect(&state, 120, -5, 60U, false, 120, -5); /* Immediate sustained bypass. */
    prime_spike(&state, 2, 0);
    spike_expect(&state, -120, 5, 50U, true, -60, 3);
    prime_spike(&state, 0, 0);
    spike_expect(&state, 0, -128, 50U, true, 0, -64);
    spike_expect(&state, 0, -128, 60U, false, 0, -128);
    prime_spike(&state, 1, 0);
    spike_expect(&state, 127, 1, 50U, true, 64, 1);
    prime_spike(&state, 1, 0);
    spike_expect(&state, 5, 120, 50U, true, 3, 60);
    prime_spike(&state, 3, 0);
    spike_expect(&state, 120, 0, 50U, false, 120, 0); /* Slow threshold is <=2. */
    const int raw[] = {6, 7, 12, 13};
    const int limited[] = {6, 6, 6, 7};
    for (unsigned i = 0; i < sizeof(raw) / sizeof(raw[0]); ++i) {
        prime_spike(&state, 2, 0);
        spike_expect(&state, raw[i], 0, 50U, i != 0U, limited[i], 0);
    }
    const uint32_t gaps[] = {0U, 1U, 10U, 50U, 51U, UINT32_C(0x7fffffff),
        UINT32_C(0x80000000), UINT32_C(0x80000001), UINT32_MAX};
    for (unsigned i = 0; i < sizeof(gaps) / sizeof(gaps[0]); ++i) {
        prime_spike(&state, 2, 0);
        bool valid_dt = gaps[i] != 0U && gaps[i] <= 50U;
        uint32_t now = 40U + gaps[i];
        spike_expect(&state, 120, 0, now, valid_dt, valid_dt ? 60 : 120, 0);
        if (!valid_dt) { assert(state.count == 1U && state.speeds[0] == 120U); }
        spike_expect(&state, 120, 0, now + 10U, false, 120, 0);
    }
    tp_cursor_spike_reset(&state);
    spike_expect(&state, 0, 0, 0U, false, 0, 0);
    for (unsigned i = 1; i <= 4U; ++i) {
        spike_expect(&state, 10, 0, i * 50U, false, 10, 0);
    }
    for (unsigned i = 0; i < 4U; ++i) { assert(state.speeds[i] == 2U); }
    spike_expect(&state, 120, 0, 250U, true, 60, 0); /* Normalized counts/10ms. */
    assert(state.speeds[(state.index + 3U) % 4U] == 24U);
    puts("PASS spike: 4 slow RAW speeds, 6/10ms cap, half bound, rounding, immediate fast bypass, dt reset");
}

static void test_alternation_and_continuous(void) {
    for (int sign = -1; sign <= 1; sign += 2) {
        struct tp_cursor_filter state = {0};
        for (unsigned i = 0; i < 4096U; ++i) {
            int raw = (i % 2U == 0U) ? sign : -sign;
            expect(&state, raw, -raw, i * 10U, 0, 0);
        }
        expect(&state, 0, 0, 40960U, 0, 0);
    }
    /* Every constant signed-8 pair, including sustained +/-1, +/-2, +127/-128.
     * Neither stage may turn the initial one-count band into recurring loss.
     */
    for (int raw = -128; raw <= 127; ++raw) {
        int other = raw == -128 ? 127 : -raw;
        struct tp_cursor_filter state = {0};
        int64_t sum_x = 0, sum_y = 0;
        for (unsigned i = 0; i < 257U; ++i) {
            int16_t x, y;
            tp_cursor_filter_apply(&state, (int8_t)raw, (int8_t)other, i * 10U, &x, &y);
            assert(x == raw - (i == 0U ? direction(raw) : 0));
            assert(y == other - (i == 0U ? direction(other) : 0));
            sum_x += x; sum_y += y;
            assert(sum_x == (int64_t)(i + 1U) * raw - direction(raw));
            assert(sum_y == (int64_t)(i + 1U) * other - direction(other));
            check_component(raw, x); check_component(other, y); check_state(&state);
        }
    }
    puts("PASS jitter: +/-1 alternation is zero; every sustained signed-8 input loses only its initial count");
}

static void test_reversal_idle_and_reset(void) {
    struct tp_cursor_filter state = {0};
    expect(&state, 1, -1, 100U, 0, 0);
    expect(&state, -1, 1, 110U, 0, 0); /* First cancel the old remainder. */
    assert(state.jitter.x.remainder == 0 && state.jitter.y.remainder == 0);
    expect(&state, -1, 1, 120U, 0, 0); /* Then retain the new direction's one count. */
    expect(&state, -1, 1, 130U, -1, 1);
    expect(&state, 2, -2, 140U, 0, 0);
    expect(&state, 2, -2, 150U, 2, -2);
    int16_t reverse_x, reverse_y;
    tp_cursor_jitter_apply(&state.jitter, -127, 127, 160U, &reverse_x, &reverse_y);
    assert(reverse_x == -125 && reverse_y == 125); /* Isolate jitter from armed spike history. */
    check_component(-127, reverse_x); check_component(127, reverse_y);
    tp_cursor_filter_reset(&state);
    expect(&state, 1, -1, 100U, 0, 0);
    expect(&state, 0, 0, 110U, 0, 0);
    assert(state.jitter.x.remainder == 1 && state.jitter.y.remainder == -1);
    assert(state.jitter.x.last_motion_time == 100U && state.jitter.y.last_motion_time == 100U);
    expect(&state, 0, 0, 150U, 0, 0);
    assert(state.jitter.x.active && state.jitter.x.remainder == 1);
    expect(&state, 0, 0, 151U, 0, 0);
    assert(!state.jitter.x.active && !state.jitter.y.active);
    expect(&state, 1, -1, 160U, 0, 0);
    expect(&state, 1, -1, 210U, 1, -1); /* Exactly 50ms retains. */
    expect(&state, 1, -1, 261U, 0, 0); /* 51ms starts afresh. */
    tp_cursor_filter_reset(&state);
    expect(&state, 1, 1, 100U, 0, 0);
    for (unsigned i = 1; i <= 5U; ++i) { expect(&state, 0, 1, 100U + i * 10U, 0, 1); }
    assert(state.jitter.x.active && state.jitter.x.last_motion_time == 100U);
    expect(&state, 0, 1, 151U, 0, 1);
    assert(!state.jitter.x.active && state.jitter.y.active);
    expect(&state, 1, 1, 160U, 0, 1); /* Y motion cannot keep X's credit alive. */

    tp_cursor_filter_reset(&state);
    for (unsigned i = 0; i < 4U; ++i) { expect(&state, 1, 0, 100U + i * 10U, i == 0U ? 0 : 1, 0); }
    tp_cursor_filter_expire(&state, 180U);
    assert(state.spike.count == 4U && state.spike.last_sample == 130U);
    assert(state.jitter.x.active && state.jitter.x.last_motion_time == 130U);
    tp_cursor_filter_expire(&state, 181U);
    assert(!state.spike.valid && state.spike.count == 0U && !state.jitter.x.active);
    expect(&state, 1, 0, 190U, 0, 0);
    /* Simulate a caller's GPIO/I2C/mode-epoch fault reset, not fake core I/O. */
    tp_cursor_filter_reset(&state);
    tp_cursor_filter_reset(&state); /* Idempotent. */
    assert(!state.spike.valid && state.spike.count == 0U && state.spike.index == 0U);
    assert(!state.jitter.x.active && !state.jitter.y.active);
    assert(state.jitter.x.remainder == 0 && state.jitter.y.remainder == 0);
    expect(&state, 120, -120, 200U, 119, -119); /* No stale spike or old remainder. */
    puts("PASS lifecycle: reversal cancellation, no tail, per-axis 50/51ms idle, explicit fault/epoch reset");
}

static void test_clock_boundaries(void) {
    const uint32_t gaps[] = {0U, 1U, 50U, 51U, UINT32_C(0x7fffffff),
        UINT32_C(0x80000000), UINT32_C(0x80000001), UINT32_MAX};
    for (unsigned i = 0; i < sizeof(gaps) / sizeof(gaps[0]); ++i) {
        struct tp_cursor_filter state = {0};
        for (unsigned n = 0; n < 4U; ++n) {
            expect(&state, 1, -1, 100U + n * 10U, n == 0U ? 0 : 1, n == 0U ? 0 : -1);
        }
        uint32_t now = 130U + gaps[i];
        int value = gaps[i] <= 50U ? 1 : 0;
        expect(&state, 1, -1, now, value, -value);
        if (gaps[i] == 0U || gaps[i] > 50U) { assert(state.spike.count == 1U); }
        expect(&state, 1, -1, now + 10U, 1, -1); /* No half-range long lockout. */
    }
    struct tp_cursor_filter state = {0};
    for (unsigned i = 0; i < 4U; ++i) { expect(&state, 1, 0, 100U + i * 10U, i == 0U ? 0 : 1, 0); }
    expect(&state, 120, 0, 130U, 120, 0); /* dt0: spike passes raw to warm jitter. */
    assert(state.spike.count == 1U && state.spike.speeds[0] == 120U);
    expect(&state, 120, 0, 140U, 120, 0);
    tp_cursor_filter_reset(&state);
    for (unsigned i = 0; i < 1024U; ++i) { expect(&state, 1, -1, 0U, i == 0U ? 0 : 1, i == 0U ? 0 : -1); }
    assert(state.spike.count == 1U); /* Repeated zero timestamp is not an uninitialized sentinel. */
    tp_cursor_filter_reset(&state);
    expect(&state, 1, -1, UINT32_MAX - 20U, 0, 0);
    expect(&state, 1, -1, 9U, 1, -1); /* Natural wrap, elapsed 30ms. */
    tp_cursor_filter_reset(&state);
    expect(&state, 1, -1, UINT32_MAX - 20U, 0, 0);
    expect(&state, 1, -1, 30U, 0, 0); /* Natural wrap, elapsed 51ms. */
    expect(&state, 1, -1, 40U, 1, -1);
    struct tp_cursor_spike spike = {0};
    for (unsigned i = 0; i < 4U; ++i) {
        spike_expect(&spike, 1, 0, UINT32_MAX - 30U + i * 10U, false, 1, 0);
    }
    spike_expect(&spike, 120, 0, 9U, true, 60, 0);
    spike_expect(&spike, 120, 0, 19U, false, 120, 0);
    puts("PASS clocks: dt0, repeated timestamps, uint32 natural wrap, backward/half-range reset and immediate recovery");
}

static void test_stage_switches(void) {
    for (unsigned spike_enabled = 0; spike_enabled <= 1U; ++spike_enabled) {
        for (unsigned jitter_enabled = 0; jitter_enabled <= 1U; ++jitter_enabled) {
            struct tp_cursor_filter state = {0};
            int16_t x, y;
            for (unsigned i = 0; i < 4U; ++i) {
                tp_cursor_filter_apply_stages(&state, 0, 0, 10U + i * 10U,
                    spike_enabled != 0U, jitter_enabled != 0U, &x, &y);
                assert(x == 0 && y == 0);
            }
            tp_cursor_filter_apply_stages(&state, 120, -120, 50U,
                spike_enabled != 0U, jitter_enabled != 0U, &x, &y);
            int expected = spike_enabled != 0U ? 60 : 120;
            if (jitter_enabled != 0U) { --expected; }
            assert(x == expected && y == -expected); /* Spike MUST precede jitter. */
            check_state(&state);
        }
    }
    struct tp_cursor_filter state = {0};
    expect(&state, 1, 0, 10U, 0, 0);
    int16_t x, y;
    tp_cursor_filter_apply_stages(&state, 1, 0, 20U, true, false, &x, &y);
    assert(x == 1 && y == 0 && !state.jitter.x.active && state.spike.count == 2U);
    expect(&state, 1, 0, 30U, 0, 0);
    tp_cursor_filter_apply_stages(&state, 0, 0, 40U, false, true, &x, &y);
    assert(x == 0 && y == 0 && !state.spike.valid && state.jitter.x.remainder == 1);
    expect(&state, 120, 0, 50U, 120, 0); /* Disabled spike did not keep old slow history. */
    tp_cursor_filter_apply_stages(&state, 0, 0, 60U, false, false, &x, &y);
    assert(x == 0 && y == 0 && !state.spike.valid && !state.jitter.x.active);
    expect(&state, 120, 0, 70U, 119, 0);
    puts("PASS independent stage switches: bypass is exact, disabled state resets, spike-before-jitter order");
}

static void test_exhaustive_pairs(void) {
    struct tp_cursor_filter templates[3] = {0};
    for (unsigned i = 0; i < 4U; ++i) {
        int16_t x, y;
        tp_cursor_filter_apply_stages(&templates[1], 0, 0, 10U + i * 10U, true, false, &x, &y);
        tp_cursor_filter_apply_stages(&templates[2], 120, -120, 10U + i * 10U, true, false, &x, &y);
    }
    uint32_t cases = 0U;
    for (int dx = -128; dx <= 127; ++dx) {
        for (int dy = -128; dy <= 127; ++dy) {
            for (unsigned history = 0; history < 3U; ++history) {
                struct tp_cursor_spike stage = templates[history].spike;
                struct legacy_spike old = legacy_copy(&stage);
                int16_t sx, sy, ox = (int16_t)dx, oy = (int16_t)dy;
                bool clipped = tp_cursor_spike_apply(&stage, (int8_t)dx, (int8_t)dy, 50U, &sx, &sy);
                assert(clipped == legacy_apply(&old, &ox, &oy, 50U));
                assert(sx == ox && sy == oy);
                check_legacy_state(&stage, &old);
                check_component(dx, sx); check_component(dy, sy);
                assert(2 * magnitude(sx) >= magnitude(dx) && 2 * magnitude(sy) >= magnitude(dy));
                for (int rx = -1; rx <= 1; ++rx) {
                    for (int ry = -1; ry <= 1; ++ry) {
                        struct tp_cursor_filter state = templates[history];
                        state.jitter.x = (struct tp_cursor_jitter_axis){.remainder = (int8_t)rx,
                            .last_motion_time = 40U, .active = true};
                        state.jitter.y = (struct tp_cursor_jitter_axis){.remainder = (int8_t)ry,
                            .last_motion_time = 40U, .active = true};
                        int ax = rx + sx, ay = ry + sy;
                        int expected_x = ax > 1 ? ax - 1 : ax < -1 ? ax + 1 : 0;
                        int expected_y = ay > 1 ? ay - 1 : ay < -1 ? ay + 1 : 0;
                        int16_t x, y;
                        tp_cursor_filter_apply(&state, (int8_t)dx, (int8_t)dy, 50U, &x, &y);
                        assert(x == expected_x && y == expected_y);
                        assert(state.jitter.x.remainder == ax - expected_x);
                        assert(state.jitter.y.remainder == ay - expected_y);
                        check_component(dx, x); check_component(dy, y); check_state(&state);
                        ++cases;
                    }
                }
            }
        }
    }
    assert(cases == UINT32_C(65536) * 3U * 9U);
    puts("PASS all 65,536 signed-8 pairs x 3 spike histories x 9 residual pairs: exact oracle, int8 bounds, no reversal/new axis");
}

static void test_axis_symmetry(void) {
    struct tp_cursor_filter normal = {0}, mirror = {0}, swapped = {0};
    uint32_t now = UINT32_MAX - 50U;
    random_state = UINT32_C(0x9981abcd);
    for (unsigned i = 0; i < 10000U; ++i) {
        /* +/-127 permits a legal signed-8 sign mirror; -128 is covered above. */
        int dx = (int)(next_random() % 255U) - 127;
        int dy = (int)(next_random() % 255U) - 127;
        if (i % 5U == 0U) { dx = 0; }
        if (i % 7U == 0U) { dy = 0; }
        now += i % 97U == 0U ? 51U : 10U;
        int16_t x, y, mx, my, tx, ty;
        tp_cursor_filter_apply(&normal, (int8_t)dx, (int8_t)dy, now, &x, &y);
        tp_cursor_filter_apply(&mirror, (int8_t)-dx, (int8_t)-dy, now, &mx, &my);
        tp_cursor_filter_apply(&swapped, (int8_t)dy, (int8_t)dx, now, &tx, &ty);
        assert(x == -mx && y == -my && x == ty && y == tx);
        check_component(dx, x); check_component(dy, y);
        check_state(&normal); check_state(&mirror); check_state(&swapped);
    }
    puts("PASS symmetry: sign mirror, X/Y swap, diagonals and zero-axis independence over 10,000 frames");
}

static void test_normal_long_slide(void) {
    for (int sign = -1; sign <= 1; sign += 2) {
        struct tp_cursor_filter state = {0};
        int64_t raw_x = 0, raw_y = 0, output_x = 0, output_y = 0;
        uint32_t now = UINT32_MAX - 500U;
        random_state = UINT32_C(0x99810001);
        for (unsigned i = 0; i < 100000U; ++i) {
            /* No four slow packets: normal varying slide, not a slip-release.
             * Sparse zeros are <50ms per axis and must not spend the remainder.
             */
            int dx = sign * (3 + (int)(next_random() % 125U));
            int dy = -sign * (3 + (int)(next_random() % 125U));
            if (i % 11U == 10U) { dx = 0; }
            if (i % 13U == 12U) { dy = 0; }
            now += 10U;
            int16_t x, y;
            tp_cursor_filter_apply(&state, (int8_t)dx, (int8_t)dy, now, &x, &y);
            raw_x += dx; raw_y += dy; output_x += x; output_y += y;
            assert(raw_x - output_x == sign && raw_y - output_y == -sign);
            assert(state.jitter.x.remainder == sign && state.jitter.y.remainder == -sign);
            check_component(dx, x); check_component(dy, y); check_state(&state);
        }
    }
    puts("PASS normal long slides: 2 x 100,000 frames, exact signed cumulative motion except one initial count/axis");
}

/* Signed displacement, not absolute path length, is conserved within each
 * jitter epoch modulo its one-count remainder. Expiry can discard one more
 * remainder. A direction change can cancel the old one count AND retain the
 * new one, so an honest absolute-path bound is 1 + 2*reversals + expiries.
 * Spike attenuation is accounted separately, never called lossless.
 */
struct ledger {
    int64_t raw, limited, output, expired, jitter_path_loss;
    uint32_t expiries, reversals;
    int previous_sign;
};
static void ledger_prepare(struct ledger *ledger, const struct tp_cursor_jitter_axis *axis,
                            uint32_t now) {
    if (axis->active && (uint32_t)(now - axis->last_motion_time) > 50U) {
        ledger->expired += axis->remainder;
        ++ledger->expiries;
        ledger->previous_sign = 0;
    }
}
static void ledger_record(struct ledger *ledger, const struct tp_cursor_jitter_axis *axis,
                           int raw, int limited, int output) {
    check_component(raw, limited); check_component(limited, output);
    int sign = direction(limited);
    if (sign != 0) {
        if (ledger->previous_sign != 0 && sign != ledger->previous_sign) { ++ledger->reversals; }
        ledger->previous_sign = sign;
    }
    ledger->raw += raw;
    ledger->limited += limited;
    ledger->output += output;
    ledger->jitter_path_loss += magnitude(limited) - magnitude(output);
    assert(ledger->limited - ledger->output == ledger->expired + axis->remainder);
    assert(magnitude64(ledger->limited - ledger->output) <= (int64_t)ledger->expiries + 1);
    assert(ledger->raw - ledger->output ==
           (ledger->raw - ledger->limited) + ledger->expired + axis->remainder);
    assert(ledger->jitter_path_loss >= 0);
    assert(ledger->jitter_path_loss <= 1 + 2 * (int64_t)ledger->reversals + ledger->expiries);
}
static void test_mixed_long_ledger_and_legacy(void) {
    struct tp_cursor_filter state = {0};
    struct legacy_spike old = {0};
    struct ledger x_ledger = {0}, y_ledger = {0};
    uint32_t now = UINT32_MAX - 80U;
    unsigned clipped_frames = 0U;
    random_state = UINT32_C(0x99812026);
    for (unsigned i = 0; i < 200000U; ++i) {
        uint32_t dt = 10U;
        if (i % 7919U == 7918U) { dt = UINT32_C(0x80000000); }
        else if (i % 997U == 996U) { dt = 51U; }
        else if (i % 631U == 630U) { dt = 0U; }
        now += dt;
        unsigned slot = i % 12U;
        int dx, dy;
        if (slot < 4U) { dx = 1; dy = -1; }
        else if (slot == 4U || slot == 5U) { dx = 120; dy = -120; }
        else { dx = random_raw(); dy = random_raw(); }
        if (slot == 8U) { dx = 0; }
        if (slot == 9U) { dy = 0; }
        int16_t limited_x = (int16_t)dx, limited_y = (int16_t)dy;
        if (legacy_apply(&old, &limited_x, &limited_y, now)) { ++clipped_frames; }
        ledger_prepare(&x_ledger, &state.jitter.x, now);
        ledger_prepare(&y_ledger, &state.jitter.y, now);
        int16_t x, y;
        tp_cursor_filter_apply(&state, (int8_t)dx, (int8_t)dy, now, &x, &y);
        check_legacy_state(&state.spike, &old);
        ledger_record(&x_ledger, &state.jitter.x, dx, limited_x, x);
        ledger_record(&y_ledger, &state.jitter.y, dy, limited_y, y);
        check_state(&state);
    }
    assert(clipped_frames > 1000U && x_ledger.expiries > 0U && y_ledger.expiries > 0U);
    assert(x_ledger.reversals > 1000U && y_ledger.reversals > 1000U);
    puts("PASS mixed 200,000-frame legacy replay: RAW spike history, signed residual/expiry ledger, bounded reversal path loss");
}

int main(void) {
    test_spike_contract();
    test_alternation_and_continuous();
    test_reversal_idle_and_reset();
    test_clock_boundaries();
    test_stage_switches();
    test_exhaustive_pairs();
    test_axis_symmetry();
    test_normal_long_slide();
    test_mixed_long_ledger_and_legacy();
    puts("PASS cursor filter pure C11 contract (host simulation only; not hardware/BLE acceptance)");
    return 0;
}
