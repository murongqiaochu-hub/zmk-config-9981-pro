/* Pure shoulder FSM tests; no Zephyr, hardware, HID or behavior dependency.
 * Build: cc -std=c11 -Wall -Wextra -Wconversion -Werror -pedantic \
 *            -fsanitize=address,undefined -fno-omit-frame-pointer \
 *            tests/test_shoulder.c -o /tmp/test_shoulder
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_shoulder.h"

#define HISTORY_CAPACITY 512u

struct fixture {
    struct tp_shoulder_state state;
    struct tp_shoulder_edge history[HISTORY_CAPACITY];
    size_t edge_count;
    unsigned int toggles;
    bool host_down;
    bool enabled;
};

static unsigned int tests_run;
static size_t boundary_combinations;
static size_t invariant_steps;

static void setup(struct fixture *f) {
    *f = (struct fixture){0};
    tp_shoulder_reset(&f->state);
    assert(!tp_shoulder_busy(&f->state));
}

static struct tp_shoulder_result accept(struct fixture *f, struct tp_shoulder_result r) {
    assert(r.count <= TP_SHOULDER_OUTPUT_EDGES);
    assert(r.count <= 6u); /* Stronger implementation bound, including recovery. */
    if (r.toggle) {
        assert(r.count == 0u);
        assert(!f->host_down);
        assert(!tp_shoulder_busy(&f->state));
        ++f->toggles;
        f->enabled = !f->enabled;
    }
    for (uint8_t i = 0u; i < r.count; ++i) {
        /* Proves no orphan UP, duplicate DOWN, or collapsed click pairs. */
        assert(r.edges[i].pressed != f->host_down);
        f->host_down = r.edges[i].pressed;
        if (f->edge_count < HISTORY_CAPACITY) {
            f->history[f->edge_count] = r.edges[i];
        }
        ++f->edge_count;
    }
    assert(f->host_down == f->state.output_down);
    assert(tp_shoulder_valid(&f->state));
    assert(tp_shoulder_busy(&f->state) ==
           (f->state.buffered_count != 0u || f->state.down || f->state.output_down));
    return r;
}

static struct tp_shoulder_result edge(struct fixture *f, uint32_t now, bool pressed) {
    return accept(f, tp_shoulder_step(&f->state, now, true, pressed));
}

static struct tp_shoulder_result poll_at(struct fixture *f, uint32_t now) {
    return accept(f, tp_shoulder_step(&f->state, now, false, false));
}

static struct tp_shoulder_result flush_at(struct fixture *f, uint32_t now) {
    return accept(f, tp_shoulder_flush(&f->state, now));
}

static void empty(struct tp_shoulder_result r) {
    assert(r.count == 0u && !r.toggle);
}

static void count_is(struct tp_shoulder_result r, uint8_t count) {
    assert(r.count == count && !r.toggle);
}

static void toggle_is(struct tp_shoulder_result r) {
    assert(r.count == 0u && r.toggle);
}

static void at(const struct fixture *f, size_t index, uint32_t now, bool pressed) {
    assert(index < f->edge_count && index < HISTORY_CAPACITY);
    assert(f->history[index].at_ms == now);
    assert(f->history[index].pressed == pressed);
}

static void buffered_tap(struct fixture *f, uint32_t press, uint32_t release) {
    empty(edge(f, press, true));
    assert(tp_shoulder_busy(&f->state));
    empty(edge(f, release, false));
    assert(tp_shoulder_busy(&f->state));
}

static void triple(struct fixture *f, uint32_t base) {
    buffered_tap(f, base, base + 10u);
    buffered_tap(f, base + 20u, base + 30u);
    empty(edge(f, base + 40u, true));
    toggle_is(edge(f, base + 50u, false));
    assert(!tp_shoulder_busy(&f->state));
}

static void test_boot_off_and_orphan_release(void) {
    struct fixture f; setup(&f);
    assert(!f.enabled && f.toggles == 0u);
    empty(poll_at(&f, 0u));
    empty(edge(&f, 0u, false));
    empty(edge(&f, 1u, false));
    assert(f.edge_count == 0u && !f.host_down);
}

static void test_single_timeout(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 140u);
    empty(poll_at(&f, 359u));
    count_is(poll_at(&f, 360u), 2u);
    at(&f, 0u, 100u, true); at(&f, 1u, 140u, false);
    assert(!tp_shoulder_busy(&f.state) && !f.enabled);
    empty(poll_at(&f, 361u));
}

static void test_double_timeout(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 130u);
    buffered_tap(&f, 350u, 400u);
    empty(poll_at(&f, 619u));
    count_is(poll_at(&f, 620u), 4u);
    at(&f, 0u, 100u, true); at(&f, 1u, 130u, false);
    at(&f, 2u, 350u, true); at(&f, 3u, 400u, false);
    assert(!tp_shoulder_busy(&f.state) && f.toggles == 0u);
}

static void test_triple_consumes_all_edges(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    buffered_tap(&f, 200u, 210u);
    empty(edge(&f, 300u, true));
    assert(!f.enabled && f.toggles == 0u && tp_shoulder_busy(&f.state));
    toggle_is(edge(&f, 310u, false));
    assert(f.enabled && f.toggles == 1u && f.edge_count == 0u);
    empty(poll_at(&f, 1000u));
}

static void test_inclusive_hold_and_gap_limits(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 280u);
    buffered_tap(&f, 500u, 680u);
    empty(edge(&f, 900u, true));
    toggle_is(edge(&f, 1080u, false));
    assert(f.edge_count == 0u && f.toggles == 1u);
}

static void held_tick(unsigned int ordinal) {
    struct fixture f; setup(&f);
    uint32_t press = 100u;
    for (unsigned int i = 1u; i < ordinal; ++i) {
        buffered_tap(&f, press, press + 30u);
        press += 100u;
    }
    empty(edge(&f, press, true));
    empty(poll_at(&f, press + 180u));
    const uint8_t expected = (uint8_t)(2u * ordinal - 1u);
    count_is(poll_at(&f, press + 181u), expected);
    assert(f.host_down && tp_shoulder_busy(&f.state));
    empty(poll_at(&f, press + 250u));
    count_is(edge(&f, press + 300u, false), 1u);
    assert(!f.host_down && !tp_shoulder_busy(&f.state));
    assert(f.toggles == 0u && f.edge_count == (size_t)(2u * ordinal));
    for (unsigned int i = 0u; i + 1u < ordinal; ++i) {
        at(&f, (size_t)(2u * i), 100u + i * 100u, true);
        at(&f, (size_t)(2u * i + 1u), 130u + i * 100u, false);
    }
    at(&f, (size_t)(2u * ordinal - 2u), press, true);
    at(&f, (size_t)(2u * ordinal - 1u), press + 300u, false);
    /* The pre-hold tap count is destroyed, even when this was the third DOWN. */
    buffered_tap(&f, press + 400u, press + 410u);
    buffered_tap(&f, press + 420u, press + 430u);
    count_is(poll_at(&f, press + 650u), 4u);
    assert(f.toggles == 0u);
}

static void test_hold_first_tick(void) { held_tick(1u); }
static void test_hold_second_tick(void) { held_tick(2u); }
static void test_hold_third_tick(void) { held_tick(3u); }

static void held_release(unsigned int ordinal) {
    struct fixture f; setup(&f);
    uint32_t press = 100u;
    for (unsigned int i = 1u; i < ordinal; ++i) {
        buffered_tap(&f, press, press + 30u);
        press += 100u;
    }
    empty(edge(&f, press, true));
    count_is(edge(&f, press + 181u, false), (uint8_t)(2u * ordinal));
    assert(f.toggles == 0u && !f.host_down && !tp_shoulder_busy(&f.state));
    at(&f, (size_t)(2u * ordinal - 2u), press, true);
    at(&f, (size_t)(2u * ordinal - 1u), press + 181u, false);
}

static void test_hold_first_release_without_poll(void) { held_release(1u); }
static void test_hold_second_release_without_poll(void) { held_release(2u); }
static void test_hold_third_release_without_poll(void) { held_release(3u); }

static void test_hold_180_release_is_tap(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    empty(poll_at(&f, 280u));
    empty(edge(&f, 280u, false));
    count_is(poll_at(&f, 500u), 2u);
    at(&f, 0u, 100u, true); at(&f, 1u, 280u, false);
}

static void test_long_drag_release(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    count_is(poll_at(&f, 1000000000u), 1u);
    empty(poll_at(&f, 1000000010u));
    count_is(edge(&f, 1000000020u, false), 1u);
    at(&f, 0u, 100u, true); at(&f, 1u, 1000000020u, false);
    assert(!f.host_down && f.toggles == 0u);
}

static void test_duplicate_press_does_not_extend_hold(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    empty(edge(&f, 150u, true));
    empty(edge(&f, 280u, true));
    count_is(edge(&f, 281u, true), 1u);
    count_is(edge(&f, 300u, false), 1u);
    empty(edge(&f, 301u, false));
    assert(f.edge_count == 2u);
    at(&f, 0u, 100u, true); at(&f, 1u, 300u, false);
}

static void test_duplicate_release_does_not_extend_gap(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    empty(edge(&f, 200u, false));
    count_is(edge(&f, 330u, false), 2u);
    assert(!tp_shoulder_busy(&f.state));
}

static void test_duplicate_edges_inside_triple(void) {
    struct fixture f; setup(&f);
    for (uint32_t base = 100u; base <= 300u; base += 100u) {
        empty(edge(&f, base, true));
        empty(edge(&f, base + 1u, true));
        if (base == 300u) {
            toggle_is(edge(&f, base + 10u, false));
        } else {
            empty(edge(&f, base + 10u, false));
        }
        empty(edge(&f, base + 11u, false));
    }
    assert(f.edge_count == 0u && f.toggles == 1u);
}

static void test_gap_219_triple(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    buffered_tap(&f, 329u, 339u);
    empty(edge(&f, 558u, true));
    toggle_is(edge(&f, 568u, false));
}

static void test_gap_221_starts_new_sequence(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    count_is(edge(&f, 331u, true), 2u);
    empty(edge(&f, 341u, false));
    buffered_tap(&f, 400u, 410u);
    empty(edge(&f, 450u, true));
    toggle_is(edge(&f, 460u, false));
    assert(f.edge_count == 2u && f.toggles == 1u);
    at(&f, 0u, 100u, true); at(&f, 1u, 110u, false);
}

static void test_gap_220_poll_precedes_press(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    count_is(poll_at(&f, 330u), 2u);
    /* Already-returned edges cannot be retroactively consumed. */
    buffered_tap(&f, 330u, 340u);
    buffered_tap(&f, 350u, 360u);
    count_is(poll_at(&f, 580u), 4u);
    assert(f.edge_count == 6u && f.toggles == 0u);
}

static void test_gap_220_press_precedes_poll(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    empty(poll_at(&f, 329u));
    empty(edge(&f, 330u, true));
    empty(poll_at(&f, 330u));
    empty(edge(&f, 340u, false));
    empty(edge(&f, 560u, true));
    toggle_is(edge(&f, 570u, false));
    assert(f.edge_count == 0u);
}

static void test_idle_forward_gap_flushes(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    count_is(poll_at(&f, 1000000u), 2u);
    assert(f.toggles == 0u && !tp_shoulder_busy(&f.state));
}

static void test_flush_single(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    count_is(flush_at(&f, 111u), 2u);
    empty(flush_at(&f, 111u));
    assert(!tp_shoulder_busy(&f.state));
    triple(&f, 120u);
    assert(f.edge_count == 2u && f.toggles == 1u);
}

static void test_flush_double(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    buffered_tap(&f, 200u, 210u);
    count_is(flush_at(&f, 211u), 4u);
    empty(flush_at(&f, 212u));
    assert(f.toggles == 0u && !tp_shoulder_busy(&f.state));
    at(&f, 2u, 200u, true); at(&f, 3u, 210u, false);
}

static void flushed_hold(unsigned int ordinal) {
    struct fixture f; setup(&f);
    uint32_t press = 100u;
    for (unsigned int i = 1u; i < ordinal; ++i) {
        buffered_tap(&f, press, press + 10u);
        press += 100u;
    }
    empty(edge(&f, press, true));
    count_is(flush_at(&f, press + 1u), (uint8_t)(2u * ordinal - 1u));
    assert(f.host_down && tp_shoulder_busy(&f.state));
    empty(flush_at(&f, press + 2u));
    empty(poll_at(&f, press + 3u));
    /* Even a short interrupted third hold MUST NOT toggle. */
    count_is(edge(&f, press + 10u, false), 1u);
    assert(f.toggles == 0u && !f.host_down && !tp_shoulder_busy(&f.state));
    triple(&f, press + 20u);
    assert(f.toggles == 1u && f.edge_count == (size_t)(2u * ordinal));
}

static void test_flush_first_hold(void) { flushed_hold(1u); }
static void test_flush_second_hold(void) { flushed_hold(2u); }
static void test_flush_third_hold(void) { flushed_hold(3u); }

static void test_flush_idle_never_invents_up(void) {
    struct fixture f; setup(&f);
    empty(flush_at(&f, 0u));
    empty(flush_at(&f, UINT32_MAX));
    empty(edge(&f, 0u, false));
    assert(f.edge_count == 0u && !tp_shoulder_busy(&f.state));
}

static void test_reset_preserves_emitted_down(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    count_is(poll_at(&f, 281u), 1u);
    tp_shoulder_reset(&f.state);
    tp_shoulder_reset(&f.state);
    assert(tp_shoulder_busy(&f.state) && f.state.output_down);
    empty(poll_at(&f, 500u));
    count_is(edge(&f, 600u, false), 1u);
    at(&f, 1u, 600u, false);
    assert(!f.host_down && !tp_shoulder_busy(&f.state));
}

static void test_reset_buffered_hold_suppresses_orphan_up(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    tp_shoulder_reset(&f.state);
    assert(tp_shoulder_busy(&f.state) && !f.state.output_down);
    empty(flush_at(&f, 110u));
    empty(edge(&f, 120u, false));
    assert(f.edge_count == 0u && !tp_shoulder_busy(&f.state));
    triple(&f, 130u);
}

static void test_reset_completed_candidate(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    buffered_tap(&f, 120u, 130u);
    tp_shoulder_reset(&f.state);
    assert(!tp_shoulder_busy(&f.state));
    buffered_tap(&f, 140u, 150u);
    count_is(poll_at(&f, 370u), 2u);
    assert(f.toggles == 0u && f.edge_count == 2u);
}

static void test_reset_repairs_unknown_down_bookkeeping(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    count_is(flush_at(&f, 101u), 1u);
    f.state.down = false; /* Unknown recognition state, known returned DOWN. */
    tp_shoulder_reset(&f.state);
    assert(f.state.down && f.state.output_down);
    count_is(edge(&f, 110u, false), 1u);
    assert(!f.host_down);
}

static void test_invalid_candidate_preserves_emitted_down(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    count_is(flush_at(&f, 101u), 1u);
    f.state.buffered_count = UINT8_MAX;
    f.state.tap_count = UINT8_MAX;
    count_is(edge(&f, 110u, false), 1u);
    assert(!f.host_down && f.toggles == 0u);
}

static void test_invalid_buffer_does_not_emit_orphan_up(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    f.state.buffered_count = UINT8_MAX;
    empty(edge(&f, 110u, false));
    assert(f.edge_count == 0u);
    buffered_tap(&f, 120u, 130u);
    f.state.buffered[0].pressed = false;
    empty(flush_at(&f, 131u));
    assert(!tp_shoulder_busy(&f.state));
    triple(&f, 140u);
}

static void test_rollback_during_hold(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    count_is(poll_at(&f, 90u), 1u);
    count_is(edge(&f, 95u, false), 1u);
    at(&f, 0u, 100u, true); at(&f, 1u, 95u, false);
    assert(f.toggles == 0u && !f.host_down);
}

static void test_rollback_after_double(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    buffered_tap(&f, 200u, 210u);
    count_is(poll_at(&f, 205u), 4u);
    buffered_tap(&f, 220u, 230u);
    buffered_tap(&f, 240u, 250u);
    count_is(poll_at(&f, 470u), 4u);
    assert(f.edge_count == 8u && f.toggles == 0u);
}

static void test_rollback_third_release_never_toggles(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    buffered_tap(&f, 120u, 130u);
    empty(edge(&f, 140u, true));
    count_is(edge(&f, 139u, false), 6u);
    at(&f, 4u, 140u, true); at(&f, 5u, 139u, false);
    assert(f.toggles == 0u && !f.host_down);
}

static void test_rollback_new_press_is_passthrough(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    count_is(edge(&f, 105u, true), 3u);
    count_is(edge(&f, 106u, false), 1u);
    at(&f, 0u, 100u, true); at(&f, 1u, 110u, false);
    at(&f, 2u, 105u, true); at(&f, 3u, 106u, false);
    triple(&f, 120u);
    assert(f.toggles == 1u);
}

static void test_half_range_jump_is_failsafe(void) {
    struct fixture f; setup(&f);
    empty(edge(&f, 100u, true));
    const uint32_t jumped = 100u + TP_SHOULDER_CLOCK_HALF_RANGE;
    count_is(poll_at(&f, jumped), 1u);
    count_is(edge(&f, jumped + 1u, false), 1u);
    assert(f.toggles == 0u && !f.host_down);
}

static void test_forward_gap_release_is_failsafe(void) {
    struct fixture f; setup(&f);
    buffered_tap(&f, 100u, 110u);
    buffered_tap(&f, 120u, 130u);
    empty(edge(&f, 140u, true));
    count_is(edge(&f, 1000000u, false), 6u);
    assert(f.toggles == 0u && !f.host_down);
}

static void test_wrap_triple(void) {
    struct fixture f; setup(&f);
    const uint32_t base = UINT32_MAX - 50u;
    buffered_tap(&f, base, base + 20u);
    buffered_tap(&f, base + 60u, base + 80u);
    empty(edge(&f, base + 120u, true));
    toggle_is(edge(&f, base + 140u, false));
    assert(f.edge_count == 0u && f.toggles == 1u);
}

static void test_wrap_hold_tick(void) {
    struct fixture f; setup(&f);
    const uint32_t base = UINT32_MAX - 100u;
    empty(edge(&f, base, true));
    empty(poll_at(&f, base + 180u));
    count_is(poll_at(&f, base + 181u), 1u);
    count_is(edge(&f, base + 200u, false), 1u);
    at(&f, 0u, base, true); at(&f, 1u, base + 200u, false);
}

static void test_wrap_gap_timeout(void) {
    struct fixture f; setup(&f);
    const uint32_t base = UINT32_MAX - 30u;
    buffered_tap(&f, base, base + 20u);
    empty(poll_at(&f, base + 239u));
    count_is(poll_at(&f, base + 240u), 2u);
    at(&f, 0u, base, true); at(&f, 1u, base + 20u, false);
}

static void test_wrap_exact_hold_gap_limits(void) {
    struct fixture f; setup(&f);
    const uint32_t base = UINT32_MAX - 500u;
    buffered_tap(&f, base, base + 180u);
    buffered_tap(&f, base + 400u, base + 580u);
    empty(edge(&f, base + 800u, true));
    toggle_is(edge(&f, base + 980u, false));
    assert(f.edge_count == 0u);
}

static void test_reentrant_continuous_triples(void) {
    struct fixture f; setup(&f);
    for (uint32_t i = 0u; i < 64u; ++i) {
        triple(&f, 100u + i * 60u);
        assert(f.enabled == ((i % 2u) == 0u));
    }
    assert(!f.enabled && f.toggles == 64u && f.edge_count == 0u);
}

static void test_triple_followed_by_single_and_double(void) {
    struct fixture f; setup(&f);
    triple(&f, 100u);
    buffered_tap(&f, 160u, 170u);
    count_is(poll_at(&f, 390u), 2u);
    buffered_tap(&f, 400u, 410u);
    buffered_tap(&f, 420u, 430u);
    count_is(poll_at(&f, 650u), 4u);
    assert(f.edge_count == 6u && f.toggles == 1u && f.enabled);
}

static void test_busy_lifecycle(void) {
    struct fixture f; setup(&f);
    assert(!tp_shoulder_busy(&f.state));
    empty(edge(&f, 100u, true));
    assert(tp_shoulder_busy(&f.state));
    empty(edge(&f, 110u, false));
    assert(tp_shoulder_busy(&f.state));
    count_is(flush_at(&f, 111u), 2u);
    assert(!tp_shoulder_busy(&f.state));
    empty(edge(&f, 120u, true));
    count_is(flush_at(&f, 121u), 1u);
    assert(tp_shoulder_busy(&f.state));
    count_is(edge(&f, 122u, false), 1u);
    assert(!tp_shoulder_busy(&f.state));
    triple(&f, 130u);
}

static void test_poll_ignores_pressed_argument(void) {
    struct fixture f; setup(&f);
    empty(accept(&f, tp_shoulder_step(&f.state, 0u, false, true)));
    assert(!tp_shoulder_busy(&f.state));
    empty(edge(&f, 100u, true));
    count_is(accept(&f, tp_shoulder_step(&f.state, 281u, false, true)), 1u);
    assert(f.host_down && f.state.down);
    count_is(edge(&f, 300u, false), 1u);
}

/* Every 3-click boundary combination: 0/1/179/180/181ms holds and
 * 0/219/220/221ms gaps. Non-triples must preserve all six original edges.
 */
static void test_exhaustive_boundaries(void) {
    const uint32_t holds[] = {0u, 1u, 179u, 180u, 181u};
    const uint32_t gaps[] = {0u, 219u, 220u, 221u};
    for (size_t a = 0u; a < 5u; ++a) {
        for (size_t b = 0u; b < 5u; ++b) {
            for (size_t c = 0u; c < 5u; ++c) {
                for (size_t d = 0u; d < 4u; ++d) {
                    for (size_t e = 0u; e < 4u; ++e) {
                        struct fixture f; setup(&f);
                        const uint32_t times[] = {
                            10u,
                            10u + holds[a],
                            10u + holds[a] + gaps[d],
                            10u + holds[a] + gaps[d] + holds[b],
                            10u + holds[a] + gaps[d] + holds[b] + gaps[e],
                            10u + holds[a] + gaps[d] + holds[b] + gaps[e] + holds[c],
                        };
                        for (size_t i = 0u; i < 6u; ++i) {
                            (void)edge(&f, times[i], (i % 2u) == 0u);
                        }
                        (void)poll_at(&f, times[5] + TP_SHOULDER_GAP_MS);
                        const bool consumed = holds[a] <= TP_SHOULDER_TAP_MS &&
                            holds[b] <= TP_SHOULDER_TAP_MS && holds[c] <= TP_SHOULDER_TAP_MS &&
                            gaps[d] <= TP_SHOULDER_GAP_MS && gaps[e] <= TP_SHOULDER_GAP_MS;
                        assert(f.toggles == (consumed ? 1u : 0u));
                        assert(f.enabled == consumed);
                        assert(f.edge_count == (consumed ? 0u : 6u));
                        assert(!f.host_down && !tp_shoulder_busy(&f.state));
                        if (!consumed) {
                            for (size_t i = 0u; i < 6u; ++i) {
                                at(&f, i, times[i], (i % 2u) == 0u);
                            }
                        }
                        ++boundary_combinations;
                    }
                }
            }
        }
    }
}

static uint32_t random32(uint32_t *seed) {
    uint32_t value = *seed;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *seed = value;
    return value;
}

/* Deterministic adversarial streams exercise repeats, arbitrary timer polls,
 * flush/reset interleavings, rollback, wrap and ambiguous half-range jumps.
 * The accept() monitor checks output ownership and bounded state on EVERY call.
 */
static void test_randomized_invariants(void) {
    const uint32_t seeds[] = {1u, UINT32_C(0x12345678), UINT32_C(0xdeadbeef), UINT32_MAX};
    for (size_t s = 0u; s < 4u; ++s) {
        struct fixture f; setup(&f);
        uint32_t seed = seeds[s];
        uint32_t now = UINT32_MAX - 100u;
        for (size_t i = 0u; i < 50000u; ++i) {
            const uint32_t value = random32(&seed);
            if ((value & 255u) == 0u) {
                now -= 3u;
            } else if ((value & 255u) == 1u) {
                now += TP_SHOULDER_CLOCK_HALF_RANGE;
            } else {
                now += (value >> 8u) % 301u;
            }
            switch ((value >> 20u) % 16u) {
            case 0u:
            case 1u:
            case 2u:
            case 3u:
                (void)edge(&f, now, true);
                break;
            case 4u:
            case 5u:
            case 6u:
            case 7u:
                (void)edge(&f, now, false);
                break;
            case 8u:
            case 9u:
            case 10u:
            case 11u:
                (void)poll_at(&f, now);
                break;
            case 12u:
            case 13u:
                (void)flush_at(&f, now);
                break;
            default:
                tp_shoulder_reset(&f.state);
                assert(f.host_down == f.state.output_down);
                assert(tp_shoulder_valid(&f.state));
                break;
            }
            ++invariant_steps;
        }
        (void)flush_at(&f, now + 1u);
        (void)edge(&f, now + 2u, false);
        invariant_steps += 2u;
        assert(!f.host_down && !tp_shoulder_busy(&f.state));
    }
}

#define RUN(test) do { test(); ++tests_run; } while (0)

int main(void) {
    RUN(test_boot_off_and_orphan_release);
    RUN(test_single_timeout);
    RUN(test_double_timeout);
    RUN(test_triple_consumes_all_edges);
    RUN(test_inclusive_hold_and_gap_limits);
    RUN(test_hold_first_tick);
    RUN(test_hold_second_tick);
    RUN(test_hold_third_tick);
    RUN(test_hold_first_release_without_poll);
    RUN(test_hold_second_release_without_poll);
    RUN(test_hold_third_release_without_poll);
    RUN(test_hold_180_release_is_tap);
    RUN(test_long_drag_release);
    RUN(test_duplicate_press_does_not_extend_hold);
    RUN(test_duplicate_release_does_not_extend_gap);
    RUN(test_duplicate_edges_inside_triple);
    RUN(test_gap_219_triple);
    RUN(test_gap_221_starts_new_sequence);
    RUN(test_gap_220_poll_precedes_press);
    RUN(test_gap_220_press_precedes_poll);
    RUN(test_idle_forward_gap_flushes);
    RUN(test_flush_single);
    RUN(test_flush_double);
    RUN(test_flush_first_hold);
    RUN(test_flush_second_hold);
    RUN(test_flush_third_hold);
    RUN(test_flush_idle_never_invents_up);
    RUN(test_reset_preserves_emitted_down);
    RUN(test_reset_buffered_hold_suppresses_orphan_up);
    RUN(test_reset_completed_candidate);
    RUN(test_reset_repairs_unknown_down_bookkeeping);
    RUN(test_invalid_candidate_preserves_emitted_down);
    RUN(test_invalid_buffer_does_not_emit_orphan_up);
    RUN(test_rollback_during_hold);
    RUN(test_rollback_after_double);
    RUN(test_rollback_third_release_never_toggles);
    RUN(test_rollback_new_press_is_passthrough);
    RUN(test_half_range_jump_is_failsafe);
    RUN(test_forward_gap_release_is_failsafe);
    RUN(test_wrap_triple);
    RUN(test_wrap_hold_tick);
    RUN(test_wrap_gap_timeout);
    RUN(test_wrap_exact_hold_gap_limits);
    RUN(test_reentrant_continuous_triples);
    RUN(test_triple_followed_by_single_and_double);
    RUN(test_busy_lifecycle);
    RUN(test_poll_ignores_pressed_argument);
    RUN(test_exhaustive_boundaries);
    RUN(test_randomized_invariants);
    printf("shoulder: %u test groups, %zu boundary combinations, %zu invariant steps passed\n",
           tests_run, boundary_combinations, invariant_steps);
    return 0;
}
