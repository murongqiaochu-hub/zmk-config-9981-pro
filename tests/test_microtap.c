/* SPDX-License-Identifier: Apache-2.0
 * Standalone host tests of an opt-in motion heuristic, NOT contact/HID tests.
 * Build from repository root (write the executable outside this shared tree):
 * cc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion
 *    -Wshadow -Wstrict-prototypes -fsanitize=address,undefined
 *    -fno-sanitize-recover=all -fno-omit-frame-pointer
 *    tests/test_microtap.c -o /tmp/test_microtap && /tmp/test_microtap
 */
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap.h"

_Static_assert(TP_MICROTAP_FRAME_CAPACITY == 16U, "public result is 16 frames");
_Static_assert(TP_MICROTAP_MAX_PENDING + 1U == TP_MICROTAP_FRAME_CAPACITY,
               "a triggering raw frame must fit in the same result");

static uint64_t checks;
static uint64_t steps;
static unsigned groups;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #expr); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* Independent ordered ledger: every accepted input must be returned exactly
 * once, remain pending, or belong to the OLD candidate discarded by a click.
 * Checks frame identity/order as well as counts and signed dx/dy conservation;
 * opposite deltas cannot conceal loss, duplication or reordered raw frames. */
struct fixture {
    struct tp_microtap_state state;
    struct tp_microtap_result out;
    struct tp_microtap_frame ledger[TP_MICROTAP_FRAME_CAPACITY];
    uint8_t ledger_count;
    uint64_t input_frames;
    uint64_t delivered_frames;
    uint64_t discarded_frames;
    uint64_t clicks;
    int64_t input_x;
    int64_t input_y;
    int64_t delivered_x;
    int64_t delivered_y;
    int64_t discarded_x;
    int64_t discarded_y;
};

static void same_frame(struct tp_microtap_frame a, struct tp_microtap_frame b) {
    CHECK(a.at_ms == b.at_ms);
    CHECK(a.dx == b.dx);
    CHECK(a.dy == b.dy);
}

static uint32_t magnitude(int32_t value) {
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static void init(struct fixture *f) {
    *f = (struct fixture){0};
    tp_microtap_reset(&f->state);
    CHECK(!tp_microtap_pending(&f->state));
}

static struct tp_microtap_result full(struct fixture *f, uint32_t now,
                                      int16_t dx, int16_t dy, bool valid,
                                      bool enabled, bool blocked) {
    const struct tp_microtap_state before = f->state;
    const bool motion = valid && (dx != 0 || dy != 0);
    const bool clock_fault = before.have_time &&
                            (uint32_t)(now - before.last_poll_ms) > 100U;
    ++steps;
    f->out = tp_microtap_step(&f->state, now, dx, dy, valid, enabled, blocked);
    CHECK(f->out.count <= TP_MICROTAP_FRAME_CAPACITY);
    if (!valid || !enabled || blocked || clock_fault) {
        CHECK(!f->out.click);
    }
    if (f->out.click) {
        CHECK(valid && enabled && !blocked && !clock_fault);
        CHECK(before.mode == TP_MICROTAP_CANDIDATE);
        CHECK(before.count >= 3U && before.count == f->ledger_count);
        CHECK(before.path >= 3U);
        CHECK((uint32_t)(before.last_motion_ms - before.first_ms) >= 15U);
        CHECK((uint32_t)(now - before.last_motion_ms) >= 30U);
        CHECK((uint32_t)(now - before.first_ms) <= 100U);
        CHECK((uint32_t)(before.last_motion_ms - before.first_ms) <= 80U);
        CHECK(before.path <= 16U && before.max_offset <= 8U);
        for (uint8_t i = 0U; i < f->ledger_count; ++i) {
            f->discarded_x += f->ledger[i].dx;
            f->discarded_y += f->ledger[i].dy;
            ++f->discarded_frames;
        }
        f->ledger_count = 0U;
        ++f->clicks;
    }
    if (motion) {
        CHECK(f->ledger_count < TP_MICROTAP_FRAME_CAPACITY);
        f->ledger[f->ledger_count++] = (struct tp_microtap_frame){
            .at_ms = now, .dx = dx, .dy = dy,
        };
        f->input_x += dx;
        f->input_y += dy;
        ++f->input_frames;
    }
    for (uint8_t i = 0U; i < f->out.count; ++i) {
        CHECK(f->ledger_count != 0U);
        CHECK(f->out.frames[i].dx != 0 || f->out.frames[i].dy != 0);
        same_frame(f->out.frames[i], f->ledger[0]);
        f->delivered_x += f->out.frames[i].dx;
        f->delivered_y += f->out.frames[i].dy;
        ++f->delivered_frames;
        --f->ledger_count;
        for (uint8_t j = 0U; j < f->ledger_count; ++j) {
            f->ledger[j] = f->ledger[j + 1U];
        }
    }
    CHECK(f->ledger_count == f->state.count);
    CHECK(f->state.count <= TP_MICROTAP_MAX_PENDING);
    CHECK(tp_microtap_pending(&f->state) == (f->ledger_count != 0U));
    CHECK((f->state.mode == TP_MICROTAP_CANDIDATE) ==
          tp_microtap_pending(&f->state));
    int64_t pending_x = 0;
    int64_t pending_y = 0;
    uint32_t path = 0U;
    uint32_t peak = 0U;
    for (uint8_t i = 0U; i < f->ledger_count; ++i) {
        same_frame(f->state.frames[i], f->ledger[i]);
        pending_x += f->ledger[i].dx;
        pending_y += f->ledger[i].dy;
        path += magnitude(f->ledger[i].dx) + magnitude(f->ledger[i].dy);
        const uint32_t ax = magnitude((int32_t)pending_x);
        const uint32_t ay = magnitude((int32_t)pending_y);
        const uint32_t offset = ax > ay ? ax : ay;
        if (offset > peak) {
            peak = offset;
        }
    }
    if (tp_microtap_pending(&f->state)) {
        CHECK(f->state.first_ms == f->ledger[0].at_ms);
        CHECK(f->state.last_motion_ms == f->ledger[f->ledger_count - 1U].at_ms);
        CHECK(f->state.path == path && path <= 16U);
        CHECK(f->state.max_offset == peak && peak <= 8U);
        CHECK(f->state.sum_x == pending_x && f->state.sum_y == pending_y);
        CHECK((uint32_t)(f->state.last_motion_ms - f->state.first_ms) <= 80U);
    }
    CHECK(f->input_x == f->delivered_x + f->discarded_x + pending_x);
    CHECK(f->input_y == f->delivered_y + f->discarded_y + pending_y);
    CHECK(f->input_frames == f->delivered_frames + f->discarded_frames +
                             f->ledger_count);
    return f->out;
}

static struct tp_microtap_result packet(struct fixture *f, uint32_t now,
                                        int16_t dx, int16_t dy) {
    return full(f, now, dx, dy, true, true, false);
}

static void three_frames(struct fixture *f, uint32_t base, int16_t dx, int16_t dy) {
    packet(f, base, dx, dy);
    packet(f, base + 10U, dx, dy);
    packet(f, base + 20U, dx, dy);
}

static void expect_frame(const struct fixture *f, uint8_t i, uint32_t now,
                         int16_t dx, int16_t dy) {
    CHECK(i < f->out.count);
    same_frame(f->out.frames[i], (struct tp_microtap_frame){
        .at_ms = now, .dx = dx, .dy = dy,
    });
}

static void complete_group(const char *name) {
    ++groups;
    printf("PASS %s\n", name);
}

static void test_zero_reset_and_pending(void) {
    struct fixture f;
    init(&f);
    CHECK(TP_MICROTAP_QUIET_MS == 30U);
    CHECK(TP_MICROTAP_MIN_FRAMES == 3U && TP_MICROTAP_MIN_PATH == 3U);
    CHECK(TP_MICROTAP_MIN_MOTION_SPAN_MS == 15U);
    CHECK(TP_MICROTAP_MAX_CANDIDATE_MS == 100U);
    CHECK(TP_MICROTAP_MAX_MOTION_SPAN_MS == 80U);
    CHECK(TP_MICROTAP_MAX_PATH == 16U && TP_MICROTAP_MAX_OFFSET == 8U);
    CHECK(TP_MICROTAP_MAX_POLL_GAP_MS == 100U);
    for (uint32_t now = 0U; now <= 1000U; ++now) {
        packet(&f, now, 0, 0);
        CHECK(!f.out.click && f.out.count == 0U);
        CHECK(f.state.mode == TP_MICROTAP_IDLE);
    }
    CHECK(f.clicks == 0U && f.input_frames == 0U);
    full(&f, 1010U, INT16_MIN, INT16_MAX, false, true, false);
    CHECK(f.out.count == 0U && !f.out.click);
    packet(&f, 1040U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_IDLE && f.clicks == 0U);

    init(&f);
    packet(&f, 0U, 1, -1);
    CHECK(tp_microtap_pending(&f.state));
    for (unsigned i = 0U; i < 100U; ++i) {
        packet(&f, 0U, 0, 0);
        CHECK(!f.out.click && f.out.count == 0U);
        CHECK(tp_microtap_pending(&f.state));
    }
    packet(&f, 29U, 0, 0);
    CHECK(!f.out.click);
    packet(&f, 30U, 0, 0);
    CHECK(!f.out.click && f.out.count == 1U && !tp_microtap_pending(&f.state));
    packet(&f, 31U, 0, 0);
    CHECK(!f.out.click && f.out.count == 0U);

    /* Hard modal reset intentionally abandons old movement, without a click. */
    struct tp_microtap_state state;
    tp_microtap_reset(&state);
    struct tp_microtap_result out = tp_microtap_step(&state, 100U, 2, 1,
                                                   true, true, false);
    CHECK(out.count == 0U && !out.click && tp_microtap_pending(&state));
    tp_microtap_reset(&state);
    CHECK(!tp_microtap_pending(&state) && !state.have_time);
    out = tp_microtap_step(&state, 130U, 0, 0, true, true, false);
    CHECK(out.count == 0U && !out.click);
    tp_microtap_reset(&state);
    CHECK(!tp_microtap_pending(&state));
    complete_group("zero/no data/reset cannot click; pending and equal timestamps");
}

static void test_disabled_and_blocked_transparency(void) {
    struct fixture f;
    init(&f);
    const int16_t deltas[][2] = {
        {1, -1}, {0, 0}, {INT16_MIN, INT16_MAX}, {-8, 8}, {INT16_MAX, INT16_MIN},
    };
    for (unsigned i = 0U; i < sizeof(deltas) / sizeof(deltas[0]); ++i) {
        full(&f, i * 10U, deltas[i][0], deltas[i][1], true, false, false);
        CHECK(!f.out.click);
        const bool motion = deltas[i][0] != 0 || deltas[i][1] != 0;
        CHECK(f.out.count == (motion ? 1U : 0U));
        if (motion) {
            expect_frame(&f, 0U, i * 10U, deltas[i][0], deltas[i][1]);
        }
        CHECK(!tp_microtap_pending(&f.state));
    }
    CHECK(f.input_frames == f.delivered_frames && f.discarded_frames == 0U);

    /* Disabled/blocked/invalid cancellation at an otherwise ready boundary. */
    for (unsigned valid = 0U; valid <= 1U; ++valid) {
        for (unsigned enabled = 0U; enabled <= 1U; ++enabled) {
            for (unsigned blocked = 0U; blocked <= 1U; ++blocked) {
                if (valid != 0U && enabled != 0U && blocked == 0U) {
                    continue;
                }
                init(&f);
                packet(&f, 1000U, 2, -1);
                packet(&f, 1010U, -1, 2);
                packet(&f, 1020U, 1, 0);
                full(&f, 1050U, 3, -4, valid != 0U, enabled != 0U, blocked != 0U);
                CHECK(!f.out.click && f.out.count == (valid != 0U ? 4U : 3U));
                expect_frame(&f, 0U, 1000U, 2, -1);
                expect_frame(&f, 1U, 1010U, -1, 2);
                expect_frame(&f, 2U, 1020U, 1, 0);
                if (valid != 0U) {
                    expect_frame(&f, 3U, 1050U, 3, -4);
                }
                CHECK(f.input_frames == f.delivered_frames);
            }
        }
    }
    for (unsigned kind = 0U; kind < 2U; ++kind) {
        init(&f);
        three_frames(&f, 0U, 1, 1);
        full(&f, 50U, 0, 0, true, kind != 0U, kind != 0U);
        CHECK(!f.out.click && f.out.count == 3U);
        expect_frame(&f, 0U, 0U, 1, 1);
        three_frames(&f, 51U, 1, -1);
        CHECK(tp_microtap_pending(&f.state));
        packet(&f, 101U, 0, 0);
        CHECK(f.out.click && f.clicks == 1U);
    }

    init(&f);
    full(&f, 0U, 3, 0, true, false, false);
    packet(&f, 10U, 1, 0);
    CHECK(f.out.count == 1U && !f.out.click); /* Re-enable cannot reinterpret tail. */
    packet(&f, 39U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_PASSTHROUGH);
    packet(&f, 40U, 0, 0);
    three_frames(&f, 41U, 1, 0);
    packet(&f, 91U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U);

    init(&f);
    full(&f, 0U, 0, 0, true, false, false);
    three_frames(&f, 1U, 1, 0);
    CHECK(tp_microtap_pending(&f.state)); /* No startup cooldown from a zero poll. */
    packet(&f, 51U, 0, 0);
    CHECK(f.out.click);
    complete_group("disabled/blocked transparent raw replay; cancellation wins over quiet");
}

static void test_large_and_int16_extremes(void) {
    struct fixture f;
    for (int32_t raw = INT16_MIN; raw <= INT16_MAX; ++raw) {
        init(&f);
        packet(&f, 0U, (int16_t)raw, 0);
        const bool small_motion = raw != 0 && magnitude(raw) <= 8U;
        CHECK(f.out.count == (magnitude(raw) > 8U ? 1U : 0U));
        CHECK(tp_microtap_pending(&f.state) == small_motion);
        if (f.out.count != 0U) {
            expect_frame(&f, 0U, 0U, (int16_t)raw, 0);
        }
        packet(&f, 30U, 0, 0);
        CHECK(!f.out.click && !tp_microtap_pending(&f.state));
        CHECK(f.input_frames == f.delivered_frames);
    }
    for (int32_t x = -12; x <= 12; ++x) {
        for (int32_t y = -12; y <= 12; ++y) {
            init(&f);
            packet(&f, 0U, (int16_t)x, (int16_t)y);
            packet(&f, 30U, 0, 0);
            CHECK(!f.out.click && f.input_frames == f.delivered_frames);
        }
    }
    const int16_t extremes[][2] = {
        {INT16_MIN, INT16_MIN}, {INT16_MAX, INT16_MAX},
        {INT16_MIN, INT16_MAX}, {INT16_MAX, INT16_MIN},
        {0, INT16_MIN}, {0, INT16_MAX},
    };
    for (unsigned i = 0U; i < sizeof(extremes) / sizeof(extremes[0]); ++i) {
        init(&f);
        packet(&f, 10U, 1, -1);
        packet(&f, 11U, extremes[i][0], extremes[i][1]);
        CHECK(!f.out.click && f.out.count == 2U);
        expect_frame(&f, 0U, 10U, 1, -1);
        expect_frame(&f, 1U, 11U, extremes[i][0], extremes[i][1]);
        packet(&f, 41U, 0, 0);
        CHECK(f.clicks == 0U && f.input_frames == f.delivered_frames);
    }
    complete_group("all 65536 int16 x deltas, Linf diagonals and two-axis extremes");
}

static void test_traveled_path_and_peak(void) {
    struct fixture f;
    init(&f);
    packet(&f, 0U, 4, 0);
    packet(&f, 10U, -4, 0);
    packet(&f, 20U, 4, 0);
    packet(&f, 30U, -4, 0);
    CHECK(f.state.path == 16U && f.state.max_offset == 4U && f.state.sum_x == 0);
    packet(&f, 60U, 0, 0);
    CHECK(f.out.click && f.out.count == 0U && f.discarded_frames == 4U);
    packet(&f, 100U, 0, 0);
    CHECK(!f.out.click && f.out.count == 0U); /* No post-stop compensation. */

    init(&f);
    packet(&f, 0U, 4, 0);
    packet(&f, 10U, -4, 0);
    packet(&f, 20U, 4, 0);
    packet(&f, 30U, -4, 0);
    packet(&f, 40U, 1, 0);
    CHECK(!f.out.click && f.out.count == 5U);
    expect_frame(&f, 0U, 0U, 4, 0);
    expect_frame(&f, 1U, 10U, -4, 0);
    expect_frame(&f, 2U, 20U, 4, 0);
    expect_frame(&f, 3U, 30U, -4, 0);
    expect_frame(&f, 4U, 40U, 1, 0);
    packet(&f, 50U, 1, 0);
    CHECK(f.out.count == 1U && !tp_microtap_pending(&f.state));
    packet(&f, 79U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_PASSTHROUGH && f.clicks == 0U);
    three_frames(&f, 80U, 1, 0);
    CHECK(tp_microtap_pending(&f.state));
    packet(&f, 130U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U);

    init(&f);
    packet(&f, 0U, 8, 0);
    packet(&f, 10U, 1, 0); /* Peak 9 must reject BEFORE returning to the origin. */
    CHECK(f.out.count == 2U && !f.out.click);
    packet(&f, 20U, -9, 0);
    CHECK(f.out.count == 1U);
    packet(&f, 50U, 0, 0);
    CHECK(f.clicks == 0U && f.input_frames == f.delivered_frames);

    init(&f);
    packet(&f, 0U, 8, 6); /* Linf=8, NOT a Euclidean radius of 8. */
    packet(&f, 10U, -1, 0);
    packet(&f, 20U, 0, -1);
    CHECK(f.state.path == 16U && f.state.max_offset == 8U);
    packet(&f, 50U, 0, 0);
    CHECK(f.out.click && f.discarded_x == 7 && f.discarded_y == 5);
    complete_group("Manhattan traveled path/round trips and sticky maximum Linf rejection");
}

static void deadline_prefix(struct fixture *f, uint32_t last) {
    init(f);
    packet(f, 0U, 1, 0);
    packet(f, 27U, -1, 0);
    packet(f, 54U, 1, 0);
    packet(f, last, -1, 0);
}

static void test_long_motion_and_total_deadline(void) {
    struct fixture f;
    init(&f);
    packet(&f, 0U, 1, 0);
    packet(&f, 20U, -1, 0);
    packet(&f, 40U, 1, 0);
    packet(&f, 60U, -1, 0);
    packet(&f, 80U, 1, 0);
    CHECK(f.state.mode == TP_MICROTAP_CANDIDATE && f.state.count == 5U);
    packet(&f, 81U, -1, 0);
    CHECK(!f.out.click && f.out.count == 6U);
    packet(&f, 90U, 1, 0);
    CHECK(f.out.count == 1U);
    packet(&f, 120U, 0, 0);
    CHECK(f.clicks == 0U && f.input_frames == f.delivered_frames);

    deadline_prefix(&f, 70U);
    packet(&f, 99U, 0, 0);
    CHECK(!f.out.click && tp_microtap_pending(&f.state));
    packet(&f, 100U, 0, 0);
    CHECK(f.out.click && f.out.count == 0U && f.discarded_frames == 4U);

    deadline_prefix(&f, 70U);
    packet(&f, 99U, 0, 0);
    packet(&f, 101U, 0, 0); /* Quiet ready but total age too old, despite no poll gap. */
    CHECK(!f.out.click && f.out.count == 4U && f.clicks == 0U);
    CHECK(f.input_frames == f.delivered_frames);

    deadline_prefix(&f, 75U);
    packet(&f, 100U, 0, 0);
    CHECK(!f.out.click && tp_microtap_pending(&f.state));
    packet(&f, 101U, 0, 0);
    CHECK(!f.out.click && f.out.count == 4U);

    /* 80ms motion is accepted, but 80+30ms quiet cannot evade the total cap. */
    deadline_prefix(&f, 80U);
    packet(&f, 100U, 0, 0);
    CHECK(tp_microtap_pending(&f.state));
    packet(&f, 101U, 0, 0);
    CHECK(f.out.count == 4U && !f.out.click);
    packet(&f, 102U, 1, 0);
    CHECK(f.out.count == 1U); /* Small tail after timeout remains passthrough. */
    three_frames(&f, 132U, 1, 0);
    CHECK(tp_microtap_pending(&f.state));
    packet(&f, 182U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U);
    complete_group("motion span 80/81ms, total candidate cap 100/101ms, no late click");
}

static void test_admission_double_triple_and_new_frame(void) {
    struct fixture f;
    for (unsigned count = 1U; count <= 2U; ++count) {
        init(&f);
        for (unsigned i = 0U; i < count; ++i) {
            packet(&f, i * 10U, 2, 0); /* Path>=3 with two frames still cannot click. */
        }
        packet(&f, (count - 1U) * 10U + 30U, 0, 0);
        CHECK(!f.out.click && f.out.count == count && f.clicks == 0U);
        CHECK(f.delivered_frames == count && !tp_microtap_pending(&f.state));
    }
    for (uint32_t span = 0U; span <= 15U; ++span) {
        init(&f);
        packet(&f, 0U, 1, 0);
        packet(&f, span / 2U, 1, 0);
        packet(&f, span, 1, 0);
        packet(&f, span + 30U, 0, 0);
        CHECK(f.out.click == (span == 15U));
        CHECK(f.out.count == (span == 15U ? 0U : 3U));
    }

    init(&f);
    three_frames(&f, 0U, 1, 0);
    packet(&f, 49U, 0, 0);
    CHECK(!f.out.click && tp_microtap_pending(&f.state));
    packet(&f, 50U, 0, 0);
    CHECK(f.out.click && f.discarded_frames == 3U);
    packet(&f, 51U, 0, 0);
    CHECK(!f.out.click && f.out.count == 0U);

    init(&f);
    three_frames(&f, 0U, 1, 0);
    packet(&f, 50U, 0, 1);
    CHECK(f.out.click && f.out.count == 0U && tp_microtap_pending(&f.state));
    CHECK(f.state.count == 1U && f.state.frames[0].at_ms == 50U);
    packet(&f, 60U, 0, 1);
    packet(&f, 70U, 0, 1);
    packet(&f, 100U, 0, 0);
    CHECK(f.out.click && f.clicks == 2U && f.discarded_frames == 6U);

    init(&f);
    three_frames(&f, 0U, 1, 0);
    packet(&f, 50U, 0, 1);
    CHECK(f.out.click && tp_microtap_pending(&f.state));
    packet(&f, 60U, 0, 1);
    packet(&f, 70U, 0, 1);
    packet(&f, 100U, -1, 0);
    CHECK(f.out.click && tp_microtap_pending(&f.state));
    packet(&f, 110U, -1, 0);
    packet(&f, 120U, -1, 0);
    packet(&f, 150U, 0, 0);
    CHECK(f.out.click && !tp_microtap_pending(&f.state));
    CHECK(f.clicks == 3U && f.delivered_frames == 0U && f.discarded_frames == 9U);

    init(&f);
    three_frames(&f, 0U, 1, 0);
    packet(&f, 50U, 12, -3);
    CHECK(f.out.click && f.out.count == 1U && !tp_microtap_pending(&f.state));
    expect_frame(&f, 0U, 50U, 12, -3); /* Old click AND new ordinary motion. */
    three_frames(&f, 80U, 1, 0);
    packet(&f, 130U, 0, 0);
    CHECK(f.out.click && f.clicks == 2U);

    init(&f);
    packet(&f, 0U, 2, 0);
    packet(&f, 30U, 1, 0); /* Underfilled old movement plus a NEW pending pulse. */
    CHECK(!f.out.click && f.out.count == 1U && tp_microtap_pending(&f.state));
    expect_frame(&f, 0U, 0U, 2, 0);
    packet(&f, 40U, 1, 0);
    packet(&f, 50U, 1, 0);
    packet(&f, 80U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U && f.delivered_frames == 1U);

    init(&f);
    three_frames(&f, 0U, 1, 0);
    packet(&f, 49U, 1, 0);
    CHECK(!f.out.click && f.state.count == 4U);
    packet(&f, 79U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U); /* <30ms pulses are ONE candidate. */

    init(&f);
    for (uint32_t i = 0U; i < 1000U; ++i) {
        const int16_t dx = i % 2U == 0U ? 1 : -1;
        packet(&f, i * 50U, dx, 0);
        CHECK(f.out.click == (i != 0U));
        CHECK(f.out.count == 0U && tp_microtap_pending(&f.state));
        packet(&f, i * 50U + 10U, dx, 0);
        packet(&f, i * 50U + 20U, dx, 0);
    }
    packet(&f, 50000U, 0, 0);
    CHECK(f.out.click && f.clicks == 1000U && f.discarded_frames == 3000U);
    complete_group("1/2 frames and span<15 replay; 3-frame double/triple/1000 pulses");
}

static void test_unknown_errors(void) {
    struct fixture f;
    init(&f);
    full(&f, 0U, INT16_MAX, INT16_MIN, false, true, false);
    CHECK(f.out.count == 0U && !f.out.click && f.input_frames == 0U);
    packet(&f, 29U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_PASSTHROUGH);
    packet(&f, 30U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_IDLE && f.clicks == 0U);

    init(&f);
    three_frames(&f, 0U, 1, -1);
    full(&f, 50U, INT16_MAX, INT16_MIN, false, true, false);
    CHECK(!f.out.click && f.out.count == 3U);
    expect_frame(&f, 0U, 0U, 1, -1);
    packet(&f, 79U, 1, 0);
    CHECK(f.out.count == 1U && !tp_microtap_pending(&f.state));
    three_frames(&f, 109U, 1, 0);
    CHECK(tp_microtap_pending(&f.state));
    packet(&f, 159U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U && f.delivered_frames == 4U);

    init(&f);
    packet(&f, 0U, 1, 0);
    for (uint32_t now = 30U; now <= 90U; now += 30U) {
        full(&f, now, 0, 0, false, true, false);
        CHECK(!f.out.click && !tp_microtap_pending(&f.state));
    }
    packet(&f, 90U, 0, 0);
    packet(&f, 119U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_PASSTHROUGH);
    packet(&f, 120U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_IDLE && f.clicks == 0U);
    CHECK(f.input_frames == f.delivered_frames);
    complete_group("unknown/error input never fabricates motion or completes a click");
}

static void test_clock_gaps_backwards_and_rollover(void) {
    struct fixture f;
    init(&f);
    packet(&f, 0U, 0, 0);
    packet(&f, 100U, 1, 0);
    CHECK(tp_microtap_pending(&f.state) && f.out.count == 0U); /* Gap=100 valid. */
    init(&f);
    packet(&f, 0U, 0, 0);
    packet(&f, 101U, 1, 0);
    CHECK(!tp_microtap_pending(&f.state) && f.out.count == 1U);
    init(&f);
    three_frames(&f, 0U, 1, 0);
    packet(&f, 100U, 0, 0);
    CHECK(f.out.click); /* Total age=100 is inclusive. */

    init(&f);
    packet(&f, 0U, 1, 0);
    packet(&f, 101U, 0, 0);
    CHECK(!f.out.click && f.out.count == 1U);
    expect_frame(&f, 0U, 0U, 1, 0);

    init(&f);
    packet(&f, 0U, 1, -1);
    packet(&f, 101U, 2, -2);
    CHECK(!f.out.click && f.out.count == 2U);
    expect_frame(&f, 0U, 0U, 1, -1);
    expect_frame(&f, 1U, 101U, 2, -2);
    packet(&f, 131U, 0, 0);
    CHECK(f.clicks == 0U);

    init(&f);
    packet(&f, 100U, 1, 0);
    packet(&f, 99U, 0, 0);
    CHECK(!f.out.click && f.out.count == 1U);
    packet(&f, 109U, 1, 0);
    CHECK(f.out.count == 1U);
    packet(&f, 139U, 0, 0);
    three_frames(&f, 140U, 1, 0);
    packet(&f, 190U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U);

    init(&f);
    packet(&f, 100U, 1, -1);
    packet(&f, 90U, 9, -9);
    CHECK(!f.out.click && f.out.count == 2U);
    expect_frame(&f, 0U, 100U, 1, -1); /* Input order, NOT numeric time sorting. */
    expect_frame(&f, 1U, 90U, 9, -9);

    const uint32_t jumps[] = {UINT32_C(0x80000000), UINT32_MAX, 10000U};
    for (unsigned i = 0U; i < sizeof(jumps) / sizeof(jumps[0]); ++i) {
        init(&f);
        packet(&f, 100U, 1, 0);
        packet(&f, 100U + jumps[i], 0, 0);
        CHECK(!f.out.click && f.out.count == 1U);
    }

    init(&f);
    three_frames(&f, UINT32_MAX - 40U, 1, -1);
    packet(&f, 9U, 0, 0);
    CHECK(f.out.click && f.discarded_frames == 3U);

    init(&f);
    packet(&f, UINT32_MAX - 60U, 1, -1);
    packet(&f, UINT32_MAX - 40U, -1, 1);
    packet(&f, UINT32_MAX - 20U, 1, -1);
    packet(&f, 9U, 0, 0);
    CHECK(f.out.click && f.discarded_frames == 3U);

    init(&f);
    three_frames(&f, UINT32_MAX - 50U, 1, 0);
    packet(&f, 49U, 0, 0);
    CHECK(f.out.click);
    init(&f);
    packet(&f, UINT32_MAX - 50U, 1, 0);
    packet(&f, 50U, 1, 0);
    CHECK(!f.out.click && f.out.count == 2U);
    expect_frame(&f, 0U, UINT32_MAX - 50U, 1, 0);
    expect_frame(&f, 1U, 50U, 1, 0);
    complete_group("poll gap 100/101ms, backwards/half-range faults, uint32 rollover");
}

static void fill_pending(struct fixture *f, uint32_t timestamp_step) {
    init(f);
    for (uint32_t i = 0U; i < TP_MICROTAP_MAX_PENDING; ++i) {
        packet(f, i * timestamp_step, i % 2U == 0U ? 1 : -1, 0);
        CHECK(!f->out.click && f->out.count == 0U);
        CHECK(f->state.count == i + 1U);
    }
}

static void test_bounded_buffer_and_overflow(void) {
    struct fixture f;
    fill_pending(&f, 1U);
    for (uint32_t now = 15U; now <= 29U; ++now) {
        packet(&f, now, 0, 0);
        CHECK(f.state.count == 15U && f.out.count == 0U);
    }
    packet(&f, 30U, -1, 0); /* Path=16 and Linf<=1, rejected ONLY by capacity. */
    CHECK(!f.out.click && f.out.count == 16U);
    for (uint8_t i = 0U; i < 15U; ++i) {
        expect_frame(&f, i, i, i % 2U == 0U ? 1 : -1, 0);
    }
    expect_frame(&f, 15U, 30U, -1, 0);
    CHECK(f.input_frames == 16U && f.delivered_frames == 16U);
    packet(&f, 60U, 0, 0);
    CHECK(f.clicks == 0U && !tp_microtap_pending(&f.state));

    fill_pending(&f, 2U);
    packet(&f, 58U, 0, 0);
    CHECK(f.out.click && f.discarded_frames == 15U); /* 15 frames can qualify. */

    fill_pending(&f, 0U);
    packet(&f, 30U, 9, 0); /* Old short-span run flushes, then new large frame. */
    CHECK(!f.out.click && f.out.count == 16U && f.delivered_frames == 16U);
    expect_frame(&f, 15U, 30U, 9, 0);

    fill_pending(&f, 0U);
    packet(&f, 0U, -1, 0);
    CHECK(!f.out.click && f.out.count == 16U);
    packet(&f, 30U, 0, 0);
    CHECK(!f.out.click);
    three_frames(&f, 31U, 1, 0);
    packet(&f, 81U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U);

    for (unsigned kind = 0U; kind < 4U; ++kind) {
        fill_pending(&f, 1U);
        full(&f, kind == 3U ? 115U : 15U, 1, -1,
             kind != 2U, kind != 0U, kind == 1U);
        CHECK(!f.out.click && f.out.count == (kind == 2U ? 15U : 16U));
        CHECK(!tp_microtap_pending(&f.state));
        CHECK(f.input_frames == f.delivered_frames);
    }
    complete_group("15 pending + triggering frame fit all 16 slots; zero polls use no slots");
}

static void test_passthrough_tail_and_exact_quiet(void) {
    struct fixture f;
    init(&f);
    packet(&f, 0U, 10, 0);
    CHECK(f.out.count == 1U);
    const uint32_t tail_times[] = {1U, 10U, 20U, 40U};
    for (unsigned i = 0U; i < sizeof(tail_times) / sizeof(tail_times[0]); ++i) {
        packet(&f, tail_times[i], 1, -1);
        CHECK(!f.out.click && f.out.count == 1U);
        CHECK(f.state.mode == TP_MICROTAP_PASSTHROUGH);
    }
    packet(&f, 69U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_PASSTHROUGH && f.clicks == 0U);
    packet(&f, 70U, 0, 0);
    CHECK(f.state.mode == TP_MICROTAP_IDLE && !f.out.click);
    three_frames(&f, 71U, 1, 0);
    packet(&f, 121U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U);

    init(&f);
    packet(&f, 0U, 9, 0);
    packet(&f, 29U, 1, 0);
    CHECK(f.out.count == 1U && !tp_microtap_pending(&f.state));
    packet(&f, 58U, 1, 0);
    CHECK(f.out.count == 1U);
    packet(&f, 88U, 1, 0); /* No zero packet is required at the >=30ms boundary. */
    CHECK(f.out.count == 0U && tp_microtap_pending(&f.state));
    packet(&f, 98U, 1, 0);
    packet(&f, 108U, 1, 0);
    packet(&f, 138U, 0, 0);
    CHECK(f.out.click && f.clicks == 1U && f.delivered_frames == 3U);
    complete_group("no tail click after ordinary movement; re-arm at quiet 30ms, not 29ms");
}

static uint32_t random_state = UINT32_C(0x4d696372);

static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_random_raw_order_and_conservation(void) {
    struct fixture f;
    init(&f);
    uint32_t now = UINT32_MAX - 1000U;
    const uint32_t deltas[] = {0U, 1U, 2U, 10U, 29U, 30U, 31U, 99U, 100U, 101U};
    for (unsigned i = 0U; i < 100000U; ++i) {
        const uint32_t choice = next_random();
        if (choice % 97U == 0U) {
            now -= 1U + next_random() % 100U;
        } else if (choice % 127U == 0U) {
            now += UINT32_C(0x80000000);
        } else {
            now += deltas[next_random() % (sizeof(deltas) / sizeof(deltas[0]))];
        }
        int16_t dx = 0;
        int16_t dy = 0;
        if (choice % 5U != 0U) {
            dx = (int16_t)((int32_t)(next_random() % 9U) - 4);
            dy = (int16_t)((int32_t)(next_random() % 9U) - 4);
        }
        if (choice % 13U == 0U) {
            dx = (int16_t)((int32_t)(next_random() % 65536U) - 32768);
            dy = (int16_t)((int32_t)(next_random() % 65536U) - 32768);
        }
        full(&f, now, dx, dy, next_random() % 11U != 0U,
             next_random() % 7U != 0U, next_random() % 17U == 0U);
    }
    full(&f, now, INT16_MAX, INT16_MIN, false, true, false);
    CHECK(!tp_microtap_pending(&f.state));
    CHECK(f.input_frames == f.delivered_frames + f.discarded_frames);
    CHECK(f.input_x == f.delivered_x + f.discarded_x);
    CHECK(f.input_y == f.delivered_y + f.discarded_y);
    CHECK(f.clicks != 0U && f.delivered_frames != 0U && f.discarded_frames != 0U);
    printf("PASS 100000 randomized steps: input=%" PRIu64 ", replayed=%" PRIu64
           ", tap-discarded=%" PRIu64 ", clicks=%" PRIu64 "\n",
           f.input_frames, f.delivered_frames, f.discarded_frames, f.clicks);
    ++groups;
}

int main(void) {
    test_zero_reset_and_pending();
    test_disabled_and_blocked_transparency();
    test_large_and_int16_extremes();
    test_traveled_path_and_peak();
    test_long_motion_and_total_deadline();
    test_admission_double_triple_and_new_frame();
    test_unknown_errors();
    test_clock_gaps_backwards_and_rollover();
    test_bounded_buffer_and_overflow();
    test_passthrough_tail_and_exact_quiet();
    test_random_raw_order_and_conservation();
    printf("PASS microtap: %u groups, %" PRIu64 " steps, %" PRIu64 " checks\n",
           groups, steps, checks);
    return EXIT_SUCCESS;
}
