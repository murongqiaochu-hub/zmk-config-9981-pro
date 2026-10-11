/* Pure click-output contract tests; no hardware/Zephyr/SDK or tap.h dependency.
 * Build: cc -std=c11 -Wall -Wextra -Werror -pedantic tests/test_tap_click.c
 * Use an anonymous memfd output when testing without an on-disk executable.
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_click.h"

enum { LEFT = 1, RIGHT = 2 };
struct packet {
    uint32_t epoch, at;
    unsigned int buttons, sequence;
    int x, y, scroll_x, scroll_y;
};
struct fake {
    struct tp_click state;
    struct tp_click_ops ops;
    uint32_t epoch, now;
    int ready_result, left_state_result;
    unsigned int foreign_left, foreign_right, own;
    unsigned int press_failures, release_failures, send_failures;
    unsigned int press_calls, release_calls, send_calls, ready_calls;
    unsigned int accepted, fifo_count, fifo_capacity, sequence;
    unsigned int last_press_sequence, last_release_sequence;
    unsigned int flushes;
    bool physical_left, physical_right, switch_after_press;
    int stale_x, stale_y, stale_scroll;
    struct packet history[128];
};

static unsigned int buttons(const struct fake *f) {
    return ((f->own || f->foreign_left || f->physical_left) ? LEFT : 0u) |
           ((f->foreign_right || f->physical_right) ? RIGHT : 0u);
}
static int ready(void *ctx, uint32_t *epoch) {
    struct fake *f = ctx;
    f->ready_calls++;
    if (f->ready_result) return f->ready_result;
    *epoch = f->epoch;
    return 0;
}
static int left_busy(void *ctx, bool ours, bool *busy) {
    struct fake *f = ctx;
    assert((f->own != 0u) == ours);
    if (f->left_state_result) return f->left_state_result;
    *busy = f->physical_left || f->foreign_left != 0u;
    return 0;
}
static int press_ref(void *ctx) {
    struct fake *f = ctx;
    f->press_calls++;
    if (f->press_failures) { f->press_failures--; return -1; }
    assert(f->own == 0u);
    f->own++;
    f->last_press_sequence = ++f->sequence;
    if (f->switch_after_press) { f->epoch++; f->switch_after_press = false; }
    return 0;
}
static int release_ref(void *ctx) {
    struct fake *f = ctx;
    f->release_calls++;
    assert(f->own == 1u); /* no release of somebody else's token */
    if (f->release_failures) { f->release_failures--; return -1; }
    f->own--;
    f->last_release_sequence = ++f->sequence;
    return 0;
}
static int send_buttons(void *ctx, uint32_t expected_epoch) {
    struct fake *f = ctx;
    f->send_calls++;
    if (f->ready_result || f->epoch != expected_epoch) return -1;
    if (f->send_failures) { f->send_failures--; return -1; }
    if (f->fifo_count >= f->fifo_capacity) return -1; /* NEVER evict oldest */
    assert(f->accepted < sizeof(f->history) / sizeof(f->history[0]));
    f->history[f->accepted++] = (struct packet){
        .epoch = f->epoch, .at = f->now, .buttons = buttons(f),
        .sequence = ++f->sequence, .x = 0, .y = 0, .scroll_x = 0, .scroll_y = 0,
    };
    f->fifo_count++;
    return 0; /* FIFO acceptance ONLY: no host ACK is modeled */
}
static void setup(struct fake *f) {
    memset(f, 0, sizeof(*f));
    tp_click_init(&f->state);
    f->epoch = 7u;
    f->fifo_capacity = 128u;
    f->stale_x = 42; f->stale_y = -17; f->stale_scroll = 9;
    f->ops = (struct tp_click_ops){ f, ready, left_busy, press_ref, release_ref, send_buttons };
}
static enum tp_click_result request(struct fake *f, uint32_t now, bool enabled) {
    f->now = now;
    enum tp_click_result ret = tp_click_request(&f->state, &f->ops, now, enabled);
    assert((f->own != 0u) == f->state.owns_left);
    return ret;
}
static enum tp_click_result poll(struct fake *f, uint32_t now, bool enabled) {
    f->now = now;
    unsigned int sends_before = f->send_calls;
    enum tp_click_result ret = tp_click_poll(&f->state, &f->ops, now, enabled);
    assert(f->send_calls - sends_before <= 1u); /* nonbusy-loop bound */
    assert((f->own != 0u) == f->state.owns_left);
    return ret;
}
static void check_packet(const struct fake *f, unsigned int n, unsigned int expected) {
    assert(n < f->accepted);
    const struct packet *p = &f->history[n];
    assert(p->buttons == expected);
    assert(p->x == 0 && p->y == 0 && p->scroll_x == 0 && p->scroll_y == 0);
}
static void resync(struct fake *f) {
    assert(!f->state.owns_left);
    /* Adapter proof: old FIFO discarded/quarantined, THEN current state queued. */
    f->fifo_count = 0;
    f->flushes++;
    assert(send_buttons(f, f->epoch) == 0);
    assert(tp_click_ack_resync(&f->state, &f->ops,
        (struct tp_click_resync_ack){f->epoch, true, true}) == TP_CLICK_OK);
}

static void test_fifo_hold_and_exact_release(void) {
    struct fake f; setup(&f);
    assert(request(&f, 100, true) == TP_CLICK_OK);
    check_packet(&f, 0, LEFT);
    assert(f.last_press_sequence < f.history[0].sequence);
    assert(poll(&f, 100, true) == TP_CLICK_BUSY);
    assert(poll(&f, 111, true) == TP_CLICK_BUSY);
    assert(f.accepted == 1 && f.release_calls == 0);
    assert(poll(&f, 112, true) == TP_CLICK_OK);
    check_packet(&f, 1, 0);
    assert(f.history[0].at == 100 && f.history[1].at == 112);
    assert(f.history[0].sequence < f.last_release_sequence);
    assert(f.last_release_sequence < f.history[1].sequence);
    assert(f.press_calls == 1 && f.release_calls == 1);
    for (uint32_t i = 113; i < 200; i++) assert(poll(&f, i, true) == TP_CLICK_OK);
    assert(f.accepted == 2 && f.release_calls == 1);
}
static void test_duplicate_request_not_buffered(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    for (uint32_t i = 1; i < 12; i++) assert(request(&f, i, true) == TP_CLICK_BUSY);
    assert(poll(&f, 12, true) == TP_CLICK_OK);
    assert(poll(&f, 100, true) == TP_CLICK_OK);
    assert(f.accepted == 2 && f.press_calls == 1);
    assert(request(&f, 101, true) == TP_CLICK_OK);
    assert(poll(&f, 113, true) == TP_CLICK_OK);
    assert(f.accepted == 4 && f.release_calls == 2);
}
static void test_physical_and_foreign_left_block(void) {
    struct fake f; setup(&f);
    f.physical_left = true;
    assert(request(&f, 0, true) == TP_CLICK_BUSY);
    f.physical_left = false; f.foreign_left = 2;
    assert(request(&f, 1, true) == TP_CLICK_BUSY);
    assert(f.press_calls == 0 && f.release_calls == 0 && f.accepted == 0);
    assert(f.foreign_left == 2);
    f.foreign_left = 0;
    assert(request(&f, 2, true) == TP_CLICK_OK);
    assert(poll(&f, 14, true) == TP_CLICK_OK);
}
static void test_foreign_right_is_preserved(void) {
    struct fake f; setup(&f);
    f.foreign_right = 3;
    assert(request(&f, 0, true) == TP_CLICK_OK);
    check_packet(&f, 0, LEFT | RIGHT);
    assert(poll(&f, 12, true) == TP_CLICK_OK);
    check_packet(&f, 1, RIGHT);
    assert(f.foreign_right == 3 && f.foreign_left == 0 && f.own == 0);
}
static void test_physical_left_takeover_no_forced_zero(void) {
    struct fake f; setup(&f);
    f.foreign_right = 1;
    assert(request(&f, 0, true) == TP_CLICK_OK);
    f.physical_left = true;
    assert(poll(&f, 3, true) == TP_CLICK_OK); /* cancellation, not normal hold */
    assert(f.own == 0 && f.release_calls == 1);
    check_packet(&f, 1, LEFT | RIGHT);
    assert(request(&f, 4, true) == TP_CLICK_BUSY);
    assert(f.physical_left && f.foreign_right == 1);
}
static void test_foreign_left_takeover_keeps_reference(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    f.foreign_left = 4;
    assert(poll(&f, 6, true) == TP_CLICK_OK);
    assert(f.release_calls == 1 && f.own == 0 && f.foreign_left == 4);
    check_packet(&f, 1, LEFT);
}
static void test_press_reference_failure_never_release(void) {
    struct fake f; setup(&f);
    f.press_failures = 1;
    assert(request(&f, 0, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.state.fault == TP_CLICK_FAULT_PRESS_REF && !f.state.owns_left);
    assert(f.release_calls == 0 && f.send_calls == 0);
    assert(request(&f, 100, true) == TP_CLICK_NEEDS_RESYNC);
    assert(poll(&f, 100, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.press_calls == 1 && f.release_calls == 0);
    resync(&f);
    assert(request(&f, 101, true) == TP_CLICK_OK);
    assert(poll(&f, 113, true) == TP_CLICK_OK);
}
static void test_send_down_failure_rolls_back_and_blocks(void) {
    struct fake f; setup(&f);
    f.foreign_right = 2; f.send_failures = 1;
    assert(request(&f, 0, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.state.fault == TP_CLICK_FAULT_SEND_DOWN);
    assert(f.press_calls == 1 && f.release_calls == 1 && f.own == 0);
    assert(f.accepted == 0 && !f.state.down_accepted && !f.state.up_pending);
    assert(request(&f, 1, true) == TP_CLICK_NEEDS_RESYNC);
    assert(poll(&f, 12, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.send_calls == 1 && f.foreign_right == 2);
}
static void test_down_rollback_release_failure_is_explicit(void) {
    struct fake f; setup(&f);
    f.send_failures = 1; f.release_failures = 1;
    assert(request(&f, 0, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.state.fault == TP_CLICK_FAULT_RELEASE_REF && f.state.owns_left);
    assert(f.own == 1 && f.accepted == 0);
    assert(tp_click_ack_resync(&f.state, &f.ops,
        (struct tp_click_resync_ack){f.epoch, true, true}) == TP_CLICK_NEEDS_RESYNC);
    for (uint32_t i = 1; i < 100; i++) assert(poll(&f, i, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.release_calls == 1); /* no automatic ref retry/busy loop */
    assert(tp_click_reset(&f.state, &f.ops) == TP_CLICK_NEEDS_RESYNC);
    assert(!f.state.owns_left && f.own == 0 && f.release_calls == 2);
    resync(&f);
}
static void test_up_reference_failure_keeps_token_and_obligation(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    f.release_failures = 1;
    assert(poll(&f, 12, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.own == 1 && f.state.up_pending && f.state.down_accepted);
    assert(f.send_calls == 1 && f.release_calls == 1);
    assert(poll(&f, 100, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.release_calls == 1);
    assert(tp_click_reset(&f.state, &f.ops) == TP_CLICK_NEEDS_RESYNC);
    assert(f.own == 0 && f.state.up_pending && f.release_calls == 2);
    resync(&f);
    assert(f.release_calls == 2);
}
static void test_up_queue_retry_uses_current_owners(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    f.send_failures = 2;
    assert(poll(&f, 12, true) == TP_CLICK_RETRY);
    assert(!f.state.owns_left && f.state.up_pending && f.release_calls == 1);
    assert(request(&f, 13, true) == TP_CLICK_BUSY);
    assert(poll(&f, 12, true) == TP_CLICK_RETRY);
    assert(poll(&f, 21, true) == TP_CLICK_RETRY);
    assert(f.send_calls == 2);
    assert(poll(&f, 22, true) == TP_CLICK_RETRY);
    f.foreign_left = 1; f.physical_right = true;
    assert(poll(&f, 32, true) == TP_CLICK_OK);
    check_packet(&f, 1, LEFT | RIGHT); /* fresh state, NOT buttons=0 */
    assert(f.release_calls == 1 && f.accepted == 2);
}
static void test_up_retry_budget_and_reset_preservation(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    f.send_failures = 100;
    assert(poll(&f, 12, true) == TP_CLICK_RETRY);
    assert(tp_click_reset(&f.state, &f.ops) == TP_CLICK_OK);
    assert(f.state.up_pending && f.state.up_attempts == 1);
    for (unsigned int i = 1; i < TP_CLICK_MAX_UP_ATTEMPTS; i++) {
        enum tp_click_result expected = (i == TP_CLICK_MAX_UP_ATTEMPTS - 1) ?
            TP_CLICK_NEEDS_RESYNC : TP_CLICK_RETRY;
        assert(poll(&f, 12u + i * TP_CLICK_RETRY_MS, true) == expected);
        (void)tp_click_reset(&f.state, &f.ops);
    }
    assert(f.send_calls == 1 + TP_CLICK_MAX_UP_ATTEMPTS);
    assert(f.state.fault == TP_CLICK_FAULT_UP_EXHAUSTED);
    assert(f.state.up_pending && !f.state.owns_left && f.release_calls == 1);
    for (uint32_t i = 500; i < 1000; i++) assert(poll(&f, i, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.send_calls == 41 && f.release_calls == 1);
    assert(request(&f, 1001, true) == TP_CLICK_NEEDS_RESYNC);
}
static void test_full_fifo_does_not_drop_down(void) {
    struct fake f; setup(&f);
    f.fifo_capacity = 1;
    assert(request(&f, 0, true) == TP_CLICK_OK);
    assert(poll(&f, 12, true) == TP_CLICK_RETRY);
    assert(f.fifo_count == 1 && f.accepted == 1);
    check_packet(&f, 0, LEFT);
    f.fifo_count = 0; /* transport dequeues, not a host acknowledgment */
    assert(poll(&f, 22, true) == TP_CLICK_OK);
    check_packet(&f, 1, 0);
    assert(f.release_calls == 1);
}
static void test_disable_while_held_and_pending(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, false) == TP_CLICK_DISABLED);
    assert(f.accepted == 0);
    assert(request(&f, 1, true) == TP_CLICK_OK);
    f.send_failures = 1; f.foreign_right = 1;
    assert(poll(&f, 2, false) == TP_CLICK_RETRY);
    assert(f.state.up_pending && f.own == 0);
    assert(request(&f, 3, false) == TP_CLICK_DISABLED);
    assert(f.state.up_pending && f.state.up_attempts == 1);
    assert(poll(&f, 12, false) == TP_CLICK_DISABLED);
    check_packet(&f, 1, RIGHT);
    assert(f.release_calls == 1 && !tp_click_inflight(&f.state));
}
static void test_reset_retains_up_until_fifo_accepts(void) {
    struct fake f; setup(&f);
    assert(request(&f, 100, true) == TP_CLICK_OK);
    assert(tp_click_reset(&f.state, &f.ops) == TP_CLICK_OK);
    assert(f.own == 0 && f.state.up_pending && f.state.down_accepted);
    assert(f.accepted == 1 && f.release_calls == 1);
    assert(tp_click_reset(&f.state, &f.ops) == TP_CLICK_OK);
    assert(f.release_calls == 1 && f.state.up_pending);
    assert(request(&f, 101, true) == TP_CLICK_BUSY);
    assert(poll(&f, 102, true) == TP_CLICK_OK);
    check_packet(&f, 1, 0);
}
static void test_epoch_change_never_replays_to_new_endpoint(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    uint32_t old = f.epoch;
    f.epoch++;
    assert(poll(&f, 2, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.state.fault == TP_CLICK_FAULT_EPOCH);
    assert(f.state.epoch == old && f.state.up_pending && f.own == 0);
    assert(f.send_calls == 1 && f.release_calls == 1);
    assert(request(&f, 3, true) == TP_CLICK_NEEDS_RESYNC);
    assert(tp_click_ack_resync(&f.state, &f.ops,
        (struct tp_click_resync_ack){f.epoch, false, true}) == TP_CLICK_NEEDS_RESYNC);
    assert(tp_click_ack_resync(&f.state, &f.ops,
        (struct tp_click_resync_ack){f.epoch, true, false}) == TP_CLICK_NEEDS_RESYNC);
    assert(tp_click_ack_resync(&f.state, &f.ops,
        (struct tp_click_resync_ack){old, true, true}) == TP_CLICK_NEEDS_RESYNC);
    f.foreign_right = 1;
    resync(&f);
    check_packet(&f, 1, RIGHT);
    assert(f.history[0].epoch == old && f.history[1].epoch == f.epoch);
    assert(poll(&f, 100, true) == TP_CLICK_OK && f.accepted == 2);
    assert(request(&f, 101, true) == TP_CLICK_OK);
    assert(poll(&f, 113, true) == TP_CLICK_OK);
    check_packet(&f, 2, LEFT | RIGHT); check_packet(&f, 3, RIGHT);
}
static void test_idle_epoch_change_also_requires_queue_flush(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    assert(poll(&f, 12, true) == TP_CLICK_OK);
    assert(f.fifo_count == 2); /* FIFO accepted != host ACK */
    f.epoch++;
    assert(request(&f, 20, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.press_calls == 1 && f.accepted == 2);
    resync(&f);
    assert(f.flushes == 1 && f.fifo_count == 1);
}
static void test_ready_loss_and_error_lifecycle(void) {
    struct fake f; setup(&f);
    f.ready_result = 1;
    assert(request(&f, 0, true) == TP_CLICK_NOT_READY);
    assert(!f.state.needs_resync && f.press_calls == 0);
    f.ready_result = 0;
    assert(request(&f, 1, true) == TP_CLICK_OK);
    f.ready_result = 1;
    assert(poll(&f, 2, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.own == 0 && f.state.up_pending && f.release_calls == 1);
    f.ready_result = 0;
    assert(poll(&f, 100, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.accepted == 1); /* reconnect alone is not resync proof */
    resync(&f);
    setup(&f); f.ready_result = -1;
    assert(request(&f, 0, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.state.fault == TP_CLICK_FAULT_READY && f.own == 0);
}
static void test_unknown_left_status_fails_closed(void) {
    struct fake f; setup(&f);
    f.left_state_result = -1;
    assert(request(&f, 0, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.press_calls == 0 && f.release_calls == 0 && f.accepted == 0);
    setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    f.left_state_result = -1;
    assert(poll(&f, 1, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.state.fault == TP_CLICK_FAULT_LEFT_STATE);
    assert(f.own == 0 && f.state.up_pending && f.accepted == 1);
}
static void test_epoch_switch_inside_press_callback_is_rejected(void) {
    struct fake f; setup(&f);
    f.switch_after_press = true;
    assert(request(&f, 0, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.own == 0 && f.accepted == 0 && f.release_calls == 1);
    assert(f.state.needs_resync);
}
static void test_clock_rollover_hold_and_retry(void) {
    struct fake f; setup(&f);
    assert(request(&f, UINT32_MAX - 5u, true) == TP_CLICK_OK);
    assert(poll(&f, 5u, true) == TP_CLICK_BUSY); /* 11 elapsed */
    assert(poll(&f, 6u, true) == TP_CLICK_OK);   /* 12 elapsed */
    assert(f.release_calls == 1 && f.accepted == 2);
    setup(&f);
    assert(request(&f, UINT32_MAX - 13u, true) == TP_CLICK_OK);
    f.send_failures = 1;
    assert(poll(&f, UINT32_MAX - 1u, true) == TP_CLICK_RETRY);
    assert(poll(&f, 7u, true) == TP_CLICK_RETRY); /* 9 elapsed */
    assert(f.send_calls == 2);
    assert(poll(&f, 8u, true) == TP_CLICK_OK); /* 10 elapsed */
    assert(f.release_calls == 1 && f.accepted == 2);
}
static void test_missing_callbacks_cannot_forget_owned_reference(void) {
    struct fake f; setup(&f);
    assert(request(&f, 0, true) == TP_CLICK_OK);
    struct tp_click_ops incomplete = f.ops;
    incomplete.release_ref = NULL;
    assert(tp_click_poll(&f.state, &incomplete, 12, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.state.owns_left && f.own == 1 && f.state.up_pending);
    assert(tp_click_ack_resync(&f.state, &f.ops,
        (struct tp_click_resync_ack){f.epoch, true, true}) == TP_CLICK_NEEDS_RESYNC);
    assert(tp_click_reset(&f.state, &f.ops) == TP_CLICK_NEEDS_RESYNC);
    assert(f.own == 0 && f.release_calls == 1 && f.state.up_pending);
    resync(&f);
    setup(&f);
    assert(tp_click_request(&f.state, NULL, 0, true) == TP_CLICK_NEEDS_RESYNC);
    assert(f.press_calls == 0 && f.release_calls == 0);
}

int main(void) {
    unsigned int tests = 0;
#define RUN(fn) do { fn(); tests++; } while (0)
    RUN(test_fifo_hold_and_exact_release);
    RUN(test_duplicate_request_not_buffered);
    RUN(test_physical_and_foreign_left_block);
    RUN(test_foreign_right_is_preserved);
    RUN(test_physical_left_takeover_no_forced_zero);
    RUN(test_foreign_left_takeover_keeps_reference);
    RUN(test_press_reference_failure_never_release);
    RUN(test_send_down_failure_rolls_back_and_blocks);
    RUN(test_down_rollback_release_failure_is_explicit);
    RUN(test_up_reference_failure_keeps_token_and_obligation);
    RUN(test_up_queue_retry_uses_current_owners);
    RUN(test_up_retry_budget_and_reset_preservation);
    RUN(test_full_fifo_does_not_drop_down);
    RUN(test_disable_while_held_and_pending);
    RUN(test_reset_retains_up_until_fifo_accepts);
    RUN(test_epoch_change_never_replays_to_new_endpoint);
    RUN(test_idle_epoch_change_also_requires_queue_flush);
    RUN(test_ready_loss_and_error_lifecycle);
    RUN(test_unknown_left_status_fails_closed);
    RUN(test_epoch_switch_inside_press_callback_is_rejected);
    RUN(test_clock_rollover_hold_and_retry);
    RUN(test_missing_callbacks_cannot_forget_owned_reference);
#undef RUN
    printf("PASS: %u pure click-output ownership/FIFO/lifecycle tests\n", tests);
    return 0;
}
