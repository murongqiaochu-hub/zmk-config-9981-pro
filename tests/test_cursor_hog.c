#define CONFIG_A320_CURSOR_STEADY 1
/* SPDX-License-Identifier: MIT
 * Actual-header steady-mode contracts; frozen SDK/mock fixture stays untouched.
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
 *   -fno-sanitize-recover=all -Itests/hog_snapshot_mocks \
 *   tests/test_cursor_hog.c -o /tmp/test_cursor_hog && /tmp/test_cursor_hog
 */
#define main legacy_hog_main
#include "test_hog_snapshot.c"
#undef main

/* Official nonblocking, non-consuming peek; no new fixture scheduling model. */
int k_msgq_peek(struct k_msgq *q, void *data) {
    if (!q->used) { return -ENOMSG; }
    memcpy(data, q->buffer + q->read_pos * q->item_size, q->item_size);
    return 0;
}
static int64_t sent_at[sizeof(notifications) / sizeof(notifications[0])];
static void steady_stamp(struct bt_conn *conn) {
    (void)conn;
    sent_at[notify_count - 1] = now_ms;
    during_notify = steady_stamp; /* Frozen mock clears the hook before calling. */
}
static void steady_balanced(void) {
    struct bbp9981_hog_mouse_stats s = stats();
    assert(!lock_depth && ref_calls == unref_calls);
    for (unsigned int i = 0; i < 3; i++) { assert(peers[i].refs == 1); }
    assert(!s.outstanding && !s.pending_head && !s.sending && !bbp9981_hog_mouse_fifo.used);
    assert(s.accepted == s.notified + s.canceled + s.coalesced);
}
static void steady_reset(void) {
    steady_balanced(); /* Verify the NEW ledger before bypassing the OLD reset ledger. */
    ref_calls = 0;
    reset_case();
    for (unsigned int i = 0; i < sizeof(sent_at) / sizeof(sent_at[0]); i++) { sent_at[i] = -1; }
    during_notify = steady_stamp;
}
static void xy(int16_t x, int16_t y, uint8_t buttons) {
    struct zmk_hid_mouse_report_body r = {.buttons = buttons, .d_x = x, .d_y = y}, copy = r;
    assert(zmk_hog_send_mouse_report(&r) == 0);
    assert(memcmp(&r, &copy, sizeof(r)) == 0);
}
static void expect_xy(unsigned int n, int x, int y, uint8_t buttons, int64_t at) {
    assert(n < notify_count && sent_at[n] == at);
    struct zmk_hid_mouse_report_body r = notifications[n].body;
    assert(r.d_x == x && r.d_y == y && r.buttons == buttons);
    assert(!r.d_scroll_x && !r.d_scroll_y);
}
static void test_steady_exact_immediate_peek(void) {
    steady_reset();
    for (int i = 1; i <= 4; i++) { xy((int16_t)(3 * i), (int16_t)(-6 * i), 3); }
    struct bbp9981_hog_mouse_entry a, b;
    unsigned int used = bbp9981_hog_mouse_fifo.used, rd = bbp9981_hog_mouse_fifo.read_pos;
    unsigned int wr = bbp9981_hog_mouse_fifo.write_pos, before = gets;
    assert(k_msgq_peek(&bbp9981_hog_mouse_fifo, &a) == 0);
    assert(k_msgq_peek(&bbp9981_hog_mouse_fifo, &b) == 0 && !memcmp(&a, &b, sizeof(a)));
    assert(used == bbp9981_hog_mouse_fifo.used && rd == bbp9981_hog_mouse_fifo.read_pos);
    assert(wr == bbp9981_hog_mouse_fifo.write_pos && gets == before);
    run_ready();
    assert(now_ms == 0 && notify_count == 1 && worker_runs == 1 && stats().coalesced == 3);
    expect_xy(0, 30, -60, 3, 0);
    assert(!stock_calls && get_conn_calls == 4 && !bbp9981_hog_mouse_work.delayed);
    assert(k_msgq_peek(&bbp9981_hog_mouse_fifo, &b) == -ENOMSG);
    xy(1, 2, 3); run_ready(); /* No batching delay, including a lone admission. */
    assert(notify_count == 2 && now_ms == 0); expect_xy(1, 1, 2, 3, 0);
    steady_balanced();
}
static void test_steady_window_not_sliding(void) {
    for (int last = 10; last <= 11; last++) {
        steady_reset(); xy(1, 2, 0);
        now_ms = 10; xy(2, 4, 0);
        now_ms = last; xy(4, 8, 0); /* 11 is only 1ms after the SECOND entry. */
        run_ready();
        assert(now_ms == last && notify_count == (last == 10 ? 1u : 2u));
        assert(stats().coalesced == (last == 10 ? 2u : 1u));
        expect_xy(0, last == 10 ? 7 : 3, last == 10 ? 14 : 6, 0, last);
        if (last == 11) { expect_xy(1, 4, 8, 0, 11); }
        steady_balanced();
    }
}
static void test_steady_geometry(void) {
    static const struct { int16_t x, y, nx, ny; bool merge; } cases[] = {
        {1, 2, 2, 4, true}, {-1, -2, -2, -4, true}, {2, -1, 6, -3, true},
        {0, -2, 0, -6, true}, {2, 0, 6, 0, true},
        {32766, 0, 1, 0, true}, {-32767, 0, -1, 0, true},
        {-16384, -16384, -16384, -16384, true},
        {1, 2, 2, 3, false}, /* Same quadrant is NOT collinearity. */
        {1, 2, -2, -4, false}, {0, 0, 1, 2, false}, {1, 2, 0, 0, false},
        {-32768, -32768, -32768, -32768, false}, /* dot exceeds INT32_MAX. */
        {32767, 32767, 32767, -32768, false},
    };
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        steady_reset(); xy(cases[i].x, cases[i].y, 0); xy(cases[i].nx, cases[i].ny, 0);
        run_ready();
        assert(notify_count == (cases[i].merge ? 1u : 2u));
        assert(stats().coalesced == (cases[i].merge ? 1u : 0u));
        expect_xy(0, cases[i].x + (cases[i].merge ? cases[i].nx : 0),
                  cases[i].y + (cases[i].merge ? cases[i].ny : 0), 0, 0);
        if (!cases[i].merge) { expect_xy(1, cases[i].nx, cases[i].ny, 0, 0); }
        steady_balanced();
    }
}
static void test_steady_hard_barriers(void) {
    for (unsigned int mixed = 0; mixed <= 1; mixed++) {
        for (unsigned int wheels = 1; wheels <= 3; wheels++) {
            steady_reset(); xy(1, 2, 0);
            struct zmk_hid_mouse_report_body r = {
                .d_x = mixed ? 2 : 0, .d_y = mixed ? 4 : 0,
                .d_scroll_x = wheels & 1 ? 1 : 0, .d_scroll_y = wheels & 2 ? -1 : 0,
            };
            assert(zmk_hog_send_mouse_report(&r) == 0);
            xy(2, 4, 0); xy(3, 6, 0); run_ready();
            assert(notify_count == 3 && stats().coalesced == 1);
            expect_xy(0, 1, 2, 0, 0); expect_xy(2, 5, 10, 0, 0);
            assert(!memcmp(&notifications[1].body, &r, sizeof(r)) && sent_at[1] == 0);
            steady_balanced();
        }
    }
    steady_reset(); xy(1, 2, 1);
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0); xy(2, 4, 0); run_ready();
    assert(notify_count == 4 && !stats().coalesced);
    expect_xy(0, 1, 2, 1, 0); expect_xy(3, 2, 4, 0, 0);
    expect_xy(1, 0, 0, 1, 0); expect_xy(2, 0, 0, 0, 0);
    steady_reset(); xy(1, 2, 1); xy(2, 4, 2); xy(3, 6, 1); xy(4, 8, 1); run_ready();
    assert(notify_count == 3 && stats().coalesced == 1);
    expect_xy(0, 1, 2, 1, 0); expect_xy(1, 2, 4, 2, 0); expect_xy(2, 7, 14, 1, 0);
    steady_balanced();
}
static void test_steady_captured_connection(void) {
    for (int reconnect = 0; reconnect <= 1; reconnect++) {
        steady_reset(); xy(1, 2, 0);
        if (reconnect) { active_conns[0] = &peers[2]; } else { active_profile = 1; }
        xy(2, 4, 0);
        active_profile = 0; active_conns[0] = &peers[0];
        xy(3, 6, 0); xy(4, 8, 0); run_ready();
        assert(notify_count == 3 && stats().coalesced == 1 && get_conn_calls == 4);
        assert(notifications[0].conn == &peers[0] && notifications[2].conn == &peers[0]);
        assert(notifications[1].conn == &peers[reconnect ? 2 : 1]);
        expect_xy(0, 1, 2, 0, 0); expect_xy(1, 2, 4, 0, 0); expect_xy(2, 7, 14, 0, 0);
        steady_balanced(); /* Both conn objects remained connected: identity is the fence. */
    }
}
static void test_steady_overflow_stops(void) {
    for (int axis = 0; axis < 2; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            steady_reset();
            int16_t x = (int16_t)(sign * (axis ? 15000 : 30000));
            int16_t y = (int16_t)(sign * (axis ? 30000 : 15000));
            xy(x, y, 0); xy(x / 15, y / 15, 0); xy(x / 30, y / 30, 0); xy(x / 30, y / 30, 0);
            run_ready();
            assert(notify_count == 2 && stats().coalesced == 2);
            expect_xy(0, x + x / 15, y + y / 15, 0, 0);
            expect_xy(1, x / 15, y / 15, 0, 0);
            steady_balanced();
        }
    }
}
static void test_steady_retry_immutable_oldest_ttl(void) {
    steady_reset(); xy(1, 2, 0); now_ms = 10; xy(2, 4, 0);
    notify_errors[notify_error_count++] = -ENOMEM; notify_errors[notify_error_count++] = -EAGAIN;
    run_ready();
    assert(notify_count == 1 && stats().coalesced == 1 && bbp9981_hog_mouse_head.attempts == 1);
    assert(bbp9981_hog_mouse_head.entry.expires_at == 400);
    xy(4, 8, 0); run_ready(); advance(9);
    assert(now_ms == 19 && notify_count == 1); advance(1);
    assert(now_ms == 20 && notify_count == 2 && stats().coalesced == 1);
    advance(10);
    assert(now_ms == 30 && notify_count == 4 && stats().retries == 2);
    expect_xy(0, 3, 6, 0, 10); expect_xy(1, 3, 6, 0, 20);
    expect_xy(2, 3, 6, 0, 30); expect_xy(3, 4, 8, 0, 30);
    steady_reset(); xy(1, 2, 0); now_ms = 10; xy(2, 4, 0);
    now_ms = 390; notify_default_error = -ENOMEM; run_ready();
    assert(notify_count == 1 && stats().coalesced == 1);
    assert(bbp9981_hog_mouse_head.entry.expires_at == 400);
    advance(9); assert(now_ms == 399 && notify_count == 1 && stats().outstanding == 1);
    advance(1);
    assert(now_ms == 400 && notify_count == 1 && stats().canceled == 1);
    assert(stats().last_send_error == -ETIMEDOUT); expect_xy(0, 3, 6, 0, 390);
    steady_balanced();
}
static void test_steady_40_attempts_and_peer_cancel(void) {
    steady_reset(); xy(1, 2, 0); xy(2, 4, 0); notify_default_error = -ENOMEM; run_ready();
    for (unsigned int i = 1; i < 40; i++) {
        advance(9); assert(notify_count == i && now_ms == (int64_t)i * 10 - 1);
        advance(1); assert(notify_count == i + 1);
    }
    assert(now_ms == 390 && stats().coalesced == 1 && stats().canceled == 1);
    assert(stats().notify_errors == 40 && stats().retries == 39 && !stats().notified);
    for (unsigned int i = 0; i < 40; i++) { expect_xy(i, 3, 6, 0, (int64_t)i * 10); }
    advance(10); assert(now_ms == 400 && notify_count == 40); steady_balanced();
    steady_reset(); xy(1, 2, 0); xy(2, 4, 0); assert(snapshot(0, 0) == 0);
    active_profile = 1; xy(5, 10, 0); notify_errors[notify_error_count++] = -ECONNRESET;
    run_ready();
    assert(notify_count == 2 && stats().coalesced == 1 && stats().canceled == 2);
    assert(stats().notified == 1 && notifications[0].conn == &peers[0]);
    assert(notifications[1].conn == &peers[1]);
    expect_xy(0, 3, 6, 0, 0); expect_xy(1, 5, 10, 0, 0); steady_balanced();
}
static void test_steady_plug_schedule_failure(void) {
    steady_reset(); xy(1, 2, 0); xy(2, 4, 0);
    hog_work_q.plugged = true; run_ready(); advance(400);
    assert(!notify_count && stats().canceled == 2 && !stats().coalesced);
    assert(stats().last_send_error == -ETIMEDOUT);
    hog_work_q.plugged = false; run_ready(); assert(!notify_count); steady_balanced();
    steady_reset(); xy(1, 2, 0); xy(2, 4, 0);
    notify_errors[notify_error_count++] = -ENOMEM; run_ready();
    hog_work_q.plugged = true; advance(10);
    assert(notify_count == 1 && stats().coalesced == 1 && stats().canceled == 1);
    assert(stats().schedule_errors == 1 && stats().last_send_error == -EBUSY);
    hog_work_q.plugged = false; xy(4, 8, 0); run_ready();
    assert(notify_count == 2); expect_xy(1, 4, 8, 0, 10); steady_balanced();
    for (int sys = 0; sys <= 1; sys++) {
        steady_reset(); xy(1, 2, 0); xy(2, 4, 0); assert(snapshot(0, 0) == 0);
        assert(run_one(&bbp9981_hog_mouse_dispatch));
        schedule_fail_queue = sys ? &k_sys_work_q : &hog_work_q; schedule_fail_error = -EIO;
        assert(run_one(&bbp9981_hog_mouse_work)); run_ready();
        assert(now_ms == 0 && notify_count == 1 && stats().coalesced == 1);
        assert(stats().notified == 1 && stats().canceled == 1 && stats().schedule_errors == 1);
        assert(stats().last_send_error == -EIO); expect_xy(0, 3, 6, 0, 0);
        steady_balanced();
    }
}
static bool steady_sdk_wait;
static void steady_refill_during_notify(struct bt_conn *conn) {
    steady_stamp(conn);
    assert(conn == &peers[0] && stats().sending && stats().outstanding == 1);
    assert(stats().coalesced == 3 && unref_calls == 3 && conn->refs == 2);
    for (int i = 0; i < 3; i++) { xy(5, 10, 0); }
    struct zmk_hid_mouse_report_body full = {.d_x = 5, .d_y = 10};
    assert(zmk_hog_send_mouse_report(&full) == -EAGAIN);
    assert(stats().outstanding == 4 && conn->refs == 5 && bbp9981_hog_mouse_fifo.used == 3);
    /* Exercise the internal sending guard, not merely the producer path. */
    struct bt_conn *released[4];
    k_spinlock_key_t key = k_spin_lock(&bbp9981_hog_mouse_lock);
    assert(bbp9981_hog_mouse_coalesce_locked(released) == 0);
    assert(bbp9981_hog_mouse_head.entry.body.d_x == 10 && bbp9981_hog_mouse_fifo.used == 3);
    k_spin_unlock(&bbp9981_hog_mouse_lock, key);
    if (steady_sdk_wait) {
        now_ms = 410; expire_timers(); assert(run_one(&bbp9981_hog_mouse_dispatch));
        assert(stats().sending && stats().outstanding == 4 && conn->refs == 5);
    }
}
static void test_steady_sdk_refill_cancel_max_batch(void) {
    for (int wait = 0; wait <= 1; wait++) {
        steady_reset(); steady_sdk_wait = wait != 0;
        for (int i = 1; i <= 4; i++) { xy((int16_t)i, (int16_t)(2 * i), 0); }
        notify_default_error = wait ? -ENOMEM : -ECONNRESET;
        during_notify = steady_refill_during_notify; run_ready();
        assert(now_ms == (wait ? 410 : 0) && notify_count == 1);
        expect_xy(0, 10, 20, 0, 0);
        assert(stats().accepted == 7 && stats().coalesced == 3 && stats().canceled == 4);
        assert(!stats().notified && stats().queue_full == 1 && stats().notify_errors == 1);
        assert(ref_calls == 8 && unref_calls == 8 && stats().last_send_error == notify_default_error);
        steady_balanced(); /* Detect stale batch count/double-unref; ASan also checks array bounds. */
    }
}
static void test_steady_bounded_random_rays(void) {
    uint32_t seed = UINT32_C(0x9981a320);
    for (unsigned int trial = 0; trial < 32; trial++) {
        steady_reset();
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        int dx = (int)(seed % 5) - 2, dy = (int)((seed >> 8) % 5) - 2;
        if (!dx && !dy) { dx = 1; }
        int sum = 0; uint8_t buttons = (uint8_t)((seed >> 16) & 7);
        for (int i = 0; i < 4; i++) {
            seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
            int scale = (int)(seed % 100) + 1; sum += scale;
            now_ms += (seed >> 8) % 4;
            xy((int16_t)(dx * scale), (int16_t)(dy * scale), buttons);
        }
        int64_t at = now_ms; run_ready();
        assert(now_ms == at && notify_count == 1 && stats().coalesced == 3);
        expect_xy(0, dx * sum, dy * sum, buttons, at); steady_balanced();
    }
}
int main(void) {
    assert(legacy_hog_main() == 0); /* ALL 25 frozen legacy cases actually execute with ON. */
    RUN(test_steady_exact_immediate_peek);
    RUN(test_steady_window_not_sliding);
    RUN(test_steady_geometry);
    RUN(test_steady_hard_barriers);
    RUN(test_steady_captured_connection);
    RUN(test_steady_overflow_stops);
    RUN(test_steady_retry_immutable_oldest_ttl);
    RUN(test_steady_40_attempts_and_peer_cancel);
    RUN(test_steady_plug_schedule_failure);
    RUN(test_steady_sdk_refill_cancel_max_batch);
    RUN(test_steady_bounded_random_rays);
    steady_balanced();
    puts("PASS cursor HOG: frozen legacy + steady contracts; no hardware I/O");
    return 0;
}
