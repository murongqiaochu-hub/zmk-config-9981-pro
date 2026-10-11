/* SPDX-License-Identifier: Apache-2.0
 * Host-only synthetic replay of the pure FSM, NOT a hardware/contact/HID test.
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined
 *           -fno-sanitize-recover=all tests/test_tap.c -o <approved-output>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_tap.h"

static unsigned long checks, steps;
static unsigned groups;
#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

struct fixture {
    struct tp_tap_fsm fsm;
    struct tp_tap_config config;
    unsigned clicks;
    struct tp_tap_result out;
};

static void init(struct fixture *f) {
    *f = (struct fixture){0};
    tp_tap_reset(&f->fsm);
    f->config = tp_tap_default_config();
    f->config.enabled = true;
}

static struct tp_tap_result full(struct fixture *f, uint32_t t,
                                enum tp_tap_contact contact, int32_t dx, int32_t dy,
                                bool valid, bool suppress) {
    ++steps;
    f->out = tp_tap_step(&f->fsm, &f->config, (struct tp_tap_input){
        .t_ms = t, .valid = valid, .contact = contact,
        .dx = dx, .dy = dy, .suppress = suppress,
    });
    f->clicks += f->out.click_ready ? 1U : 0U;
    CHECK(!f->out.click_ready || (f->out.state == TP_TAP_ARMED &&
                                  f->out.reason == TP_TAP_REASON_READY));
    return f->out;
}

static struct tp_tap_result packet(struct fixture *f, uint32_t t,
                                  enum tp_tap_contact contact, int32_t dx, int32_t dy) {
    return full(f, t, contact, dx, dy, true, false);
}

static void expect(struct fixture *f, uint32_t t, enum tp_tap_contact contact,
                   int32_t dx, int32_t dy, enum tp_tap_state state,
                   enum tp_tap_reason reason, bool ready) {
    struct tp_tap_result out = packet(f, t, contact, dx, dy);
    if (out.state != state || out.reason != reason || out.click_ready != ready) {
        fprintf(stderr, "t=%lu actual=%s/%s/%d expected=%s/%s/%d\n",
                (unsigned long)t, tp_tap_state_name(out.state),
                tp_tap_reason_name(out.reason), out.click_ready,
                tp_tap_state_name(state), tp_tap_reason_name(reason), ready);
    }
    CHECK(out.state == state);
    CHECK(out.reason == reason);
    CHECK(out.click_ready == ready);
}

static void arm(struct fixture *f, uint32_t base) {
    expect(f, base, TP_TAP_UP, 0, 0, TP_TAP_WAIT_UP, TP_TAP_REASON_NEED_UP, false);
    CHECK(f->out.up_samples == 1U);
    expect(f, base + 20U, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
}

static void begin(struct fixture *f, uint32_t base) {
    expect(f, base, TP_TAP_DOWN, 0, 0, TP_TAP_DOWN_DEBOUNCE,
           TP_TAP_REASON_DOWN_PENDING, false);
    expect(f, base + 20U, TP_TAP_DOWN, 0, 0, TP_TAP_CONTACT,
           TP_TAP_REASON_DOWN_CONFIRMED, false);
}

static void release(struct fixture *f, uint32_t base, uint32_t duration) {
    expect(f, base, TP_TAP_UP, 0, 0, TP_TAP_UP_DEBOUNCE,
           TP_TAP_REASON_UP_PENDING, false);
    CHECK(f->out.touch_duration_ms == duration);
    expect(f, base + 20U, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_READY, true);
    CHECK(f->out.touch_duration_ms == duration);
}

static void test_defaults_and_unknown(void) {
    struct fixture f;
    init(&f);
    struct tp_tap_config defaults = tp_tap_default_config();
    CHECK(!defaults.enabled && defaults.max_duration_ms == 200U);
    CHECK(defaults.max_displacement_counts == 12U && defaults.min_samples == 2U);
    CHECK(defaults.down_debounce_ms == 20U && defaults.up_debounce_ms == 20U);
    CHECK(defaults.max_gap_ms == 100U && tp_tap_config_valid(&defaults));
    CHECK(f.fsm.state == TP_TAP_WAIT_UP && f.fsm.reason == TP_TAP_REASON_RESET);
    CHECK(!tp_tap_snapshot(&f.fsm, false).click_ready);
    struct tp_tap_result out = tp_tap_step(&f.fsm, &defaults, (struct tp_tap_input){0});
    CHECK(out.state == TP_TAP_DISABLED && !out.click_ready);
    out = tp_tap_step(&f.fsm, &f.config, (struct tp_tap_input){0});
    CHECK(out.state == TP_TAP_CANCEL_WAIT_UP && out.reason == TP_TAP_REASON_INVALID);
    CHECK(!out.click_ready && out.up_samples == 0U);
    full(&f, 1, TP_TAP_UNKNOWN, 0, 0, true, false);
    CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.up_samples == 0U);
    full(&f, 2, (enum tp_tap_contact)2, 0, 0, true, false);
    CHECK(f.out.reason == TP_TAP_REASON_INVALID && f.out.up_samples == 0U);
    packet(&f, 10, TP_TAP_UP, 0, 0);
    CHECK(f.out.up_samples == 1U && f.out.state == TP_TAP_CANCEL_WAIT_UP);
    packet(&f, 15, TP_TAP_DOWN, 0, 0);
    CHECK(f.out.up_samples == 0U);
    packet(&f, 20, TP_TAP_UP, 0, 0);
    expect(&f, 40, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_valid_and_repeated_taps(void) {
    struct fixture f;
    init(&f); arm(&f, 0);
    expect(&f, 40, TP_TAP_DOWN, 3, 4, TP_TAP_DOWN_DEBOUNCE,
           TP_TAP_REASON_DOWN_PENDING, false);
    CHECK(f.out.max_displacement_sq == 25U);
    expect(&f, 60, TP_TAP_DOWN, 1, -1, TP_TAP_CONTACT,
           TP_TAP_REASON_DOWN_CONFIRMED, false);
    CHECK(f.out.sum_x == 4 && f.out.sum_y == 3 && f.out.max_displacement_sq == 25U);
    packet(&f, 80, TP_TAP_UP, 0, 0);
    CHECK(!f.out.click_ready && f.out.touch_duration_ms == 40U);
    packet(&f, 99, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_UP_DEBOUNCE && !f.out.click_ready);
    expect(&f, 100, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_READY, true);
    expect(&f, 101, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    begin(&f, 120); release(&f, 160, 40);
    expect(&f, 190, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 2U);
    ++groups;
}

static void test_boot_finger_and_up_bounce_gate(void) {
    struct fixture f;
    init(&f);
    expect(&f, 0, TP_TAP_DOWN, 0, 0, TP_TAP_WAIT_UP, TP_TAP_REASON_NEED_UP, false);
    packet(&f, 20, TP_TAP_DOWN, 0, 0);
    packet(&f, 100, TP_TAP_DOWN, 0, 0);
    CHECK(f.out.state == TP_TAP_WAIT_UP && f.clicks == 0U);
    packet(&f, 120, TP_TAP_UP, 0, 0);
    packet(&f, 130, TP_TAP_DOWN, 0, 0);
    CHECK(f.out.up_samples == 0U);
    packet(&f, 140, TP_TAP_UP, 0, 0);
    expect(&f, 160, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    begin(&f, 180); release(&f, 220, 40);
    CHECK(f.clicks == 1U);
    ++groups;
}

static void test_reset_in_every_candidate_phase(void) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        struct fixture f;
        init(&f); arm(&f, 0);
        packet(&f, 40, TP_TAP_DOWN, 0, 0);
        if (phase >= 1U) packet(&f, 60, TP_TAP_DOWN, 0, 0);
        if (phase >= 2U) packet(&f, 80, TP_TAP_UP, 0, 0);
        tp_tap_reset(&f.fsm);
        CHECK(f.fsm.state == TP_TAP_WAIT_UP && !f.fsm.have_time);
        expect(&f, 90, TP_TAP_DOWN, 0, 0, TP_TAP_WAIT_UP, TP_TAP_REASON_NEED_UP, false);
        packet(&f, 110, TP_TAP_UP, 0, 0);
        expect(&f, 130, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
        CHECK(f.clicks == 0U);
        begin(&f, 150); release(&f, 190, 40);
        CHECK(f.clicks == 1U);
    }
    /* A reset clock may restart at zero, but still needs stable UP. */
    struct fixture f;
    init(&f); arm(&f, UINT32_C(100000)); begin(&f, UINT32_C(100040));
    tp_tap_reset(&f.fsm); arm(&f, 0);
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_short_touch_and_no_rebuild(void) {
    struct fixture f;
    init(&f); arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, 2, 0);
    expect(&f, 45, TP_TAP_UP, 0, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_SHORT_TOUCH, false);
    CHECK(f.out.touch_duration_ms == 5U && f.out.up_samples == 1U);
    expect(&f, 50, TP_TAP_DOWN, -2, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_SHORT_TOUCH, false);
    packet(&f, 70, TP_TAP_DOWN, 0, 0);
    CHECK(f.fsm.first_down_ms == 40U && f.out.sum_x == 2);
    packet(&f, 90, TP_TAP_UP, 0, 0);
    packet(&f, 109, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP);
    packet(&f, 110, TP_TAP_DOWN, 0, 0);
    packet(&f, 120, TP_TAP_UP, 0, 0);
    expect(&f, 140, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);
    begin(&f, 160); release(&f, 200, 40);
    CHECK(f.clicks == 1U);

    init(&f); arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, 0, 0);
    expect(&f, 60, TP_TAP_UP, 0, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_SHORT_TOUCH, false); /* 20ms but only ONE DOWN sample. */
    packet(&f, 80, TP_TAP_UP, 0, 0);
    packet(&f, 100, TP_TAP_DOWN, 0, 0);
    packet(&f, 105, TP_TAP_DOWN, 0, 0);
    expect(&f, 110, TP_TAP_UP, 0, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_SHORT_TOUCH, false); /* Two samples but only 10ms. */
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_up_confirmation_bounce_preserves_candidate(void) {
    struct fixture f;
    init(&f); arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, 4, 0);
    packet(&f, 60, TP_TAP_DOWN, 3, 0);
    packet(&f, 80, TP_TAP_UP, 0, 0);
    CHECK(f.fsm.first_up_ms == 80U && f.out.touch_duration_ms == 40U);
    expect(&f, 90, TP_TAP_DOWN, 1, 0, TP_TAP_CONTACT, TP_TAP_REASON_UP_BOUNCE, false);
    CHECK(f.fsm.first_down_ms == 40U && f.out.touch_duration_ms == 50U);
    CHECK(f.out.sum_x == 8 && f.out.max_displacement_sq == 64U);
    CHECK(f.fsm.down_confirmed && f.out.up_samples == 0U);
    packet(&f, 100, TP_TAP_UP, 0, 0);
    CHECK(f.fsm.first_up_ms == 100U && f.out.touch_duration_ms == 60U);
    packet(&f, 110, TP_TAP_UP, 0, 0);
    CHECK(!f.out.click_ready && f.out.touch_duration_ms == 60U);
    expect(&f, 120, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_READY, true);
    CHECK(f.clicks == 1U && f.out.touch_duration_ms == 60U);
    ++groups;
}

static void test_bounce_does_not_restart_timeout_or_motion(void) {
    struct fixture f;
    init(&f); arm(&f, 0); begin(&f, 40);
    packet(&f, 140, TP_TAP_DOWN, 0, 0);
    packet(&f, 160, TP_TAP_DOWN, 0, 0);
    packet(&f, 240, TP_TAP_UP, 0, 0);
    CHECK(f.out.touch_duration_ms == 200U && !f.out.click_ready);
    expect(&f, 250, TP_TAP_DOWN, 0, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_DURATION, false);
    CHECK(f.fsm.first_down_ms == 40U && f.out.touch_duration_ms == 210U);
    packet(&f, 260, TP_TAP_UP, 0, 0);
    packet(&f, 270, TP_TAP_DOWN, 0, 0);
    packet(&f, 280, TP_TAP_UP, 0, 0);
    expect(&f, 300, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);

    init(&f); arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, 8, 0);
    packet(&f, 60, TP_TAP_DOWN, 0, 0);
    packet(&f, 80, TP_TAP_UP, 0, 0);
    expect(&f, 90, TP_TAP_DOWN, 5, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_DISPLACEMENT, false);
    CHECK(f.out.sum_x == 13 && f.out.max_displacement_sq == 169U);
    packet(&f, 100, TP_TAP_DOWN, -13, 0);
    CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.sum_x == 13);
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_duration_boundaries_and_frozen_confirmation(void) {
    /* Every accepted/rejected duration from 21..221ms (201 independent replays). */
    for (uint32_t duration = 21; duration <= 221; ++duration) {
        struct fixture f;
        init(&f); arm(&f, 0); begin(&f, 40);
        uint32_t end = 40U + duration;
        for (uint32_t t = 140; t < end; t += 80U) {
            packet(&f, t, TP_TAP_DOWN, 0, 0);
            CHECK(!f.out.click_ready);
        }
        packet(&f, end, TP_TAP_UP, 0, 0);
        CHECK(f.out.touch_duration_ms == duration);
        CHECK(f.out.state == (duration <= 200U ? TP_TAP_UP_DEBOUNCE
                                              : TP_TAP_CANCEL_WAIT_UP));
        packet(&f, end + 20U, TP_TAP_UP, 0, 0);
        CHECK(f.clicks == (duration <= 200U ? 1U : 0U));
        CHECK(f.out.touch_duration_ms == duration);
    }
    struct fixture f;
    init(&f); f.config.up_debounce_ms = 300U;
    packet(&f, 0, TP_TAP_UP, 0, 0);
    packet(&f, 100, TP_TAP_UP, 0, 0);
    packet(&f, 200, TP_TAP_UP, 0, 0);
    expect(&f, 300, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    begin(&f, 320);
    packet(&f, 420, TP_TAP_DOWN, 0, 0);
    packet(&f, 500, TP_TAP_DOWN, 0, 0);
    packet(&f, 520, TP_TAP_UP, 0, 0);
    packet(&f, 620, TP_TAP_UP, 0, 0);
    packet(&f, 720, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_UP_DEBOUNCE && f.out.touch_duration_ms == 200U);
    expect(&f, 820, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_READY, true);
    CHECK(f.out.touch_duration_ms == 200U && f.clicks == 1U);
    ++groups;
}

static void test_euclidean_radius_matrix(void) {
    /* 33x33=1089 origins: axes, diagonals, signs, exact boundary, first delta. */
    for (int32_t x = -16; x <= 16; ++x) for (int32_t y = -16; y <= 16; ++y) {
        struct fixture f;
        init(&f); arm(&f, 0);
        uint64_t sq = (uint64_t)((int64_t)x * x + (int64_t)y * y);
        packet(&f, 40, TP_TAP_DOWN, x, y);
        CHECK(f.out.max_displacement_sq == sq);
        if (sq <= 144U) {
            CHECK(f.out.state == TP_TAP_DOWN_DEBOUNCE);
            packet(&f, 60, TP_TAP_DOWN, -x, -y);
            CHECK(f.out.sum_x == 0 && f.out.sum_y == 0);
            CHECK(f.out.max_displacement_sq == sq);
            release(&f, 80, 40);
            CHECK(f.clicks == 1U);
        } else {
            CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP);
            CHECK(f.out.reason == TP_TAP_REASON_DISPLACEMENT);
            packet(&f, 60, TP_TAP_DOWN, -x, -y);
            CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.max_displacement_sq == sq);
            packet(&f, 80, TP_TAP_UP, 0, 0);
            expect(&f, 100, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
            CHECK(f.clicks == 0U);
        }
    }
    ++groups;
}

static void test_slide_out_return_and_long_hold(void) {
    struct fixture f;
    init(&f); arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, 7, 0);
    expect(&f, 45, TP_TAP_DOWN, 6, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_DISPLACEMENT, false); /* Before DOWN debounce completes. */
    packet(&f, 60, TP_TAP_DOWN, -13, 0);
    packet(&f, 80, TP_TAP_DOWN, 0, 0);
    CHECK(f.out.sum_x == 13 && f.fsm.first_down_ms == 40U && !f.fsm.down_confirmed);
    packet(&f, 100, TP_TAP_UP, 0, 0);
    expect(&f, 120, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);

    init(&f); arm(&f, 0); begin(&f, 40);
    packet(&f, 80, TP_TAP_DOWN, 6, 0);
    expect(&f, 100, TP_TAP_DOWN, 7, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_DISPLACEMENT, false); /* Ordinary cumulative slide. */
    packet(&f, 120, TP_TAP_DOWN, -13, 0);
    packet(&f, 140, TP_TAP_DOWN, 0, 0);
    CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.clicks == 0U);

    init(&f); arm(&f, 0); begin(&f, 40);
    packet(&f, 140, TP_TAP_DOWN, 0, 0);
    packet(&f, 240, TP_TAP_DOWN, 0, 0);
    CHECK(f.out.state == TP_TAP_CONTACT && f.out.touch_duration_ms == 200U);
    expect(&f, 241, TP_TAP_DOWN, 0, 0, TP_TAP_CANCEL_WAIT_UP, TP_TAP_REASON_DURATION, false);
    packet(&f, 260, TP_TAP_DOWN, 0, 0);
    packet(&f, 300, TP_TAP_DOWN, 0, 0);
    packet(&f, 400, TP_TAP_DOWN, 0, 0);
    packet(&f, 420, TP_TAP_UP, 0, 0);
    expect(&f, 440, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_motion_on_release_and_confirmation(void) {
    struct fixture f;
    init(&f); arm(&f, 0); begin(&f, 40);
    expect(&f, 80, TP_TAP_UP, 13, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_DISPLACEMENT, false);
    CHECK(f.out.up_samples == 1U);
    expect(&f, 100, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);
    init(&f); arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, 11, 0);
    packet(&f, 60, TP_TAP_DOWN, 0, 0);
    packet(&f, 80, TP_TAP_UP, 0, 0);
    expect(&f, 100, TP_TAP_UP, 2, 0, TP_TAP_CANCEL_WAIT_UP,
           TP_TAP_REASON_DISPLACEMENT, false);
    CHECK(f.out.touch_duration_ms == 40U && f.out.max_displacement_sq == 169U);
    packet(&f, 120, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_ARMED && f.clicks == 0U);
    ++groups;
}

/* Return a fault timestamp in ARMED, DOWN debounce, CONTACT, or UP debounce. */
static uint32_t prepare_phase(struct fixture *f, unsigned phase) {
    init(f); arm(f, 0);
    if (phase >= 1U) packet(f, 40, TP_TAP_DOWN, 0, 0);
    if (phase >= 2U) packet(f, 60, TP_TAP_DOWN, 0, 0);
    if (phase >= 3U) packet(f, 80, TP_TAP_UP, 0, 0);
    return 30U + phase * 20U;
}

static void test_invalid_in_all_phases_and_sticky_reason(void) {
    for (unsigned phase = 0; phase < 4; ++phase) {
        struct fixture f;
        uint32_t t = prepare_phase(&f, phase);
        full(&f, t, TP_TAP_UP, 0, 0, false, false); /* Error is NOT release. */
        CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.reason == TP_TAP_REASON_INVALID);
        CHECK(f.out.up_samples == 0U && !f.out.click_ready);
        packet(&f, t + 10U, TP_TAP_DOWN, 0, 0);
        CHECK(f.out.reason == TP_TAP_REASON_INVALID);
        packet(&f, t + 30U, TP_TAP_UP, 0, 0);
        expect(&f, t + 50U, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
        CHECK(f.clicks == 0U);
    }
    struct fixture f;
    init(&f);
    packet(&f, 0, TP_TAP_UP, 0, 0);
    full(&f, 10, TP_TAP_UP, 0, 0, false, false);
    packet(&f, 20, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.up_samples == 1U);
    full(&f, 30, TP_TAP_UNKNOWN, 0, 0, true, false);
    CHECK(f.out.up_samples == 0U);
    full(&f, 40, TP_TAP_UP, 0, 0, true, true);
    CHECK(f.out.reason == TP_TAP_REASON_INVALID); /* First cancellation retained. */
    packet(&f, 200, TP_TAP_UP, 0, 0);
    CHECK(f.out.reason == TP_TAP_REASON_INVALID && f.out.up_samples == 0U);
    packet(&f, 220, TP_TAP_UP, 0, 0);
    expect(&f, 240, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_gap_limit_and_missing_frames(void) {
    struct fixture f;
    init(&f); arm(&f, 0); begin(&f, 40);
    expect(&f, 161, TP_TAP_UP, 0, 0, TP_TAP_CANCEL_WAIT_UP, TP_TAP_REASON_GAP, false);
    CHECK(f.out.up_samples == 0U); /* Fault packet cannot begin stable UP. */
    packet(&f, 180, TP_TAP_DOWN, 0, 0);
    packet(&f, 200, TP_TAP_UP, 0, 0);
    expect(&f, 220, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);
    init(&f); arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, 0, 0);
    expect(&f, 140, TP_TAP_DOWN, 0, 0, TP_TAP_CONTACT, TP_TAP_REASON_DOWN_CONFIRMED, false);
    release(&f, 160, 120); /* Exactly 100ms gap is allowed. */
    CHECK(f.clicks == 1U);
    init(&f); arm(&f, 0); begin(&f, 40);
    full(&f, 70, TP_TAP_UNKNOWN, 0, 0, false, false); /* Explicit no-packet tick. */
    packet(&f, 90, TP_TAP_DOWN, 0, 0);
    CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.reason == TP_TAP_REASON_INVALID);
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_stale_backward_and_half_range(void) {
    const uint32_t times[] = {60U, 50U, 60U + UINT32_C(0x7fffffff),
                             60U + UINT32_C(0x80000000), 59U};
    for (unsigned i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        struct fixture f;
        init(&f); arm(&f, 0); begin(&f, 40);
        packet(&f, times[i], TP_TAP_UP, 0, 0);
        CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && !f.out.click_ready);
        CHECK(f.out.reason == (i == 2U ? TP_TAP_REASON_GAP : TP_TAP_REASON_STALE_TIME));
        CHECK(f.out.up_samples == 0U && f.clicks == 0U);
        if (i != 2U) {
            packet(&f, 70, TP_TAP_UP, 0, 0);
            expect(&f, 90, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
        }
    }
    struct fixture f;
    init(&f);
    packet(&f, 0, TP_TAP_UP, 0, 0);
    expect(&f, 0, TP_TAP_UP, 0, 0, TP_TAP_CANCEL_WAIT_UP, TP_TAP_REASON_STALE_TIME, false);
    CHECK(f.out.up_samples == 0U);
    packet(&f, 20, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.up_samples == 1U);
    expect(&f, 40, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    ++groups;
}

static void test_rollover(void) {
    struct fixture f;
    uint32_t base = UINT32_MAX - 50U;
    init(&f); arm(&f, base);
    begin(&f, base + 40U); release(&f, base + 80U, 40);
    CHECK(f.fsm.first_down_ms == UINT32_MAX - 10U && f.fsm.first_up_ms == 29U);
    begin(&f, base + 120U); release(&f, base + 160U, 40);
    CHECK(f.clicks == 2U);
    init(&f); arm(&f, UINT32_MAX - 10U); /* Stable-UP debounce itself wraps. */
    CHECK(f.out.state == TP_TAP_ARMED && f.out.up_samples == 2U);
    begin(&f, 30); release(&f, 70, 40);
    CHECK(f.clicks == 1U);
    init(&f); base = UINT32_MAX - 110U; arm(&f, base);
    begin(&f, base + 40U); release(&f, base + 100U, 60);
    CHECK(f.fsm.first_up_ms == UINT32_MAX - 10U && f.fsm.last_ms == 9U);
    CHECK(f.clicks == 1U); /* Candidate release-confirmation debounce wraps. */
    init(&f); arm(&f, UINT32_MAX - 100U); begin(&f, UINT32_MAX - 60U);
    packet(&f, UINT32_MAX - 20U, TP_TAP_UP, 0, 0);
    expect(&f, 9, TP_TAP_DOWN, 0, 0, TP_TAP_CONTACT, TP_TAP_REASON_UP_BOUNCE, false);
    CHECK(f.out.touch_duration_ms == 70U && f.fsm.first_down_ms == UINT32_MAX - 60U);
    release(&f, 19, 80);
    CHECK(f.clicks == 1U); /* Bounce preserves the original pre-rollover origin. */
    init(&f); arm(&f, UINT32_MAX - 50U);
    packet(&f, UINT32_MAX - 10U, TP_TAP_DOWN, 0, 0);
    packet(&f, 90, TP_TAP_DOWN, 0, 0); /* 101ms across rollover. */
    CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.reason == TP_TAP_REASON_GAP);
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_suppress_in_all_phases(void) {
    for (unsigned phase = 0; phase < 4; ++phase) {
        struct fixture f;
        uint32_t t = prepare_phase(&f, phase);
        full(&f, t, TP_TAP_UP, 0, 0, true, true);
        CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.reason == TP_TAP_REASON_SUPPRESSED);
        CHECK(f.out.up_samples == 0U);
        packet(&f, t + 10U, TP_TAP_DOWN, 0, 0);
        CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP);
        full(&f, t + 30U, TP_TAP_UP, 0, 0, true, true);
        CHECK(f.out.up_samples == 0U);
        packet(&f, t + 50U, TP_TAP_UP, 0, 0);
        expect(&f, t + 70U, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
        CHECK(f.clicks == 0U);
    }
    struct fixture f;
    init(&f); arm(&f, 0); begin(&f, 40);
    packet(&f, 80, TP_TAP_UP, 0, 0);
    full(&f, 100, TP_TAP_UP, 0, 0, true, true); /* Suppress the exact ready step. */
    CHECK(!f.out.click_ready && f.out.reason == TP_TAP_REASON_SUPPRESSED && f.clicks == 0U);
    ++groups;
}

static void test_disable_reenable_and_contact_already_present(void) {
    for (unsigned phase = 0; phase < 4; ++phase) {
        struct fixture f;
        uint32_t t = prepare_phase(&f, phase);
        f.config.enabled = false;
        expect(&f, t, TP_TAP_UP, 0, 0, TP_TAP_DISABLED, TP_TAP_REASON_DISABLED, false);
        CHECK(!f.fsm.have_time && !f.fsm.down_confirmed);
        f.config.enabled = true;
        expect(&f, t + 10U, TP_TAP_DOWN, 0, 0, TP_TAP_WAIT_UP, TP_TAP_REASON_NEED_UP, false);
        packet(&f, t + 30U, TP_TAP_UP, 0, 0);
        expect(&f, t + 50U, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
        CHECK(f.clicks == 0U);
        begin(&f, t + 70U); release(&f, t + 110U, 40);
        CHECK(f.clicks == 1U);
    }
    struct fixture f;
    init(&f); packet(&f, 0, TP_TAP_UP, 0, 0);
    f.config.enabled = false; packet(&f, 10, TP_TAP_UP, 0, 0);
    f.config.enabled = true; packet(&f, 20, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_WAIT_UP && f.out.up_samples == 1U);
    expect(&f, 40, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    CHECK(f.clicks == 0U);
    ++groups;
}

static void test_coordinate_overflow_and_extremes(void) {
    for (unsigned axis = 0; axis < 2; ++axis) for (unsigned sign = 0; sign < 2; ++sign) {
        struct fixture f;
        init(&f); f.config.max_displacement_counts = (uint32_t)INT32_MAX; arm(&f, 0);
        int32_t start = sign ? -INT32_MAX : INT32_MAX;
        int32_t delta = sign ? -2 : 1;
        packet(&f, 40, TP_TAP_DOWN, axis ? 0 : start, axis ? start : 0);
        CHECK(f.out.state == TP_TAP_DOWN_DEBOUNCE);
        expect(&f, 60, TP_TAP_DOWN, axis ? 0 : delta, axis ? delta : 0,
               TP_TAP_CANCEL_WAIT_UP, TP_TAP_REASON_COORD_OVERFLOW, false);
        CHECK((axis ? f.out.sum_y : f.out.sum_x) == start);
        packet(&f, 80, TP_TAP_DOWN, axis ? 0 : -start, axis ? -start : 0);
        CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP);
        packet(&f, 100, TP_TAP_UP, 0, 0);
        expect(&f, 120, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
        CHECK(f.clicks == 0U);
    }
    struct fixture f;
    init(&f); f.config.max_displacement_counts = (uint32_t)INT32_MAX; arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, INT32_MIN, INT32_MIN);
    CHECK(f.out.reason == TP_TAP_REASON_DISPLACEMENT);
    CHECK(f.out.max_displacement_sq == (UINT64_C(1) << 63));
    init(&f); f.config.max_displacement_counts = (uint32_t)INT32_MAX; arm(&f, 0);
    packet(&f, 40, TP_TAP_DOWN, INT32_MAX, INT32_MAX);
    CHECK(f.out.max_displacement_sq == UINT64_C(2) * (uint64_t)INT32_MAX * (uint64_t)INT32_MAX);
    CHECK(f.out.reason == TP_TAP_REASON_DISPLACEMENT && f.clicks == 0U);
    ++groups;
}

static void test_minimum_sample_count_and_zero_radius(void) {
    struct fixture f;
    init(&f); f.config.min_samples = 3U;
    packet(&f, 0, TP_TAP_UP, 0, 0);
    packet(&f, 20, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_WAIT_UP && f.out.up_samples == 2U);
    expect(&f, 21, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
    packet(&f, 40, TP_TAP_DOWN, 0, 0);
    packet(&f, 60, TP_TAP_DOWN, 0, 0);
    CHECK(f.out.state == TP_TAP_DOWN_DEBOUNCE && f.out.down_samples == 2U);
    expect(&f, 61, TP_TAP_DOWN, 0, 0, TP_TAP_CONTACT, TP_TAP_REASON_DOWN_CONFIRMED, false);
    packet(&f, 80, TP_TAP_UP, 0, 0);
    packet(&f, 100, TP_TAP_UP, 0, 0);
    CHECK(f.out.state == TP_TAP_UP_DEBOUNCE && f.out.up_samples == 2U);
    expect(&f, 101, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_READY, true);
    CHECK(f.out.down_samples == 3U && f.out.up_samples == 3U && f.clicks == 1U);
    init(&f); f.config.max_displacement_counts = 0U; arm(&f, 0);
    begin(&f, 40); release(&f, 80, 40);
    packet(&f, 120, TP_TAP_DOWN, 1, 0);
    CHECK(f.out.reason == TP_TAP_REASON_DISPLACEMENT && f.clicks == 1U);
    ++groups;
}

static void test_bad_config_and_live_limit_change(void) {
    for (unsigned bad = 0; bad < 12; ++bad) {
        struct fixture f;
        init(&f);
        switch (bad) {
        case 0: f.config.min_samples = 0U; break;
        case 1: f.config.min_samples = 1U; break;
        case 2: f.config.max_duration_ms = 0U; break;
        case 3: f.config.max_duration_ms = UINT32_C(0x80000000); break;
        case 4: f.config.max_duration_ms = 19U; break;
        case 5: f.config.max_displacement_counts = UINT32_C(0x80000000); break;
        case 6: f.config.down_debounce_ms = 0U; break;
        case 7: f.config.down_debounce_ms = UINT32_C(0x80000000); break;
        case 8: f.config.up_debounce_ms = 0U; break;
        case 9: f.config.up_debounce_ms = UINT32_C(0x80000000); break;
        case 10: f.config.max_gap_ms = 0U; break;
        default: f.config.max_gap_ms = UINT32_C(0x80000000); break;
        }
        CHECK(!tp_tap_config_valid(&f.config));
        packet(&f, 0, TP_TAP_UP, 0, 0);
        CHECK(f.out.reason == TP_TAP_REASON_BAD_CONFIG && !f.out.click_ready);
        f.config = tp_tap_default_config(); f.config.enabled = true;
        packet(&f, 10, TP_TAP_UP, 0, 0); /* Restored limits also invalidate this step. */
        CHECK(f.out.state == TP_TAP_CANCEL_WAIT_UP && f.out.up_samples == 0U);
        packet(&f, 20, TP_TAP_UP, 0, 0);
        expect(&f, 40, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
        CHECK(f.clicks == 0U);
    }
    for (unsigned phase = 0; phase < 4; ++phase) {
        struct fixture f;
        uint32_t t = prepare_phase(&f, phase);
        f.config.max_displacement_counts = 13U;
        expect(&f, t, TP_TAP_UP, 0, 0, TP_TAP_CANCEL_WAIT_UP,
               TP_TAP_REASON_CONFIG_CHANGED, false);
        CHECK(f.out.up_samples == 0U);
        packet(&f, t + 10U, TP_TAP_DOWN, 0, 0);
        packet(&f, t + 30U, TP_TAP_UP, 0, 0);
        expect(&f, t + 50U, TP_TAP_UP, 0, 0, TP_TAP_ARMED, TP_TAP_REASON_ARMED, false);
        CHECK(f.clicks == 0U);
    }
    ++groups;
}

static void test_names_and_memory_scope(void) {
    for (int i = TP_TAP_WAIT_UP; i <= TP_TAP_CANCEL_WAIT_UP; ++i) {
        CHECK(strcmp(tp_tap_state_name((enum tp_tap_state)i), "unknown-state") != 0);
    }
    for (int i = TP_TAP_REASON_RESET; i <= TP_TAP_REASON_CONFIG_CHANGED; ++i) {
        CHECK(strcmp(tp_tap_reason_name((enum tp_tap_reason)i), "unknown-reason") != 0);
    }
    CHECK(strcmp(tp_tap_state_name((enum tp_tap_state)99), "unknown-state") == 0);
    CHECK(strcmp(tp_tap_reason_name((enum tp_tap_reason)99), "unknown-reason") == 0);
    struct guarded {
        uint32_t unrelated_key_state;
        struct tp_tap_fsm fsm;
        uint32_t unrelated_pointer_state;
    } g = {.unrelated_key_state = UINT32_C(0x12345678),
           .unrelated_pointer_state = UINT32_C(0x87654321)};
    struct tp_tap_config config = tp_tap_default_config(); config.enabled = true;
    struct tp_tap_config before;
    memcpy(&before, &config, sizeof(config));
    tp_tap_reset(&g.fsm);
    const struct tp_tap_input inputs[] = {
        {.t_ms = 0, .valid = true, .contact = TP_TAP_UP},
        {.t_ms = 20, .valid = true, .contact = TP_TAP_UP},
        {.t_ms = 40, .valid = true, .contact = TP_TAP_DOWN},
        {.t_ms = 60, .valid = true, .contact = TP_TAP_DOWN},
        {.t_ms = 80, .valid = false, .contact = TP_TAP_UP},
    };
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        struct tp_tap_result out = tp_tap_step(&g.fsm, &config, inputs[i]);
        CHECK(!out.click_ready);
        CHECK(g.unrelated_key_state == UINT32_C(0x12345678));
        CHECK(g.unrelated_pointer_state == UINT32_C(0x87654321));
        CHECK(memcmp(&config, &before, sizeof(config)) == 0);
    }
    tp_tap_reset(&g.fsm);
    CHECK(g.unrelated_key_state == UINT32_C(0x12345678));
    CHECK(g.unrelated_pointer_state == UINT32_C(0x87654321));
    ++groups;
}

int main(void) {
    test_defaults_and_unknown();
    test_valid_and_repeated_taps();
    test_boot_finger_and_up_bounce_gate();
    test_reset_in_every_candidate_phase();
    test_short_touch_and_no_rebuild();
    test_up_confirmation_bounce_preserves_candidate();
    test_bounce_does_not_restart_timeout_or_motion();
    test_duration_boundaries_and_frozen_confirmation();
    test_euclidean_radius_matrix();
    test_slide_out_return_and_long_hold();
    test_motion_on_release_and_confirmation();
    test_invalid_in_all_phases_and_sticky_reason();
    test_gap_limit_and_missing_frames();
    test_stale_backward_and_half_range();
    test_rollover();
    test_suppress_in_all_phases();
    test_disable_reenable_and_contact_already_present();
    test_coordinate_overflow_and_extremes();
    test_minimum_sample_count_and_zero_radius();
    test_bad_config_and_live_limit_change();
    test_names_and_memory_scope();
    printf("PASS Tap FSM: %u groups, %lu replay steps, %lu checks; "
           "1089 displacement origins + 201 duration boundaries; "
           "synthetic host replay ONLY, no hardware/HID validation\n", groups, steps, checks);
    return 0;
}
