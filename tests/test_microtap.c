/* SPDX-License-Identifier: Apache-2.0 -- actual policy + independent conservation ledger.
 * Host logic only: neither physical contact accuracy nor host HID acceptance. */
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap.h"

static uint64_t checks, steps;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)

struct fixture {
    struct tp_microtap_state s;
    struct tp_microtap_frame ledger[16];
    unsigned n;
    uint64_t input, replayed, discarded, clicks;
    int64_t ix, iy, rx, ry, tx, ty;
    bool have_click, have_poll, slow_pointer;
    uint32_t last_click, last_poll;
};

static void init(struct fixture *f) {
    *f = (struct fixture){0};
    tp_microtap_reset(&f->s);
}

/* Do not use core path/offset/timer fields as an admission oracle. The ledger
 * retains original input frames independently, including modular timestamps. */
static uint32_t magnitude(int32_t v) {
    return (uint32_t)(v < 0 ? -(int64_t)v : (int64_t)v);
}

static bool expected_cursor_band(const int16_t *xs, const int16_t *ys,
                                 unsigned n, bool slow) {
    if (n < TP_MICROTAP_MIN_FRAMES || n > TP_MICROTAP_MAX_FRAMES) { return false; }
    struct tp_pointer_state p = {0};
    int32_t x = 0, y = 0;
    uint64_t peak_sq = 0U;
    for (unsigned i = 0; i < n; i++) {
        if (xs[i] < INT8_MIN || xs[i] > INT8_MAX ||
            ys[i] < INT8_MIN || ys[i] > INT8_MAX) { return false; }
        int16_t px = 0, py = 0;
        tp_pointer_motion(&p, (int8_t)xs[i], (int8_t)ys[i], slow,
                          i * 10U, &px, &py);
        x += px; y += py;
        const int64_t xx = x, yy = y;
        const uint64_t d2 = (uint64_t)(xx * xx) + (uint64_t)(yy * yy);
        if (d2 > peak_sq) peak_sq = d2;
    }
    const int64_t xx = x, yy = y;
    const uint64_t end_sq = (uint64_t)(xx * xx) + (uint64_t)(yy * yy);
    const uint64_t min_sq = (uint64_t)TP_MICROTAP_MIN_CURSOR_DISTANCE_PX *
                            TP_MICROTAP_MIN_CURSOR_DISTANCE_PX;
    const uint64_t max_sq = (uint64_t)TP_MICROTAP_MAX_CURSOR_DISTANCE_PX *
                            TP_MICROTAP_MAX_CURSOR_DISTANCE_PX;
    return peak_sq <= max_sq && end_sq >= min_sq && end_sq <= max_sq;
}

static struct tp_microtap_result feed(struct fixture *f, uint32_t t,
                                      int16_t x, int16_t y, bool valid,
                                      bool enabled, bool blocked) {
    const uint32_t gap = (uint32_t)(t - f->last_poll);
    const bool bad = f->have_poll && gap > 100U;
    struct tp_microtap_result r = tp_microtap_step(&f->s, t, x, y,
                                                 f->slow_pointer, valid, enabled, blocked);
    steps++;
    CHECK(r.count <= 16U && f->s.count <= TP_MICROTAP_MAX_FRAMES);
    if (!valid || !enabled || blocked || bad) CHECK(!r.click);
    if (bad && gap >= UINT32_C(0x80000000)) f->have_click = false;
    f->have_poll = true;
    f->last_poll = t;
    if (r.click) {
        CHECK(f->n >= TP_MICROTAP_MIN_FRAMES && f->n <= TP_MICROTAP_MAX_FRAMES);
        int32_t sx = 0, sy = 0, cursor_x = 0, cursor_y = 0;
        uint32_t path = 0U, offset = 0U;
        uint64_t peak_cursor_sq = 0U;
        struct tp_pointer_state measure = {0};
        for (unsigned j = 0; j < f->n; j++) {
            sx += f->ledger[j].dx;
            sy += f->ledger[j].dy;
            path += magnitude(f->ledger[j].dx) + magnitude(f->ledger[j].dy);
            if (magnitude(sx) > offset) offset = magnitude(sx);
            if (magnitude(sy) > offset) offset = magnitude(sy);
            int16_t out_x = 0, out_y = 0;
            tp_pointer_motion(&measure, (int8_t)f->ledger[j].dx,
                              (int8_t)f->ledger[j].dy, f->slow_pointer,
                              f->ledger[j].at_ms, &out_x, &out_y);
            cursor_x += out_x;
            cursor_y += out_y;
            const int64_t cx = cursor_x, cy = cursor_y;
            const uint64_t d2 = (uint64_t)(cx * cx) + (uint64_t)(cy * cy);
            if (d2 > peak_cursor_sq) peak_cursor_sq = d2;
        }
        const int64_t cx = cursor_x, cy = cursor_y;
        const uint64_t distance_sq = (uint64_t)(cx * cx) + (uint64_t)(cy * cy);
        CHECK(path <= TP_MICROTAP_MAX_PATH && offset <= TP_MICROTAP_MAX_OFFSET);
        const uint64_t min_sq = (uint64_t)TP_MICROTAP_MIN_CURSOR_DISTANCE_PX *
                                TP_MICROTAP_MIN_CURSOR_DISTANCE_PX;
        const uint64_t max_sq = (uint64_t)TP_MICROTAP_MAX_CURSOR_DISTANCE_PX *
                                TP_MICROTAP_MAX_CURSOR_DISTANCE_PX;
        CHECK(distance_sq >= min_sq && distance_sq <= max_sq && peak_cursor_sq <= max_sq);
        CHECK((uint32_t)(t - f->ledger[f->n - 1U].at_ms) >= 120U);
        CHECK((uint32_t)(t - f->ledger[0].at_ms) <= TP_MICROTAP_MAX_CANDIDATE_MS);
        CHECK((uint32_t)(f->ledger[f->n - 1U].at_ms - f->ledger[0].at_ms) <= TP_MICROTAP_MAX_MOTION_SPAN_MS);
        if (f->have_click) CHECK((uint32_t)(t - f->last_click) >= TP_MICROTAP_CLICK_INTERVAL_MS);
        f->have_click = true;
        f->last_click = t;
        /* Only the OLD candidate can be discarded. Current motion is appended
         * afterwards, and must independently be replayed or remain pending. */
        for (unsigned j = 0; j < f->n; j++) {
            f->tx += f->ledger[j].dx;
            f->ty += f->ledger[j].dy;
        }
        f->discarded += f->n;
        f->clicks++;
        f->n = 0;
    }
    if (valid && (x != 0 || y != 0)) {
        CHECK(f->n < 16U);
        f->ledger[f->n++] = (struct tp_microtap_frame){t, x, y};
        f->input++;
        f->ix += x;
        f->iy += y;
    }
    for (unsigned i = 0; i < r.count; i++) {
        CHECK(f->n > 0U && r.frames[i].at_ms == f->ledger[0].at_ms);
        CHECK(r.frames[i].dx == f->ledger[0].dx && r.frames[i].dy == f->ledger[0].dy);
        f->rx += r.frames[i].dx;
        f->ry += r.frames[i].dy;
        f->replayed++;
        f->n--;
        for (unsigned j = 0; j < f->n; j++) f->ledger[j] = f->ledger[j + 1U];
    }
    CHECK(f->n == f->s.count && tp_microtap_pending(&f->s) == (f->n != 0U));
    int64_t px = 0, py = 0;
    for (unsigned j = 0; j < f->n; j++) {
        CHECK(f->s.frames[j].at_ms == f->ledger[j].at_ms);
        CHECK(f->s.frames[j].dx == f->ledger[j].dx && f->s.frames[j].dy == f->ledger[j].dy);
        px += f->ledger[j].dx;
        py += f->ledger[j].dy;
    }
    CHECK(f->input == f->replayed + f->discarded + f->n);
    CHECK(f->ix == f->rx + f->tx + px && f->iy == f->ry + f->ty + py);
    return r;
}

static struct tp_microtap_result packet(struct fixture *f, uint32_t t,
                                        int16_t x, int16_t y) {
    return feed(f, t, x, y, true, true, false);
}

/* Regular zero polls are essential: quiet120 > maximum allowed poll gap100. */
static void zeros(struct fixture *f, uint32_t base, unsigned n) {
    for (unsigned i = 1; i <= n; i++) (void)packet(f, base + i * 10U, 0, 0);
}

static void policy_constants(void) {
    CHECK(TP_MICROTAP_MIN_CURSOR_DISTANCE_PX == 3U &&
          TP_MICROTAP_MAX_CURSOR_DISTANCE_PX == 50U && TP_MICROTAP_QUIET_MS == 120U);
    CHECK(TP_MICROTAP_MAX_CANDIDATE_MS == 360U && TP_MICROTAP_CLICK_INTERVAL_MS == 300U);
    CHECK(TP_MICROTAP_MAX_PATH == 8192U && TP_MICROTAP_MAX_OFFSET == 4096U);
    CHECK(TP_MICROTAP_MIN_FRAMES == 1U && TP_MICROTAP_MAX_FRAMES == 8U);
    CHECK(TP_MICROTAP_MIN_MOTION_SPAN_MS == 0U && TP_MICROTAP_MAX_MOTION_SPAN_MS == 120U);
    CHECK(TP_MICROTAP_NAV_QUIET_MS == 200U && TP_MICROTAP_SWIPE_QUIET_MS == 300U &&
          TP_MICROTAP_MAX_POLL_GAP_MS == 100U);
    CHECK(TP_MICROTAP_FRAME_CAPACITY == 16U);
}

static void lower_bound_and_quiet(void) {
    struct fixture f;
    for (int16_t path = 1; path <= 80; path++) {
        init(&f);
        const int16_t xs[] = {path};
        const int16_t ys[] = {0};
        const bool expected = expected_cursor_band(xs, ys, 1U, false);
        (void)packet(&f, 0, path, 0);
        zeros(&f, 0, 12);
        CHECK(f.clicks == (expected ? 1U : 0U));
        CHECK(f.replayed == (expected ? 0U : 1U));
    }
    for (int16_t raw = 2; raw <= 50; raw++) {
        init(&f);
        const int16_t first = (int16_t)(raw / 2);
        const int16_t second = (int16_t)(raw - first);
        const int16_t xs[] = {first, second};
        const int16_t ys[] = {0, 0};
        const bool expected = expected_cursor_band(xs, ys, 2U, false);
        (void)packet(&f, 0, first, 0);
        (void)packet(&f, 10, second, 0);
        zeros(&f, 10, 12);
        CHECK(f.clicks == (expected ? 1U : 0U));
        CHECK(f.replayed == (expected ? 0U : 2U));
    }
    for (int16_t raw = 3; raw <= 50; raw++) {
        init(&f);
        const int16_t first = (int16_t)(raw / 3);
        const int16_t second = first;
        const int16_t third = (int16_t)(raw - first - second);
        const int16_t xs[] = {first, second, third};
        const int16_t ys[] = {0, 0, 0};
        const bool expected = expected_cursor_band(xs, ys, 3U, false);
        (void)packet(&f, 0, first, 0);
        (void)packet(&f, 10, second, 0);
        (void)packet(&f, 20, third, 0);
        zeros(&f, 20, 12);
        CHECK(f.clicks == (expected ? 1U : 0U));
        CHECK(f.replayed == (expected ? 0U : 3U));
    }
    for (int16_t raw = 2; raw <= 100; raw++) {
        init(&f);
        f.slow_pointer = true;
        const int16_t first = (int16_t)(raw / 2);
        const int16_t second = (int16_t)(raw - first);
        const int16_t xs[] = {first, second};
        const int16_t ys[] = {0, 0};
        const bool expected = expected_cursor_band(xs, ys, 2U, true);
        (void)packet(&f, 0, first, 0);
        (void)packet(&f, 10, second, 0);
        zeros(&f, 10, 12);
        CHECK(f.clicks == (expected ? 1U : 0U));
        CHECK(f.replayed == (expected ? 0U : 2U));
    }
    init(&f);
    (void)packet(&f, 0, 6, 6);
    (void)packet(&f, 10, 6, 6);
    zeros(&f, 10, 12);
    CHECK(f.clicks == 1U && f.discarded == 2U); /* ~19.8px diagonal enters the inferred band. */
    init(&f);
    (void)packet(&f, 0, 15, 15);
    (void)packet(&f, 10, 15, 15);
    zeros(&f, 10, 12);
    CHECK(f.clicks == 0U && f.replayed == 2U); /* Diagonal excursion >50px is passthrough. */
    init(&f);
    (void)packet(&f, 0, 20, 0);
    (void)packet(&f, 10, -20, 0);
    zeros(&f, 10, 12);
    CHECK(f.clicks == 0U && f.replayed == 2U); /* Opposing movement cancels net cursor displacement. */

    init(&f);
    (void)packet(&f, 0, 8, 0);
    (void)packet(&f, 10, 8, 0);
    (void)packet(&f, 20, 8, 0);
    (void)packet(&f, 120, 0, 0);
    (void)packet(&f, 139, 0, 0);
    CHECK(f.clicks == 0U && f.n == 3U);
    struct tp_microtap_result r = packet(&f, 140, 0, 0);
    CHECK(r.click && f.clicks == 1U && f.discarded == 3U);

    init(&f);
    (void)packet(&f, 0, -128, -128);
    (void)packet(&f, 10, -128, -128);
    (void)packet(&f, 20, -128, -128);
    zeros(&f, 20, 12);
    CHECK(f.clicks == 0U && f.replayed == 3U); /* Large diagonal is ordinary pointer motion. */
    init(&f);
    (void)packet(&f, 0, 127, 127);
    (void)packet(&f, 10, -128, 127);
    (void)packet(&f, 20, 127, -128);
    zeros(&f, 20, 12);
    CHECK(f.clicks == 0U && f.replayed == 3U); /* Large first cursor step enters passthrough. */
    init(&f);
    (void)packet(&f, 0, -128, 0);
    (void)packet(&f, 10, -128, 0);
    (void)packet(&f, 20, -128, 0);
    zeros(&f, 20, 12);
    CHECK(f.clicks == 0U && f.replayed == 3U); /* >50px displacement is a swipe. */

    init(&f);
    (void)packet(&f, 0, 8, 0);
    (void)packet(&f, 10, 8, 0);
    (void)packet(&f, 20, 8, 0);
    (void)packet(&f, 80, 0, 0);
    (void)packet(&f, 139, 0, 0);
    r = packet(&f, 140, 7, -2);
    CHECK(r.click && r.count == 1U && r.frames[0].at_ms == 140U);
    CHECK(f.discarded == 3U && f.replayed == 1U && f.n == 0U);
    zeros(&f, 140, 40);
    CHECK(f.clicks == 1U); /* New movement never becomes a deferred click. */
    puts("PASS inferred screen3..50px boundaries at normal/slow gain, single-frame/Euclidean net, >50px swipe replay, quiet119/120");
}

static void late_first_click(struct fixture *f, uint32_t decision) {
    init(f);
    (void)packet(f, 0, 8, 0);
    (void)packet(f, 20, 8, 0);
    (void)packet(f, 40, 8, 0);
    (void)packet(f, 100, 0, 0);
    (void)packet(f, 159, 0, 0); /* 119ms quiet; no earlier decision. */
    CHECK(f->clicks == 0U);
    (void)packet(f, decision, 0, 0);
}

static void frame_span_age_and_navigation(void) {
    struct fixture f;
    for (uint32_t span = 0; span <= 165U; span++) {
        init(&f);
        (void)packet(&f, 0, 8, 0);
        (void)packet(&f, span / 2U, 8, 0);
        (void)packet(&f, span, 8, 0);
        zeros(&f, span, 12);
        CHECK(f.clicks == (span <= 120U ? 1U : 0U));
        CHECK(f.discarded == (span <= 120U ? 3U : 0U));
    }
    init(&f);
    for (uint32_t i = 0; i < 8U; i++) (void)packet(&f, i * 10U, 3, 0);
    CHECK(f.s.count == 8U);
    zeros(&f, 70U, 12U);
    CHECK(f.clicks == 1U && f.discarded == 8U);
    init(&f);
    struct tp_microtap_result r = {0};
    for (uint32_t i = 0; i < 9U; i++) r = packet(&f, i * 10U, 3, 0);
    CHECK(r.count == 9U && f.replayed == 9U && f.n == 0U);
    for (unsigned i = 0; i < 9U; i++) CHECK(r.frames[i].at_ms == i * 10U);
    zeros(&f, 80U, 30U);
    CHECK(f.clicks == 0U);

    init(&f);
    (void)packet(&f, 0U, 8, 0);
    (void)packet(&f, 60U, 8, 0);
    (void)packet(&f, 120U, 8, 0);
    (void)packet(&f, 220U, 0, 0); /* 100ms poll gap, only 100ms quiet. */
    CHECK(f.clicks == 0U);
    (void)packet(&f, 320U, 0, 0); /* 120ms burst + 200ms to decision. */
    CHECK(f.clicks == 1U && f.discarded == 3U);
    init(&f);
    (void)packet(&f, 0U, 8, 0);
    (void)packet(&f, 40U, 8, 0);
    (void)packet(&f, 121U, 8, 0); /* 121ms burst is flushed immediately. */
    CHECK(f.clicks == 0U && f.replayed == 3U && f.n == 0U);

    for (uint32_t delta = 299; delta <= 300U; delta++) {
        init(&f);
        (void)packet(&f, 0, 600, 0); /* Immediate rejection starts the swipe guard. */
        zeros(&f, 0, 28U);
        (void)packet(&f, delta, 8, 0);
        (void)packet(&f, delta + 10U, 8, 0);
        (void)packet(&f, delta + 20U, 8, 0);
        zeros(&f, delta + 20U, 12U);
        CHECK(f.clicks == (delta == 300U ? 1U : 0U));
    }
    init(&f);
    (void)packet(&f, 0U, 60, 0); /* >50px swipe starts the longer rearm. */
    CHECK(f.replayed == 1U && f.s.mode == TP_MICROTAP_PASSTHROUGH && f.s.swipe_rearm);
    zeros(&f, 0U, 29U);
    (void)packet(&f, 299U, 3, 0); /* Small rubbing during the lockout is passthrough. */
    CHECK(f.clicks == 0U && f.replayed == 2U && f.s.swipe_rearm);
    zeros(&f, 299U, 29U);
    CHECK(f.clicks == 0U && f.s.mode == TP_MICROTAP_PASSTHROUGH);
    (void)packet(&f, 599U, 0, 0); /* Exact300ms quiet after the last swipe/rub motion. */
    CHECK(f.s.mode == TP_MICROTAP_IDLE && !f.s.swipe_rearm);
    (void)packet(&f, 600U, 3, 0);
    zeros(&f, 600U, 12U);
    CHECK(f.clicks == 1U && f.discarded == 1U && f.replayed == 2U);
    zeros(&f, 720U, 17U); /* Ordinary click rearm remains200ms after the swipe lockout. */
    (void)packet(&f, 900U, 3, 0);
    zeros(&f, 900U, 12U);
    CHECK(f.clicks == 2U); /* A second tap exactly300ms later still completes the double. */
    puts("PASS screen3..50px/min1, frames1..8/span120 boundaries, ordered ninth-frame replay; swipe rearm299/300; normal NAV200 and post-swipe double preserved");
}

static void repeat_guard_and_no_deferred_click(void) {
    struct fixture f;
    for (uint32_t spacing = 550; spacing <= 600U; spacing += 50U) {
        init(&f);
        for (uint32_t i = 0; i < 1000U; i++) {
            const uint32_t base = i * spacing;
            (void)packet(&f, base, 8, 0);
            (void)packet(&f, base + 10U, 8, 0);
            (void)packet(&f, base + 20U, 8, 0);
            zeros(&f, base + 20U, spacing / 10U - 3U);
        }
        CHECK(f.clicks == 1000U && f.discarded == 3000U && f.replayed == 0U);
    }
    /* No SDK/queue clock is involved. Use a late FIRST decision at180, not the
     * first motion at0: decision intervals299/300 must reject/admit exactly. */
    for (uint32_t delta = 299; delta <= 300U; delta++) {
        late_first_click(&f, 180);
        CHECK(f.clicks == 1U);
        const uint32_t start = 180U + delta - 140U;
        zeros(&f, 180, 15U); /* Reach near the next burst without stale poll gaps. */
        (void)packet(&f, start, 8, 0);
        (void)packet(&f, start + 10U, 8, 0);
        (void)packet(&f, start + 20U, 8, 0);
        (void)packet(&f, start + 120U, 0, 0);
        (void)packet(&f, start + 139U, 0, 0);
        CHECK(f.clicks == 1U);
        (void)packet(&f, start + 140U, 0, 0);
        const uint64_t expected = delta == 300U ? 2U : 1U;
        CHECK(f.clicks == expected && f.replayed == (delta == 300U ? 0U : 3U));
        zeros(&f, start + 140U, 40);
        CHECK(f.clicks == expected && f.n == 0U); /* Expiry cannot catch up. */
    }
    init(&f);
    for (uint32_t i = 0; i < 1000U; i++) {
        const uint32_t base = i * 70U;
        (void)packet(&f, base, 8, 0);
        (void)packet(&f, base + 10U, 8, 0);
        (void)packet(&f, base + 20U, 8, 0);
        zeros(&f, base + 20U, 4U);
    }
    CHECK(f.clicks == 0U && f.discarded == 0U && f.replayed == 3000U);
    uint32_t last = 999U * 70U + 20U;
    zeros(&f, last, 40U);
    CHECK(f.clicks == 0U && f.n == 0U); /* Repeating 70ms movement is passthrough. */

    init(&f);
    (void)packet(&f, 0U, 8, 0);
    (void)packet(&f, 10U, 8, 0);
    (void)packet(&f, 20U, 8, 0);
    zeros(&f, 20U, 12U);
    CHECK(f.clicks == 1U);
    zeros(&f, 140U, 6U);
    for (uint32_t i = 0; i < 1000U; i++) {
        const uint32_t base = 210U + i * 70U;
        (void)packet(&f, base, 8, -8);
        (void)packet(&f, base + 10U, 8, -8);
        (void)packet(&f, base + 20U, 8, -8);
        zeros(&f, base + 20U, 4U);
    }
    CHECK(f.clicks == 1U && f.discarded == 3U && f.replayed == 3000U);
    last = 210U + 999U * 70U + 20U;
    zeros(&f, last, 40U);
    CHECK(f.clicks == 1U && f.n == 0U);
    puts("PASS 1000 three-frame taps at550/600ms, decision299 rejects/300 admits, 1000 continuous70ms bursts zero clicks, native-repeat guard retained");
}

static void extremes_and_cancellation(void) {
    struct fixture f;
    for (int x = -128; x <= 127; x++) for (int y = -128; y <= 127; y++) {
        init(&f);
        for (uint32_t t = 0; t <= 20U; t += 10U)
            (void)packet(&f, t, (int16_t)x, (int16_t)y);
        zeros(&f, 20, 12);
        const int16_t xs[] = {(int16_t)x, (int16_t)x, (int16_t)x};
        const int16_t ys[] = {(int16_t)y, (int16_t)y, (int16_t)y};
        const bool expected = expected_cursor_band(xs, ys, 3U, false);
        CHECK(f.clicks == (expected ? 1U : 0U));
    }
    for (unsigned axis = 0; axis < 2U; axis++) {
        for (int32_t v = INT16_MIN; v <= INT16_MAX; v++) {
            init(&f);
            for (uint32_t t = 0; t <= 20U; t += 10U)
                (void)packet(&f, t, axis == 0U ? (int16_t)v : 0,
                                    axis == 1U ? (int16_t)v : 0);
            zeros(&f, 20, 12);
            const int16_t xs[] = {axis == 0U ? (int16_t)v : 0,
                                  axis == 0U ? (int16_t)v : 0,
                                  axis == 0U ? (int16_t)v : 0};
            const int16_t ys[] = {axis == 1U ? (int16_t)v : 0,
                                  axis == 1U ? (int16_t)v : 0,
                                  axis == 1U ? (int16_t)v : 0};
            const bool expected = expected_cursor_band(xs, ys, 3U, false);
            CHECK(f.clicks == (expected ? 1U : 0U));
        }
    }
    init(&f);
    (void)packet(&f, 0, -128, -128);
    (void)packet(&f, 10, -128, -128);
    (void)packet(&f, 20, INT16_MIN, INT16_MAX);
    CHECK(f.replayed == 3U && f.clicks == 0U);

    for (unsigned cause = 0; cause < 3U; cause++) {
        init(&f);
        (void)packet(&f, 0, 24, 0);
        (void)packet(&f, 60, 0, 0);
        (void)feed(&f, 120, 0, 0, cause != 0U, cause != 1U, cause == 2U);
        CHECK(f.clicks == 0U && f.replayed == 1U && f.n == 0U);
        zeros(&f, 120, 40);
        CHECK(f.clicks == 0U);
        init(&f);
        (void)packet(&f, 0, 2, 0);
        (void)packet(&f, 10, 2, 0);
        (void)packet(&f, 20, 2, 0);
        struct tp_microtap_result r = feed(&f, 30, INT16_MIN, INT16_MAX,
                                         cause != 0U, cause != 1U, cause == 2U);
        CHECK(r.count == (cause == 0U ? 3U : 4U)); /* Invalid payload is not motion. */
        CHECK(f.clicks == 0U && f.n == 0U);
    }
    /* Explicit default-disabled and native-blocked pure-motion transparency. */
    for (unsigned blocked = 0; blocked < 2U; blocked++) {
        init(&f);
        for (unsigned i = 0; i < 1000U; i++)
            (void)feed(&f, i * 10U, 1, -1, true, blocked != 0U, blocked != 0U);
        CHECK(f.replayed == 1000U && f.clicks == 0U && f.n == 0U);
    }
    puts("PASS all65536 signed8 XY, all65536 int16 values on BOTH axes, extremes, error/disabled/blocked ordered replay and raw-motion transparency");
}

static void clocks_and_poll_gaps(void) {
    struct fixture f;
    init(&f);
    (void)packet(&f, 0U, 8, 0);
    (void)packet(&f, 10U, 8, 0);
    (void)packet(&f, 20U, 8, 0);
    (void)packet(&f, 120U, 0, 0);
    CHECK(f.clicks == 0U && f.n == 3U);
    (void)packet(&f, 140U, 0, 0);
    CHECK(f.clicks == 1U); /* Gap100 is allowed, then quiet is observed. */
    for (uint32_t gap = 101; gap <= 120U; gap += 19U) {
        init(&f);
        (void)packet(&f, 0U, 24, 0);
        (void)packet(&f, gap, 0, 0);
        CHECK(f.clicks == 0U && f.replayed == 1U && f.n == 0U);
    }
    init(&f);
    (void)packet(&f, 0, 2, 0);
    (void)packet(&f, 10, 2, 0);
    (void)packet(&f, 20, 2, 0);
    struct tp_microtap_result r = packet(&f, 121, 6, 0);
    CHECK(!r.click && r.count == 4U && f.replayed == 4U);
    init(&f);
    (void)packet(&f, 100U, 6, 0);
    (void)packet(&f, 90U, 0, 0); /* Backwards clock starts both fail-closed guards. */
    CHECK(f.clicks == 0U && f.replayed == 1U);
    zeros(&f, 90U, 20U); /* Re-arm after the 200ms navigation quiet. */
    (void)packet(&f, 300U, 8, 0);
    (void)packet(&f, 310U, 8, 0);
    (void)packet(&f, 320U, 8, 0);
    (void)packet(&f, 420U, 0, 0);
    (void)packet(&f, 439U, 0, 0);
    (void)packet(&f, 440U, 0, 0);
    CHECK(f.clicks == 1U);
    init(&f);
    (void)packet(&f, 100U, 6, 0);
    const uint32_t jump = 100U + UINT32_C(0x80000000);
    (void)packet(&f, jump, INT16_MIN, INT16_MAX);
    CHECK(f.clicks == 0U && f.replayed == 2U);
    zeros(&f, jump, 40U);
    CHECK(f.clicks == 0U);
    init(&f);
    const uint32_t base = UINT32_MAX - 80U;
    (void)packet(&f, base, 8, 0);
    (void)packet(&f, base + 10U, 8, 0);
    (void)packet(&f, base + 20U, 8, 0);
    (void)packet(&f, base + 120U, 0, 0);
    (void)packet(&f, base + 139U, 0, 0);
    (void)packet(&f, base + 140U, 0, 0);
    (void)packet(&f, base + 240U, 0, 0);
    (void)packet(&f, base + 290U, 0, 0);
    (void)packet(&f, base + 300U, 8, 0);
    (void)packet(&f, base + 310U, 8, 0);
    (void)packet(&f, base + 320U, 8, 0);
    (void)packet(&f, base + 420U, 0, 0);
    (void)packet(&f, base + 439U, 0, 0);
    (void)packet(&f, base + 440U, 0, 0);
    CHECK(f.clicks == 2U && f.discarded == 6U);
    puts("PASS gap100 allowed, gap101/120 flush before quiet, rollback/half-range faults, guard299/300, expanded window across uint32 rollover");
}

static uint32_t rng = UINT32_C(0x31415926);
static uint32_t random_u32(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static void randomized_ledger(void) {
    struct fixture f;
    init(&f);
    uint32_t t = UINT32_MAX - 200U;
    for (unsigned i = 0; i < 100000U; i++) {
        const uint32_t r = random_u32();
        t += (r & 31U) + 1U;
        if (i % 997U == 0U) t += 101U;
        if (i % 7919U == 0U) t += UINT32_C(0x80000000);
        int16_t x = 0, y = 0;
        if ((r & 15U) == 0U) {
            x = (int16_t)((int32_t)((r >> 8) % 129U) - 64);
            y = (int16_t)((int32_t)((r >> 16) % 129U) - 64);
            if ((r & 2047U) == 256U) { x = INT16_MIN; y = INT16_MAX; }
        }
        (void)feed(&f, t, x, y, (r & UINT32_C(0xff00)) != UINT32_C(0x0100),
                   (r & UINT32_C(0xff0000)) != UINT32_C(0x020000),
                   (r & UINT32_C(0xff000000)) == UINT32_C(0x03000000));
    }
    (void)feed(&f, t + 1U, 0, 0, true, false, false); /* Drain, never discard. */
    CHECK(f.n == 0U && f.input == f.replayed + f.discarded);
    CHECK(f.clicks > 0U && f.replayed > 0U && f.discarded > 0U);
    printf("PASS ordered100000 random ledger: input=%" PRIu64 " replay=%" PRIu64
           " discarded=%" PRIu64 " clicks=%" PRIu64 "\n",
           f.input, f.replayed, f.discarded, f.clicks);
}

int main(void) {
    policy_constants();
    lower_bound_and_quiet();
    frame_span_age_and_navigation();
    repeat_guard_and_no_deferred_click();
    extremes_and_cancellation();
    clocks_and_poll_gaps();
    randomized_ledger();
    printf("PASS policy: %" PRIu64 " steps, %" PRIu64
           " checks; no physical tap accuracy claim\n", steps, checks);
    return 0;
}
