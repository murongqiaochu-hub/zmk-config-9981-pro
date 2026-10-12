/* SPDX-License-Identifier: MIT */
#ifndef BBP9981_TRACKPAD_HOG_SNAPSHOT_H
#define BBP9981_TRACKPAD_HOG_SNAPSHOT_H

/* Pinned extension: force-include ONLY into ZMK v0.3.0 app/src/hog.c.
 * west.yml pins Zephyr v3.5.0+zmk-fixes. Sources checked:
 * https://github.com/zmkfirmware/zmk/blob/v0.3.0/app/src/hog.c
 * https://github.com/zmkfirmware/zephyr/blob/v3.5.0%2Bzmk-fixes/include/zephyr/bluetooth/gatt.h
 *
 * Include the official declaration BEFORE renaming the stock definition. All
 * external normal mouse sends and button snapshots then use this ONE FIFO.
 * Stock keyboard/consumer behavior is unchanged. The renamed stock mouse code
 * remains in this TU, including its self-recursion, but is never called here.
 * Define BBP9981_HOG_SNAPSHOT_API_ONLY for declarations in another TU; never
 * force-include the implementation into a second TU.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/ble.h>
#include <zmk/hog.h>

#if IS_ENABLED(CONFIG_ZMK_POINTING)

#define BBP9981_HOG_MOUSE_RETRY_MS 10
#define BBP9981_HOG_MOUSE_MAX_ATTEMPTS 40u
#define BBP9981_HOG_MOUSE_MAX_AGE_MS 400

struct bbp9981_hog_mouse_stats {
    uint32_t accepted;
    uint32_t notified;
    uint32_t canceled;
    uint32_t rejected;
    uint32_t queue_full;
    uint32_t stale_captures;
    uint32_t notify_errors;
    uint32_t retries;
    uint32_t security_requests;
    uint32_t schedule_errors;
    unsigned int outstanding;
    int last_enqueue_error;
    int last_send_error;
    bool pending_head;
    bool sending;
};

/* 0 means a body COPY + one captured conn reference were accepted, NOT a host
 * ACK and NOT a completed click. -EAGAIN means full, without eviction/waiting;
 * -ESTALE means the expected/captured active profile did not match;
 * -ENOTCONN means no established captured connection; scheduling errors are
 * returned before acceptance of the first entry. expected_profile must be >= 0.
 * Only this snapshot clears XY/scroll; normal zmk_hog_send_mouse_report does not.
 */
int bbp9981_ble_hid_send_mouse_buttons_snapshot(struct zmk_hid_mouse_report_body *body,
                                             int expected_profile);
int bbp9981_ble_hid_mouse_report_stats_get(struct bbp9981_hog_mouse_stats *out);

#ifndef BBP9981_HOG_SNAPSHOT_API_ONLY

/* Actual BT_GATT_SERVICE_DEFINE type/linkage in the pinned Zephyr, not the
 * dynamic bt_gatt_service type. hog.c defines the const service later. Its
 * keyboard/consumer/mouse characteristic declarations are attrs[5]/[9]/[13].
 */
extern const struct bt_gatt_service_static hog_svc;
extern struct k_work_q hog_work_q;

BUILD_ASSERT(CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE > 0,
             "The pinned mouse FIFO needs at least one entry");

struct bbp9981_hog_mouse_entry {
    struct zmk_hid_mouse_report_body body;
    struct bt_conn *conn;
    int64_t expires_at;
};

/* outstanding includes the removed-but-pending HEAD, so total owned references
 * are bounded by the configured queue size, not queue size plus one.
 */
K_MSGQ_DEFINE(bbp9981_hog_mouse_fifo, sizeof(struct bbp9981_hog_mouse_entry),
              CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE, 4);
static struct k_spinlock bbp9981_hog_mouse_lock;
static struct bbp9981_hog_mouse_stats bbp9981_hog_mouse_stats;
static struct {
    struct bbp9981_hog_mouse_entry entry;
    int64_t retry_at;
    unsigned int attempts;
    bool valid;
    bool sending;
    bool security_requested;
} bbp9981_hog_mouse_head;

static void bbp9981_hog_mouse_worker(struct k_work *work);
static void bbp9981_hog_mouse_dispatcher(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(bbp9981_hog_mouse_work, bbp9981_hog_mouse_worker);
static K_WORK_DELAYABLE_DEFINE(bbp9981_hog_mouse_dispatch, bbp9981_hog_mouse_dispatcher);

static void bbp9981_hog_mouse_unref_batch(struct bt_conn **conns, unsigned int count) {
    for (unsigned int i = 0; i < count; i++) {
        bt_conn_unref(conns[i]);
    }
}

/* All FIFO/head/stat helpers run under the state spinlock; no BT wait/unref is
 * performed there. A short kernel lock/schedule is not a queue-space wait.
 */
static void bbp9981_hog_mouse_load_head_locked(void) {
    if (!bbp9981_hog_mouse_head.valid &&
        k_msgq_get(&bbp9981_hog_mouse_fifo, &bbp9981_hog_mouse_head.entry, K_NO_WAIT) == 0) {
        bbp9981_hog_mouse_head.retry_at = 0;
        bbp9981_hog_mouse_head.attempts = 0;
        bbp9981_hog_mouse_head.security_requested = false;
        bbp9981_hog_mouse_head.valid = true;
    }
}

/* Terminal failure/disconnect cancels HEAD and all already queued reports for
 * that SAME connection (including a queued UP), without replay or rerouting.
 * Other captured peers retain their FIFO order. NULL cancels all unsent entries
 * on a local scheduling failure. A sending HEAD cannot be reclaimed until the
 * SDK call returns. The bounded loop is cleanup, never a retry/busy-wait loop.
 */
static unsigned int bbp9981_hog_mouse_cancel_locked(struct bt_conn *only, int error,
                                                   struct bt_conn **released) {
    unsigned int count = 0;
    if (bbp9981_hog_mouse_head.valid && !bbp9981_hog_mouse_head.sending &&
        (!only || bbp9981_hog_mouse_head.entry.conn == only)) {
        released[count++] = bbp9981_hog_mouse_head.entry.conn;
        bbp9981_hog_mouse_head.valid = false;
    }
    unsigned int queued = k_msgq_num_used_get(&bbp9981_hog_mouse_fifo);
    for (unsigned int i = 0; i < queued; i++) {
        struct bbp9981_hog_mouse_entry entry;
        int ret = k_msgq_get(&bbp9981_hog_mouse_fifo, &entry, K_NO_WAIT);
        __ASSERT_NO_MSG(ret == 0);
        if (!only || entry.conn == only) {
            released[count++] = entry.conn;
        } else {
            ret = k_msgq_put(&bbp9981_hog_mouse_fifo, &entry, K_NO_WAIT);
            __ASSERT_NO_MSG(ret == 0);
        }
        (void)ret; /* Still used when Zephyr assertions are compiled out. */
    }
    bbp9981_hog_mouse_stats.outstanding -= count;
    bbp9981_hog_mouse_stats.canceled += count;
    bbp9981_hog_mouse_stats.last_send_error = error;
    return count;
}

/* The system dispatcher never sends HID. It supplies the delay/watchdog while
 * only K_NO_WAIT submissions target hog_work_q. In pinned work.c a NONZERO
 * delay to a plugged/unstarted target silently loses work at timeout expiry;
 * using that directly on hog_work_q would strand conn references forever.
 * k_sys_work_q must remain the SDK's running, non-plugged system queue.
 */
static void bbp9981_hog_mouse_dispatcher(struct k_work *work) {
    (void)work;
    struct bt_conn *released[CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE];
    unsigned int count = 0;
    k_spinlock_key_t key = k_spin_lock(&bbp9981_hog_mouse_lock);
    if (bbp9981_hog_mouse_stats.outstanding && !bbp9981_hog_mouse_head.sending) {
        bbp9981_hog_mouse_load_head_locked();
        if (bbp9981_hog_mouse_head.valid &&
            k_uptime_get() >= bbp9981_hog_mouse_head.entry.expires_at) {
            count += bbp9981_hog_mouse_cancel_locked(bbp9981_hog_mouse_head.entry.conn,
                                                    -ETIMEDOUT, released + count);
            bbp9981_hog_mouse_load_head_locked();
        }
        if (bbp9981_hog_mouse_head.valid &&
            k_uptime_get() >= bbp9981_hog_mouse_head.retry_at) {
            int ret = k_work_schedule_for_queue(&hog_work_q, &bbp9981_hog_mouse_work, K_NO_WAIT);
            if (ret < 0) {
                bbp9981_hog_mouse_stats.schedule_errors++;
                count += bbp9981_hog_mouse_cancel_locked(NULL, ret, released + count);
            }
        }
    }
    if (bbp9981_hog_mouse_stats.outstanding) {
        int ret = k_work_schedule_for_queue(&k_sys_work_q, &bbp9981_hog_mouse_dispatch,
                                           K_MSEC(BBP9981_HOG_MOUSE_RETRY_MS));
        if (ret < 0) {
            bbp9981_hog_mouse_stats.schedule_errors++;
            count += bbp9981_hog_mouse_cancel_locked(NULL, ret, released + count);
        }
    }
    k_spin_unlock(&bbp9981_hog_mouse_lock, key);
    bbp9981_hog_mouse_unref_batch(released, count);
}

static unsigned int bbp9981_hog_mouse_followup_locked(struct bt_conn **released) {
    if (!bbp9981_hog_mouse_stats.outstanding) {
        return 0;
    }
    int ret = k_work_schedule_for_queue(&k_sys_work_q, &bbp9981_hog_mouse_dispatch,
                                       K_MSEC(BBP9981_HOG_MOUSE_RETRY_MS));
    if (ret >= 0) {
        bbp9981_hog_mouse_load_head_locked();
        if (k_uptime_get() >= bbp9981_hog_mouse_head.retry_at) {
            ret = k_work_schedule_for_queue(&hog_work_q, &bbp9981_hog_mouse_work, K_NO_WAIT);
        }
    }
    if (ret < 0) {
        bbp9981_hog_mouse_stats.schedule_errors++;
        return bbp9981_hog_mouse_cancel_locked(NULL, ret, released);
    }
    return 0;
}

/* ONE notify attempt per invocation, on the original HOG work queue. Failed
 * HEAD stays pending until a >=10ms delay, success, disconnect, age/attempt
 * limit, or scheduling fault. New producers cannot accelerate the retry or let
 * UP pass it. SDK bt_gatt_notify_cb may itself wait for BT resources here:
 * only the producer is nonblocking, not all lower layers. The 400ms admission
 * age/40 attempts bound local retries, not an in-progress SDK wait or host ACK.
 */
static void bbp9981_hog_mouse_worker(struct k_work *work) {
    (void)work;
    struct bt_conn *released[CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE];
    unsigned int count = 0;
    k_spinlock_key_t key = k_spin_lock(&bbp9981_hog_mouse_lock);
    bbp9981_hog_mouse_load_head_locked();
    if (!bbp9981_hog_mouse_head.valid ||
        k_uptime_get() < bbp9981_hog_mouse_head.retry_at) {
        k_spin_unlock(&bbp9981_hog_mouse_lock, key);
        return;
    }
    if (k_uptime_get() >= bbp9981_hog_mouse_head.entry.expires_at) {
        count = bbp9981_hog_mouse_cancel_locked(bbp9981_hog_mouse_head.entry.conn,
                                               -ETIMEDOUT, released);
        count += bbp9981_hog_mouse_followup_locked(released + count);
        k_spin_unlock(&bbp9981_hog_mouse_lock, key);
        bbp9981_hog_mouse_unref_batch(released, count);
        return;
    }
    struct bbp9981_hog_mouse_entry entry = bbp9981_hog_mouse_head.entry;
    bbp9981_hog_mouse_head.sending = true;
    k_spin_unlock(&bbp9981_hog_mouse_lock, key);

    struct bt_conn_info info;
    bool terminal = false;
    bool attempted_notify = false;
    int err;
    if (bt_conn_get_info(entry.conn, &info) != 0 || info.state != BT_CONN_STATE_CONNECTED) {
        err = -ENOTCONN;
        terminal = true;
    } else if (hog_svc.attr_count <= 13) {
        err = -ENOTSUP;
        terminal = true;
    } else {
        struct bt_gatt_notify_params params = {
            .attr = &hog_svc.attrs[13],
            .data = &entry.body,
            .len = sizeof(entry.body),
        };
        attempted_notify = true;
        err = bt_gatt_notify_cb(entry.conn, &params);
        terminal = (err == -ENOTCONN || err == -ECONNRESET);
    }
    bool notify_failed = attempted_notify && err != 0;
    if (err == -EPERM) {
        key = k_spin_lock(&bbp9981_hog_mouse_lock);
        bool upgrade = !bbp9981_hog_mouse_head.security_requested;
        bbp9981_hog_mouse_head.security_requested = true;
        if (upgrade) {
            bbp9981_hog_mouse_stats.security_requests++;
        }
        k_spin_unlock(&bbp9981_hog_mouse_lock, key);
        if (upgrade) {
            int security_err = bt_conn_set_security(entry.conn, BT_SECURITY_L2);
            if (security_err && security_err != -EBUSY && security_err != -EALREADY) {
                err = security_err;
                terminal = true;
            }
        }
    }

    key = k_spin_lock(&bbp9981_hog_mouse_lock);
    bbp9981_hog_mouse_head.sending = false;
    if (notify_failed) {
        bbp9981_hog_mouse_stats.notify_errors++;
    }
    if (!err) {
        released[count++] = entry.conn;
        bbp9981_hog_mouse_head.valid = false;
        bbp9981_hog_mouse_stats.outstanding--;
        bbp9981_hog_mouse_stats.notified++;
        /* Do not clear last_send_error: asynchronous loss remains observable. */
    } else {
        bbp9981_hog_mouse_stats.last_send_error = err;
        bbp9981_hog_mouse_head.attempts++;
        if (terminal || bbp9981_hog_mouse_head.attempts >= BBP9981_HOG_MOUSE_MAX_ATTEMPTS ||
            k_uptime_get() >= entry.expires_at) {
            count += bbp9981_hog_mouse_cancel_locked(entry.conn, err, released + count);
        } else {
            bbp9981_hog_mouse_stats.retries++;
            bbp9981_hog_mouse_head.retry_at = k_uptime_get() + BBP9981_HOG_MOUSE_RETRY_MS;
        }
    }
    count += bbp9981_hog_mouse_followup_locked(released + count);
    k_spin_unlock(&bbp9981_hog_mouse_lock, key);
    bbp9981_hog_mouse_unref_batch(released, count);
}

static int bbp9981_hog_mouse_reject(int error) {
    k_spinlock_key_t key = k_spin_lock(&bbp9981_hog_mouse_lock);
    bbp9981_hog_mouse_stats.rejected++;
    bbp9981_hog_mouse_stats.stale_captures += (error == -ESTALE);
    bbp9981_hog_mouse_stats.last_enqueue_error = error;
    k_spin_unlock(&bbp9981_hog_mouse_lock, key);
    return error;
}

static int bbp9981_hog_mouse_enqueue(const struct zmk_hid_mouse_report_body *body,
                                   int expected_profile) {
    if (!body) {
        return bbp9981_hog_mouse_reject(-EINVAL);
    }
    struct bbp9981_hog_mouse_entry entry = {.body = *body};
    int profile = zmk_ble_active_profile_index();
    if (profile < 0 || (expected_profile >= 0 && profile != expected_profile)) {
        return bbp9981_hog_mouse_reject(-ESTALE);
    }
    /* Official getter supplies exactly one owned reference. Validate BOTH the
     * returned peer's profile and the active index around capture. A switch
     * after this validation is allowed: the report remains pinned to old conn.
     * Public index APIs do not expose an ABA/endpoint generation fence.
     */
    entry.conn = zmk_ble_active_profile_conn();
    if (!entry.conn) {
        return bbp9981_hog_mouse_reject(zmk_ble_active_profile_index() == profile ? -ENOTCONN
                                                                              : -ESTALE);
    }
    const bt_addr_le_t *dst = bt_conn_get_dst(entry.conn);
    int err = 0;
    struct bt_conn_info info;
    if (!dst || zmk_ble_profile_index(dst) != profile ||
        zmk_ble_active_profile_index() != profile) {
        err = -ESTALE;
    } else if (bt_conn_get_info(entry.conn, &info) != 0 || info.state != BT_CONN_STATE_CONNECTED) {
        err = -ENOTCONN;
    }
    if (err) {
        bt_conn_unref(entry.conn);
        return bbp9981_hog_mouse_reject(err);
    }
    entry.expires_at = k_uptime_get() + BBP9981_HOG_MOUSE_MAX_AGE_MS;
    k_spinlock_key_t key = k_spin_lock(&bbp9981_hog_mouse_lock);
    if (bbp9981_hog_mouse_stats.outstanding >= CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE) {
        bbp9981_hog_mouse_stats.queue_full++;
        err = -EAGAIN;
    } else if (!bbp9981_hog_mouse_stats.outstanding) {
        /* Static K_MSGQ/K_WORK objects require no custom init priority/hook.
         * K_NO_WAIT validates that BOTH SDK queues have actually started. The
         * workers cannot consume the entry until this spinlock is released.
         */
        err = k_work_schedule_for_queue(&k_sys_work_q, &bbp9981_hog_mouse_dispatch, K_NO_WAIT);
        if (err >= 0) {
            err = k_work_schedule_for_queue(&hog_work_q, &bbp9981_hog_mouse_work, K_NO_WAIT);
        }
        if (err < 0) {
            bbp9981_hog_mouse_stats.schedule_errors++;
        }
    }
    if (err >= 0) {
        err = k_msgq_put(&bbp9981_hog_mouse_fifo, &entry, K_NO_WAIT);
    }
    if (!err) {
        bbp9981_hog_mouse_stats.outstanding++;
        bbp9981_hog_mouse_stats.accepted++;
    } else {
        bbp9981_hog_mouse_stats.rejected++;
    }
    bbp9981_hog_mouse_stats.last_enqueue_error = err;
    k_spin_unlock(&bbp9981_hog_mouse_lock, key);
    if (err) {
        bt_conn_unref(entry.conn);
    }
    return err;
}

int zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *body) {
    return bbp9981_hog_mouse_enqueue(body, -1);
}

int bbp9981_ble_hid_send_mouse_buttons_snapshot(struct zmk_hid_mouse_report_body *body,
                                             int expected_profile) {
    if (!body || expected_profile < 0) {
        return bbp9981_hog_mouse_reject(-EINVAL);
    }
    struct zmk_hid_mouse_report_body snapshot = {.buttons = body->buttons};
    return bbp9981_hog_mouse_enqueue(&snapshot, expected_profile);
}

int bbp9981_ble_hid_mouse_report_stats_get(struct bbp9981_hog_mouse_stats *out) {
    if (!out) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&bbp9981_hog_mouse_lock);
    *out = bbp9981_hog_mouse_stats;
    out->pending_head = bbp9981_hog_mouse_head.valid;
    out->sending = bbp9981_hog_mouse_head.sending;
    k_spin_unlock(&bbp9981_hog_mouse_lock, key);
    return 0;
}

/* Intentionally left defined for the rest of hog.c, not for any other TU. */
#define zmk_hog_send_mouse_report bbp9981_stock_hog_send_mouse_report

#endif /* !BBP9981_HOG_SNAPSHOT_API_ONLY */
#endif /* CONFIG_ZMK_POINTING */
#endif /* BBP9981_TRACKPAD_HOG_SNAPSHOT_H */
