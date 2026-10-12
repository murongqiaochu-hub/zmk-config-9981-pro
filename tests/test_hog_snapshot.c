/* SPDX-License-Identifier: MIT
 * Host contract tests INCLUDE THE ACTUAL preinclude header, not a parallel
 * implementation. Compile separately (do not change the existing runner):
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
 *    -fno-sanitize-recover=all -Itests/hog_snapshot_mocks \
 *    tests/test_hog_snapshot.c -o <test-executable>
 * No hardware, real HID/Bluetooth notifications, keymap or movement code runs.
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_ZMK_POINTING 1
#define CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE 4
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h"

/* Same-TU service and stock definition appear AFTER preinclude, as in hog.c.
 * This validates const bt_gatt_service_static linkage and the rename boundary.
 */
BT_GATT_SERVICE_DEFINE(hog_svc, [13] = {.marker = 13});
struct k_work_q hog_work_q;
struct k_work_q k_sys_work_q;
static unsigned int stock_calls;
static bool stock_recurse;
int zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *body) {
    stock_calls++;
    if (stock_recurse) {
        stock_recurse = false;
        return zmk_hog_send_mouse_report(body);
    }
    return 77;
}
#undef zmk_hog_send_mouse_report

static int64_t now_ms;
static unsigned int lock_depth, ref_calls, unref_calls, get_conn_calls;
static unsigned int msgq_put_calls, gets, schedules, worker_runs, dispatcher_runs;
static int active_profile, capture_race, notify_default_error, security_error;
static int put_error_once;
static bool no_conn, no_dst;
static struct bt_conn peers[3]; /* third object is a NEW connection to peer 0 */
static struct bt_conn *active_conns[2];
static int addr_profile[3];
static struct k_work_q *current_queue, *schedule_fail_queue;
static int schedule_fail_error;
static unsigned int schedule_fail_after;
static int notify_errors[128];
static unsigned int notify_error_count, notify_error_pos;
struct notification {
    struct bt_conn *conn;
    struct zmk_hid_mouse_report_body body;
    int error;
};
static struct notification notifications[512];
static unsigned int notify_count, security_count;
static struct bt_conn *security_conn;
static void (*during_notify)(struct bt_conn *conn);

k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = true;
    return lock_depth++;
}
void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held && lock_depth == key + 1);
    lock->held = false;
    lock_depth--;
}
int64_t k_uptime_get(void) { return now_ms; }

int k_msgq_put(struct k_msgq *q, const void *data, k_timeout_t timeout) {
    assert(timeout == K_NO_WAIT); /* Producer AND cleanup must not wait. */
    msgq_put_calls++;
    if (put_error_once) {
        int error = put_error_once;
        put_error_once = 0;
        return error;
    }
    if (q->used == q->capacity) { return -ENOMSG; }
    memcpy(q->buffer + q->write_pos * q->item_size, data, q->item_size);
    q->write_pos = (q->write_pos + 1) % q->capacity;
    q->used++;
    return 0;
}
int k_msgq_get(struct k_msgq *q, void *data, k_timeout_t timeout) {
    assert(timeout == K_NO_WAIT);
    gets++;
    if (!q->used) { return -ENOMSG; }
    memcpy(data, q->buffer + q->read_pos * q->item_size, q->item_size);
    q->read_pos = (q->read_pos + 1) % q->capacity;
    q->used--;
    return 0;
}
unsigned int k_msgq_num_used_get(struct k_msgq *q) { return q->used; }

/* Match pinned kernel/work.c: nonzero-delay submissions do NOT validate the
 * queue; timeout expiry can silently lose submission. K_NO_WAIT does validate
 * started/plugged, and running work may be queued for a later invocation.
 */
int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                              k_timeout_t delay) {
    schedules++;
    if (schedule_fail_queue == queue) {
        if (!schedule_fail_after) {
            schedule_fail_queue = NULL;
            return schedule_fail_error;
        }
        schedule_fail_after--;
    }
    if (work->queued || work->delayed || work->canceling) { return 0; }
    if (delay == K_NO_WAIT) {
        if (!queue->started) { return -ENODEV; }
        if (queue->plugged) { return -EBUSY; }
        work->queued = true;
        work->queue = queue;
        return work->running ? 2 : 1;
    }
    assert(delay > 0);
    work->queue = queue;
    work->due = now_ms + delay;
    work->delayed = true;
    return 1;
}

struct bt_conn *bt_conn_ref(struct bt_conn *conn) {
    assert(conn && conn->refs >= 1);
    conn->refs++;
    ref_calls++;
    return conn;
}
void bt_conn_unref(struct bt_conn *conn) {
    assert(!lock_depth); /* Reclamation is never done inside state lock. */
    assert(conn && conn->refs > 1);
    conn->refs--;
    unref_calls++;
}
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn) {
    return no_dst ? NULL : &conn->dst;
}
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info) {
    if (conn->info_error) { return conn->info_error; }
    info->state = conn->state;
    return 0;
}
int zmk_ble_active_profile_index(void) { return active_profile; }
int zmk_ble_profile_index(const bt_addr_le_t *addr) {
    assert(addr->a.val[0] < 3);
    return addr_profile[addr->a.val[0]];
}
struct bt_conn *zmk_ble_active_profile_conn(void) {
    get_conn_calls++;
    if (capture_race == 1) { active_profile = 1; }
    if (no_conn || capture_race == 4) {
        if (capture_race == 4) { active_profile = 1; }
        return NULL;
    }
    struct bt_conn *conn = active_conns[capture_race == 2 ? 1 : active_profile];
    bt_conn_ref(conn); /* Official getter returns a reference. */
    if (capture_race == 3) { active_profile = 1; }
    return conn;
}
int bt_conn_set_security(struct bt_conn *conn, bt_security_t security) {
    assert(!lock_depth && current_queue == &hog_work_q);
    assert(security == BT_SECURITY_L2);
    security_conn = conn;
    security_count++;
    return security_error;
}
int bt_gatt_notify_cb(struct bt_conn *conn, struct bt_gatt_notify_params *params) {
    assert(!lock_depth && current_queue == &hog_work_q);
    assert(params->attr == &hog_svc.attrs[13]);
    assert(params->len == sizeof(struct zmk_hid_mouse_report_body));
    assert(!params->func && !params->user_data);
    assert(notify_count < sizeof(notifications) / sizeof(notifications[0]));
    int error = notify_error_pos < notify_error_count ? notify_errors[notify_error_pos++]
                                                     : notify_default_error;
    notifications[notify_count++] = (struct notification){
        .conn = conn, .body = *(const struct zmk_hid_mouse_report_body *)params->data, .error = error,
    };
    if (during_notify) {
        void (*hook)(struct bt_conn *) = during_notify;
        during_notify = NULL;
        hook(conn);
    }
    return error;
}

static struct bbp9981_hog_mouse_stats stats(void) {
    struct bbp9981_hog_mouse_stats out;
    assert(bbp9981_ble_hid_mouse_report_stats_get(&out) == 0);
    return out;
}
static void balanced(void) {
    assert(lock_depth == 0 && ref_calls == unref_calls);
    assert(peers[0].refs == 1 && peers[1].refs == 1 && peers[2].refs == 1);
    assert(stats().outstanding == 0 && !stats().pending_head && !stats().sending);
    assert(bbp9981_hog_mouse_fifo.used == 0);
    assert(stats().accepted == stats().notified + stats().canceled);
}
static bool run_one(struct k_work_delayable *work) {
    if (!work->queued || !work->queue->started || work->queue->plugged) { return false; }
    assert(!work->running && !lock_depth);
    work->queued = false;
    work->running = true;
    struct k_work_q *previous = current_queue;
    current_queue = work->queue;
    unsigned int before = notify_count;
    if (work == &bbp9981_hog_mouse_work) { worker_runs++; }
    else { dispatcher_runs++; }
    work->work.handler(&work->work);
    if (work == &bbp9981_hog_mouse_work) { assert(notify_count <= before + 1); }
    else { assert(notify_count == before); }
    assert(!lock_depth);
    current_queue = previous;
    work->running = false;
    return true;
}
static void expire_timers(void) {
    struct k_work_delayable *works[] = {&bbp9981_hog_mouse_dispatch, &bbp9981_hog_mouse_work};
    for (unsigned int i = 0; i < 2; i++) {
        struct k_work_delayable *work = works[i];
        if (work->delayed && work->due <= now_ms) {
            work->delayed = false;
            /* work_timeout discards submission errors without notifying caller. */
            (void)k_work_schedule_for_queue(work->queue, work, K_NO_WAIT);
        }
    }
}
static void run_ready(void) {
    for (unsigned int i = 0; i < 1000; i++) {
        bool progress = run_one(&bbp9981_hog_mouse_dispatch);
        progress = run_one(&bbp9981_hog_mouse_work) || progress;
        if (!progress) { return; }
    }
    assert(!"work busy loop detected");
}
static void advance(int64_t ms) {
    assert(ms >= 0);
    now_ms += ms;
    expire_timers();
    run_ready();
}
static void reset_case(void) {
    if (ref_calls) { balanced(); }
    now_ms = 0;
    lock_depth = ref_calls = unref_calls = get_conn_calls = 0;
    msgq_put_calls = gets = schedules = worker_runs = dispatcher_runs = 0;
    active_profile = capture_race = notify_default_error = security_error = put_error_once = 0;
    no_conn = no_dst = false;
    notify_count = notify_error_count = notify_error_pos = security_count = 0;
    security_conn = NULL;
    current_queue = schedule_fail_queue = NULL;
    schedule_fail_error = 0;
    schedule_fail_after = 0;
    during_notify = NULL;
    hog_work_q = k_sys_work_q = (struct k_work_q){.started = true};
    for (unsigned int i = 0; i < 3; i++) {
        peers[i] = (struct bt_conn){.state = BT_CONN_STATE_CONNECTED, .refs = 1};
        peers[i].dst.a.val[0] = i;
        addr_profile[i] = i == 1 ? 1 : 0;
    }
    active_conns[0] = &peers[0];
    active_conns[1] = &peers[1];
    bbp9981_hog_mouse_stats = (struct bbp9981_hog_mouse_stats){0};
    memset(&bbp9981_hog_mouse_head, 0, sizeof(bbp9981_hog_mouse_head));
    bbp9981_hog_mouse_fifo.used = bbp9981_hog_mouse_fifo.read_pos =
        bbp9981_hog_mouse_fifo.write_pos = 0;
    bbp9981_hog_mouse_work = (struct k_work_delayable){.work = {.handler = bbp9981_hog_mouse_worker}};
    bbp9981_hog_mouse_dispatch = (struct k_work_delayable){
        .work = {.handler = bbp9981_hog_mouse_dispatcher},
    };
    stock_calls = 0;
    stock_recurse = false;
}
static struct zmk_hid_mouse_report_body body(uint8_t buttons) {
    return (struct zmk_hid_mouse_report_body){
        .buttons = buttons, .d_x = 123, .d_y = -456, .d_scroll_y = 7, .d_scroll_x = -8,
    };
}
static int snapshot(uint8_t buttons, int profile) {
    struct zmk_hid_mouse_report_body report = body(buttons);
    return bbp9981_ble_hid_send_mouse_buttons_snapshot(&report, profile);
}
static void zero_motion(struct zmk_hid_mouse_report_body report) {
    assert(!report.d_x && !report.d_y && !report.d_scroll_x && !report.d_scroll_y);
}

static void test_static_init_queues(void) {
    reset_case();
    /* No custom initializer or ordering assumption; reject before queues start. */
    hog_work_q.started = false;
    assert(snapshot(1, 0) == -ENODEV);
    run_ready();
    balanced();
    hog_work_q.started = true;
    k_sys_work_q.started = false;
    assert(snapshot(1, 0) == -ENODEV);
    balanced();
    k_sys_work_q.started = true;
    assert(snapshot(1, 0) == 0);
    run_ready();
    assert(stats().notified == 1 && stats().schedule_errors == 2);
    balanced();
}
static void test_fifo_down_up_copy(void) {
    reset_case();
    struct zmk_hid_mouse_report_body report = body(1), original = report;
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&report, 0) == 0);
    report.buttons = 0;
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&report, 0) == 0);
    report.buttons = 31;
    assert(notify_count == 0 && stats().outstanding == 2); /* Accepted != sent/ACK. */
    run_ready();
    assert(notify_count == 2 && notifications[0].body.buttons == 1 &&
           notifications[1].body.buttons == 0);
    zero_motion(notifications[0].body);
    zero_motion(notifications[1].body);
    assert(report.d_x == original.d_x && report.d_y == original.d_y &&
           report.d_scroll_y == original.d_scroll_y && report.d_scroll_x == original.d_scroll_x);
    assert(!stock_calls && !bbp9981_hog_mouse_work.delayed);
    balanced();
}
static void test_full_no_eviction(void) {
    reset_case();
    for (unsigned int i = 0; i < 4; i++) { assert(snapshot((uint8_t)(i + 1), 0) == 0); }
    unsigned int get_before = gets, put_before = msgq_put_calls;
    assert(snapshot(31, 0) == -EAGAIN);
    assert(gets == get_before && msgq_put_calls == put_before && stats().outstanding == 4);
    run_ready();
    assert(notify_count == 4 && stats().queue_full == 1);
    for (unsigned int i = 0; i < 4; i++) { assert(notifications[i].body.buttons == i + 1); }
    balanced();
}
static void test_pending_counts_capacity(void) {
    reset_case();
    notify_errors[notify_error_count++] = -ENOMEM;
    assert(snapshot(1, 0) == 0);
    run_ready();
    assert(stats().pending_head && stats().outstanding == 1);
    for (unsigned int i = 0; i < 3; i++) { assert(snapshot(0, 0) == 0); }
    assert(snapshot(31, 0) == -EAGAIN); /* Capacity includes pending HEAD. */
    assert(bbp9981_hog_mouse_fifo.used == 3);
    advance(10);
    assert(notify_count == 5 && notifications[1].body.buttons == 1);
    balanced();
}
static void test_pinned_peer_after_switch(void) {
    reset_case();
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    active_profile = 1;
    assert(snapshot(2, 1) == 0);
    unsigned int capture_count = get_conn_calls;
    run_ready();
    assert(notify_count == 3 && get_conn_calls == capture_count);
    assert(notifications[0].conn == &peers[0] && notifications[1].conn == &peers[0] &&
           notifications[2].conn == &peers[1]);
    balanced();
}
static void test_capture_races(void) {
    for (int race = 1; race <= 4; race++) {
        reset_case();
        capture_race = race;
        assert(snapshot(1, 0) == -ESTALE);
        assert(stats().stale_captures == 1 && !stats().accepted);
        run_ready();
        assert(!notify_count);
        balanced();
    }
    reset_case();
    addr_profile[0] = -1;
    assert(snapshot(1, 0) == -ESTALE);
    balanced();
    reset_case();
    no_dst = true;
    assert(snapshot(1, 0) == -ESTALE);
    balanced();
}
static void test_expected_profile_and_invalid(void) {
    reset_case();
    assert(snapshot(1, 1) == -ESTALE && !get_conn_calls);
    assert(snapshot(1, -1) == -EINVAL);
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(NULL, 0) == -EINVAL);
    assert(zmk_hog_send_mouse_report(NULL) == -EINVAL);
    assert(bbp9981_ble_hid_mouse_report_stats_get(NULL) == -EINVAL);
    balanced();
}
static void test_capture_disconnected(void) {
    reset_case();
    no_conn = true;
    assert(snapshot(1, 0) == -ENOTCONN);
    no_conn = false;
    peers[0].state = BT_CONN_STATE_DISCONNECTING;
    assert(snapshot(1, 0) == -ENOTCONN);
    peers[0].state = BT_CONN_STATE_CONNECTED;
    peers[0].info_error = -EINVAL;
    assert(snapshot(1, 0) == -ENOTCONN);
    assert(!notify_count);
    balanced();
}
static void test_disconnect_cancels_no_new_peer_replay(void) {
    reset_case();
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    peers[0].state = BT_CONN_STATE_DISCONNECTED;
    active_profile = 1;
    assert(snapshot(2, 1) == 0);
    run_ready();
    assert(notify_count == 1 && notifications[0].conn == &peers[1] &&
           notifications[0].body.buttons == 2);
    assert(stats().canceled == 2 && stats().last_send_error == -ENOTCONN);
    peers[0].state = BT_CONN_STATE_CONNECTED;
    advance(500);
    assert(notify_count == 1);
    balanced();
}
static void test_reconnect_different_object_no_replay(void) {
    reset_case();
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    peers[0].state = BT_CONN_STATE_DISCONNECTED;
    active_conns[0] = &peers[2];
    assert(snapshot(2, 0) == 0);
    run_ready();
    assert(notify_count == 1 && notifications[0].conn == &peers[2] &&
           notifications[0].body.buttons == 2);
    assert(stats().canceled == 2);
    balanced();
}
static void test_normal_motion_same_fifo(void) {
    reset_case();
    struct zmk_hid_mouse_report_body moving = body(3), original = moving;
    assert(snapshot(1, 0) == 0);
    assert(zmk_hog_send_mouse_report(&moving) == 0);
    assert(snapshot(0, 0) == 0);
    memset(&moving, 0, sizeof(moving));
    run_ready();
    assert(notify_count == 3 && notifications[0].body.buttons == 1 &&
           notifications[2].body.buttons == 0);
    assert(memcmp(&notifications[1].body, &original, sizeof(original)) == 0);
    zero_motion(notifications[0].body);
    zero_motion(notifications[2].body);
    assert(!stock_calls);
    balanced();
}
static void test_normal_capture_race(void) {
    reset_case();
    struct zmk_hid_mouse_report_body moving = body(0);
    capture_race = 2;
    assert(zmk_hog_send_mouse_report(&moving) == -ESTALE);
    balanced();
}
static void test_head_backpressure_no_busy_retry(void) {
    reset_case();
    notify_errors[notify_error_count++] = -ENOMEM;
    notify_errors[notify_error_count++] = -EAGAIN;
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    run_ready();
    assert(notify_count == 1 && stats().pending_head && stats().outstanding == 2);
    unsigned int runs = worker_runs, scheduling = schedules;
    run_ready();
    assert(worker_runs == runs && schedules == scheduling);
    assert(snapshot(2, 0) == 0); /* New input does not accelerate failed HEAD. */
    run_ready();
    advance(9);
    assert(notify_count == 1);
    advance(1);
    assert(notify_count == 2 && notifications[1].body.buttons == 1);
    advance(10);
    assert(notify_count == 5 && notifications[2].body.buttons == 1 &&
           notifications[3].body.buttons == 0 && notifications[4].body.buttons == 2);
    assert(stats().notify_errors == 2 && stats().retries == 2);
    assert(!bbp9981_hog_mouse_work.delayed);
    balanced();
}
static void test_security_upgrade_pending_head(void) {
    reset_case();
    notify_errors[notify_error_count++] = -EPERM;
    notify_errors[notify_error_count++] = -EPERM;
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    run_ready();
    assert(notify_count == 1 && security_count == 1 && security_conn == &peers[0]);
    advance(10);
    assert(notify_count == 2 && security_count == 1 && stats().outstanding == 2);
    advance(10);
    assert(notify_count == 4 && notifications[2].body.buttons == 1 &&
           notifications[3].body.buttons == 0);
    assert(stats().security_requests == 1);
    balanced();
}
static void test_security_failure_cancels_group(void) {
    reset_case();
    security_error = -EIO;
    notify_errors[notify_error_count++] = -EPERM;
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    run_ready();
    assert(notify_count == 1 && security_count == 1 && stats().canceled == 2 &&
           stats().last_send_error == -EIO);
    advance(500);
    assert(notify_count == 1);
    balanced();
}
static void test_security_busy_retries(void) {
    reset_case();
    security_error = -EBUSY;
    notify_errors[notify_error_count++] = -EPERM;
    assert(snapshot(1, 0) == 0);
    run_ready();
    advance(10);
    assert(notify_count == 2 && security_count == 1 && stats().notified == 1);
    balanced();
}
static void test_retry_limit_cancel_peer_preserve_other_fifo(void) {
    reset_case();
    notify_default_error = -ENOMEM;
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    active_profile = 1;
    assert(snapshot(2, 1) == 0 && snapshot(0, 1) == 0);
    run_ready();
    for (unsigned int i = 1; i < BBP9981_HOG_MOUSE_MAX_ATTEMPTS; i++) {
        if (i == BBP9981_HOG_MOUSE_MAX_ATTEMPTS - 1) { notify_default_error = 0; }
        /* Keep the 40th old-peer HEAD attempt failing, new-peer reports succeed. */
        if (!notify_default_error) { notify_errors[notify_error_count++] = -ENOMEM; }
        advance(10);
    }
    assert(notify_count == 42 && stats().canceled == 2 && stats().notified == 2);
    for (unsigned int i = 0; i < 40; i++) {
        assert(notifications[i].conn == &peers[0] && notifications[i].body.buttons == 1);
    }
    assert(notifications[40].conn == &peers[1] && notifications[40].body.buttons == 2 &&
           notifications[41].conn == &peers[1] && notifications[41].body.buttons == 0);
    balanced();
}
static void test_runtime_hog_plug_cancels_and_recovers(void) {
    reset_case();
    notify_errors[notify_error_count++] = -ENOMEM;
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    run_ready();
    hog_work_q.plugged = true;
    advance(10);
    assert(notify_count == 1 && stats().canceled == 2 && stats().schedule_errors == 1 &&
           stats().last_send_error == -EBUSY);
    balanced();
    hog_work_q.plugged = false;
    assert(snapshot(2, 0) == 0);
    run_ready();
    assert(notify_count == 2 && notifications[1].body.buttons == 2);
    balanced();
}
static void test_queued_hog_stall_age_cancellation(void) {
    reset_case();
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    hog_work_q.plugged = true; /* Already queued work cannot reveal submit error. */
    run_ready();
    advance(400);
    assert(!notify_count && stats().canceled == 2 && stats().last_send_error == -ETIMEDOUT);
    balanced();
    hog_work_q.plugged = false;
    run_ready();
    assert(!notify_count); /* Orphan queued work cannot replay the old report. */
    balanced();
}
static void test_runtime_hog_stop(void) {
    reset_case();
    notify_errors[notify_error_count++] = -ENOMEM;
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    run_ready();
    hog_work_q.started = false;
    advance(10);
    assert(stats().canceled == 2 && stats().last_send_error == -ENODEV);
    balanced();
}
static void test_producer_failures_balanced(void) {
    reset_case();
    put_error_once = -EIO;
    assert(snapshot(1, 0) == -EIO);
    run_ready();
    balanced();
    schedule_fail_queue = &hog_work_q;
    schedule_fail_error = -EBUSY;
    assert(snapshot(1, 0) == -EBUSY);
    run_ready();
    balanced();
    schedule_fail_queue = &k_sys_work_q;
    schedule_fail_error = -EINVAL;
    assert(snapshot(1, 0) == -EINVAL);
    run_ready();
    assert(stats().accepted == 0 && stats().rejected == 3);
    balanced();
}
static void test_followup_schedule_failure_balanced(void) {
    reset_case();
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    assert(run_one(&bbp9981_hog_mouse_dispatch));
    schedule_fail_queue = &hog_work_q;
    schedule_fail_error = -EIO;
    assert(run_one(&bbp9981_hog_mouse_work));
    assert(notify_count == 1 && stats().notified == 1 && stats().canceled == 1 &&
           stats().last_send_error == -EIO);
    balanced();
}
static void test_notify_disconnected_after_check(void) {
    reset_case();
    notify_errors[notify_error_count++] = -ENOTCONN;
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    run_ready();
    assert(notify_count == 1 && stats().canceled == 2);
    balanced();
}
static void simulate_sdk_wait(struct bt_conn *conn) {
    assert(stats().sending && conn == &peers[0] && conn->refs == 3);
    now_ms = 410;
    expire_timers();
    assert(run_one(&bbp9981_hog_mouse_dispatch));
    assert(stats().sending && stats().outstanding == 2 && conn->refs == 3);
    /* Deadline does not free the still-in-progress SDK call's reference. */
}
static void test_sdk_wait_no_premature_unref(void) {
    reset_case();
    notify_errors[notify_error_count++] = -ENOMEM;
    assert(snapshot(1, 0) == 0 && snapshot(0, 0) == 0);
    during_notify = simulate_sdk_wait;
    run_ready();
    assert(notify_count == 1 && stats().canceled == 2 && stats().last_send_error == -ENOMEM);
    balanced();
}
static void test_legacy_isolation(void) {
    reset_case();
    struct zmk_hid_mouse_report_body report = body(0);
    stock_recurse = true;
    assert(bbp9981_stock_hog_send_mouse_report(&report) == 77 && stock_calls == 2);
    assert(!stats().accepted && !notify_count && !ref_calls);
    assert(zmk_hog_send_mouse_report(&report) == 0);
    run_ready();
    assert(stock_calls == 2 && stats().notified == 1);
    balanced();
}

int main(void) {
#define RUN(test) do { test(); puts("PASS " #test); } while (0)
    RUN(test_static_init_queues);
    RUN(test_fifo_down_up_copy);
    RUN(test_full_no_eviction);
    RUN(test_pending_counts_capacity);
    RUN(test_pinned_peer_after_switch);
    RUN(test_capture_races);
    RUN(test_expected_profile_and_invalid);
    RUN(test_capture_disconnected);
    RUN(test_disconnect_cancels_no_new_peer_replay);
    RUN(test_reconnect_different_object_no_replay);
    RUN(test_normal_motion_same_fifo);
    RUN(test_normal_capture_race);
    RUN(test_head_backpressure_no_busy_retry);
    RUN(test_security_upgrade_pending_head);
    RUN(test_security_failure_cancels_group);
    RUN(test_security_busy_retries);
    RUN(test_retry_limit_cancel_peer_preserve_other_fifo);
    RUN(test_runtime_hog_plug_cancels_and_recovers);
    RUN(test_queued_hog_stall_age_cancellation);
    RUN(test_runtime_hog_stop);
    RUN(test_producer_failures_balanced);
    RUN(test_followup_schedule_failure_balanced);
    RUN(test_notify_disconnected_after_check);
    RUN(test_sdk_wait_no_premature_unref);
    RUN(test_legacy_isolation);
    puts("PASS HOG snapshot: 25 actual-header cases; every conn reference balanced; no hardware I/O");
    return 0;
}
