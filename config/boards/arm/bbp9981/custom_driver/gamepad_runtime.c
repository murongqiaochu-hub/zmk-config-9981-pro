/* SPDX-License-Identifier: MIT
 * MEDIA-only standard HID gamepad. Producers never wait for transport I/O.
 * Absolute snapshots keep FIFO edges; a dedicated worker owns all destination
 * references and releases the OLD captured peer before admitting a new context.
 * SDK acceptance is not a host ACK. A disconnected peer cannot receive an UP.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zephyr/bluetooth/conn.h>
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif
#include "gamepad_runtime.h"
#if IS_ENABLED(CONFIG_A320_MICROTAP)
#include "trackpad_microtap_runtime.h"
#endif
#if IS_ENABLED(CONFIG_A320_TAP_OUTPUT)
#include "trackpad_tap_output.h"
#endif

LOG_MODULE_REGISTER(bbp_gamepad, CONFIG_ZMK_LOG_LEVEL);
#define GP_QUEUE_CAPACITY 32U
#define GP_FRAME_MAX_AGE_MS 250
#define GP_RETRY_MS 10
#define GP_RELEASE_MAX_MS 400
#define GP_MAX_ATTEMPTS 40U

struct gp_target {
    uint8_t transport;
    uint32_t usb_epoch;
    struct bt_conn *conn;
    uint8_t profile;
};
struct gp_frame {
    struct gp_target target;
    struct bbp_gamepad_report_body body;
    uint32_t context;
    int64_t born;
    uint8_t attempts;
    bool security_requested;
};
static struct bbp_gamepad_state gamepad;
static struct k_spinlock state_lock;
static atomic_t context_epoch;
static atomic_t ready;
static atomic_t idle_rearm;
static struct gp_frame ring[GP_QUEUE_CAPACITY];
static uint8_t head, count;
static uint8_t state_transport;
static uint8_t state_profile;
static uint32_t state_usb_epoch, state_context;
/* Identity token only, never dereferenced; frame/temporary refs own lifetimes. */
static uintptr_t state_conn_token;
static bool state_destination_valid;
static atomic_t context_was_game;
static atomic_t pointer_mode_epoch;
static struct k_work_q gamepad_work_q;
K_THREAD_STACK_DEFINE(gamepad_stack, 2048);
static void pump(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(pump_work, pump);

#if IS_ENABLED(CONFIG_ZMK_USB)
uint32_t bbp9981_usb_hid_connection_epoch(void);
#endif

uint32_t bbp_gamepad_context_epoch(void) { return (uint32_t)atomic_get(&context_epoch); }
bool bbp_gamepad_context_valid(uint32_t expected) {
    return expected == bbp_gamepad_context_epoch();
}
bool bbp_gamepad_layer_active(void) {
    /* The SDK returns an order index; Studio can reorder stable layer IDs. */
    return zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()) == BBP_GAMEPAD_LAYER;
}
uint32_t bbp_gamepad_pointer_epoch(void) { return (uint32_t)atomic_get(&pointer_mode_epoch); }
static void wake_worker(uint32_t delay) {
    if (atomic_get(&ready)) {
        int ret = k_work_reschedule_for_queue(&gamepad_work_q, &pump_work, K_MSEC(delay));
        if (ret < 0) { LOG_ERR("Gamepad scheduling failed: %d", ret); }
    }
}
static void defer_worker(uint32_t delay) {
    if (atomic_get(&ready)) {
        /* A producer may already have queued an immediate wake while we ran.
         * Do not overwrite that with a retry/idle timer. */
        int ret = k_work_schedule_for_queue(&gamepad_work_q, &pump_work, K_MSEC(delay));
        if (ret < 0) { LOG_ERR("Gamepad deferred scheduling failed: %d", ret); }
    }
}
void bbp_gamepad_transport_invalidate(void) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bbp_gamepad_reset(&gamepad);
    state_destination_valid = false;
    atomic_inc(&context_epoch);
    k_spin_unlock(&state_lock, key);
    wake_worker(0);
}
static void target_drop(struct gp_target *target) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (target->conn) { bt_conn_unref(target->conn); }
#endif
    target->conn = NULL;
}
static bool target_capture(struct gp_target *out) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    *out = (struct gp_target){.transport = (uint8_t)endpoint.transport};
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (endpoint.transport == ZMK_TRANSPORT_USB) {
        out->usb_epoch = bbp9981_usb_hid_connection_epoch();
        return zmk_usb_is_hid_ready();
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (endpoint.transport == ZMK_TRANSPORT_BLE) {
        out->profile = endpoint.ble.profile_index;
        out->conn = zmk_ble_active_profile_conn();
        return out->conn != NULL;
    }
#endif
    return false;
}
static bool target_selected(const struct gp_target *target) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    if ((uint8_t)endpoint.transport != target->transport) { return false; }
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (endpoint.transport == ZMK_TRANSPORT_USB) {
        return target->usb_epoch == bbp9981_usb_hid_connection_epoch();
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (endpoint.transport == ZMK_TRANSPORT_BLE) {
        if (endpoint.ble.profile_index != target->profile) { return false; }
        struct bt_conn *active = zmk_ble_active_profile_conn();
        bool same = active && active == target->conn;
        if (active) { bt_conn_unref(active); }
        return same;
    }
#endif
    return false;
}
/* Called under state_lock, no SDK/connection lookup in these helpers. */
static bool state_matches(const struct gp_target *target, uint32_t epoch) {
    return state_destination_valid && state_context == epoch &&
        state_transport == target->transport && state_profile == target->profile &&
        state_usb_epoch == target->usb_epoch && state_conn_token == (uintptr_t)target->conn;
}
static void state_adopt(const struct gp_target *target, uint32_t epoch) {
    state_destination_valid = true; state_context = epoch;
    state_transport = target->transport; state_profile = target->profile;
    state_usb_epoch = target->usb_epoch; state_conn_token = (uintptr_t)target->conn;
}
static uint32_t expire_motion(void) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    int64_t now = k_uptime_get();
    (void)bbp_gamepad_expire(&gamepad, now);
    uint32_t remaining = bbp_gamepad_motion_remaining_ms(&gamepad, now);
    k_spin_unlock(&state_lock, key);
    return remaining;
}
void bbp_gamepad_trackpad_poll(int16_t dx, int16_t dy, bool valid, int64_t sample_time) {
    if (!bbp_gamepad_layer_active() || !atomic_get(&ready)) { return; }
    if (!valid || sample_time < 0 || sample_time > k_uptime_get() ||
        k_uptime_get() - sample_time >= BBP_GAMEPAD_STICK_QUIET_MS) {
        k_spinlock_key_t key = k_spin_lock(&state_lock);
        bool changed = bbp_gamepad_motion_cancel(&gamepad);
        k_spin_unlock(&state_lock, key);
        if (changed) { wake_worker(0); }
        return;
    }
    uint32_t epoch = bbp_gamepad_context_epoch();
    struct gp_target target;
    if (!target_capture(&target)) {
        target_drop(&target); bbp_gamepad_transport_invalidate(); return;
    }
    bool game_now = bbp_gamepad_layer_active();
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool current = bbp_gamepad_context_valid(epoch) && game_now;
    bool mismatched = current && state_destination_valid && !state_matches(&target, epoch);
    bool changed = false;
    uint32_t remaining = 0;
    if (current && !mismatched) {
        /* A getter/worker may have observed a later clock tick while capture
         * ran. Commit on the lock's fresh clock, not a spuriously backwards
         * sample timestamp. Samples already quiet-aged were rejected above. */
        int64_t commit_time = k_uptime_get();
        if (commit_time < sample_time || commit_time - sample_time >= BBP_GAMEPAD_STICK_QUIET_MS) {
            changed = bbp_gamepad_motion_cancel(&gamepad);
        } else { changed = bbp_gamepad_motion(&gamepad, dx, dy, commit_time); }
        remaining = bbp_gamepad_motion_remaining_ms(&gamepad, commit_time);
        if (changed || remaining) { state_adopt(&target, epoch); }
    }
    k_spin_unlock(&state_lock, key);
    target_drop(&target);
    if (mismatched) { bbp_gamepad_transport_invalidate(); }
    else if (changed || remaining) { wake_worker(0); }
}
void bbp_gamepad_get_report(uint8_t transport, struct bbp_gamepad_report_body *out) {
    if (!out) { return; }
    *out = bbp_gamepad_neutral();
    uint32_t epoch = bbp_gamepad_context_epoch();
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    bool active = bbp_gamepad_layer_active() && (uint8_t)endpoint.transport == transport;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    struct bt_conn *active_conn = transport == ZMK_TRANSPORT_BLE ? zmk_ble_active_profile_conn() : NULL;
#endif
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    (void)bbp_gamepad_expire(&gamepad, k_uptime_get());
    bool owner = state_destination_valid && state_context == epoch &&
                 state_transport == transport;
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (transport == ZMK_TRANSPORT_USB) {
        owner = owner && state_usb_epoch == bbp9981_usb_hid_connection_epoch();
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (transport == ZMK_TRANSPORT_BLE) {
        owner = owner && active_conn && state_profile == endpoint.ble.profile_index &&
                state_conn_token == (uintptr_t)active_conn;
    }
#endif
    if (active && owner) { *out = bbp_gamepad_snapshot(&gamepad); }
    k_spin_unlock(&state_lock, key);
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (active_conn) { bt_conn_unref(active_conn); }
#endif
    if (!bbp_gamepad_context_valid(epoch)) { *out = bbp_gamepad_neutral(); }
}
static int target_send(struct gp_frame *frame) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (frame->target.transport == ZMK_TRANSPORT_USB) {
        return bbp_gamepad_usb_send(&frame->body, frame->target.usb_epoch, frame->context);
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (frame->target.transport == ZMK_TRANSPORT_BLE) {
        int ret = bbp_gamepad_ble_send(frame->target.conn, &frame->body, frame->context);
        if ((ret == -EPERM || ret == -EACCES) && !frame->security_requested) {
            frame->security_requested = true;
            (void)bt_conn_set_security(frame->target.conn, BT_SECURITY_L2);
        }
        return ret;
    }
#endif
    return -ENOTSUP;
}
static bool terminal_error(int ret) {
    return ret == -ENODEV || ret == -ENOTCONN || ret == -ESTALE || ret == -ENOTSUP || ret == -EINVAL;
}
static bool pop_frame(struct gp_frame *out) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool found = count != 0U;
    if (found) {
        *out = ring[head];
        head = (uint8_t)((head + 1U) % GP_QUEUE_CAPACITY);
        count--;
    }
    k_spin_unlock(&state_lock, key);
    return found;
}
static bool queue_empty(void) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool empty = count == 0U;
    k_spin_unlock(&state_lock, key);
    return empty;
}

/* Only analog motion is coalesced. Capture outside the spinlock, then recheck
 * the discrete FIFO while copying; a later button edge cannot jump ahead. */
static bool coalesced_frame(struct gp_frame *out, const struct bbp_gamepad_report_body *accepted) {
    *out = (struct gp_frame){0};
    uint32_t epoch = bbp_gamepad_context_epoch();
    if (!bbp_gamepad_layer_active() || !target_capture(&out->target)) {
        target_drop(&out->target); return false;
    }
    bool game_now = bbp_gamepad_layer_active();
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    (void)bbp_gamepad_expire(&gamepad, k_uptime_get());
    struct bbp_gamepad_report_body current = bbp_gamepad_snapshot(&gamepad);
    bool changed = count == 0U && bbp_gamepad_context_valid(epoch) &&
        game_now && state_matches(&out->target, epoch) &&
        !bbp_gamepad_is_neutral(&current) && (!accepted || memcmp(accepted, &current, sizeof(current)));
    if (changed) {
        out->body = current; out->context = epoch; out->born = k_uptime_get();
    }
    k_spin_unlock(&state_lock, key);
    if (!changed) { target_drop(&out->target); }
    return changed;
}
static void refresh_axes(struct gp_frame *frame) {
    bool game_now = bbp_gamepad_layer_active();
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    struct bbp_gamepad_report_body current = bbp_gamepad_neutral();
    (void)bbp_gamepad_expire(&gamepad, k_uptime_get());
    if (bbp_gamepad_context_valid(frame->context) && state_matches(&frame->target, frame->context) &&
        game_now) { current = bbp_gamepad_snapshot(&gamepad); }
    /* Keep captured button/trigger/hat DOWN/UP edges; only axes follow newest
     * live motion. An expired stick must not hitchhike in a delayed key frame. */
    frame->body.x = current.x; frame->body.y = current.y;
    frame->body.z = current.z; frame->body.rz = current.rz;
    k_spin_unlock(&state_lock, key);
}

/* Only this dedicated worker accesses pending/last; never the input callback.
 * `last` retains the destination owning a potentially accepted non-neutral
 * report. On exit/switch the captured old peer gets neutral, never the new one. */
static struct gp_frame pending, last;
static bool pending_valid, last_valid, releasing;
static int64_t release_started;
static uint32_t initialized_epoch = UINT32_MAX;
static uint32_t idle_init_epoch = UINT32_MAX;
static uint8_t idle_init_attempts;
static bool idle_security_requested;
static int64_t idle_next_probe;
static void pump(struct k_work *work) {
    (void)work;
    if (atomic_cas(&idle_rearm, 1, 0)) {
        idle_init_attempts = 0; idle_next_probe = 0;
        /* Readiness rearms the budget, not the once-per-context L2 flag. */
    }
    unsigned int drained = 0U;
    for (; drained < 8U; ++drained) {
        uint32_t epoch = bbp_gamepad_context_epoch();
        (void)expire_motion();
        bool game = bbp_gamepad_layer_active();
        bool should_release = last_valid && (!bbp_gamepad_context_valid(last.context) ||
                                              !game || !target_selected(&last.target));
        if (last_valid && queue_empty() && !pending_valid && game) {
            struct bbp_gamepad_report_body current;
            bbp_gamepad_get_report(last.target.transport, &current);
            /* Reconcile a dropped/expired UP: never leave the last DOWN held. */
            should_release = should_release || bbp_gamepad_is_neutral(&current);
        }
        if (should_release) {
            if (!releasing) { releasing = true; release_started = k_uptime_get(); last.attempts = 0; }
            last.body = bbp_gamepad_neutral();
            int ret = target_send(&last);
            last.attempts++;
            if (ret == 0 || terminal_error(ret) || last.attempts >= GP_MAX_ATTEMPTS ||
                k_uptime_get() - release_started >= GP_RELEASE_MAX_MS) {
                if (ret) { LOG_WRN("Old gamepad peer neutral not accepted: %d", ret); }
                target_drop(&last.target); last_valid = releasing = false;
            } else { defer_worker(GP_RETRY_MS); return; }
            continue;
        }
        if (!pending_valid) { pending_valid = pop_frame(&pending); }
        if (!pending_valid && game) {
            pending_valid = coalesced_frame(&pending, last_valid ? &last.body : NULL);
        }
        if (!pending_valid && !queue_empty()) { wake_worker(0); return; }
        if (!pending_valid) {
            /* Enumerated gamepads must centre even before the first game key,
             * including outside MEDIA. Hat0 would mean UP, not neutral. CCC,
             * connection, endpoint and protocol callbacks invalidate the epoch
             * and rearm this bounded handshake; never replay held actions. */
            if (idle_init_epoch != epoch) {
                idle_init_epoch = epoch; idle_init_attempts = 0;
                idle_security_requested = false; idle_next_probe = 0;
            }
            int64_t probe_now = k_uptime_get();
            if (initialized_epoch != epoch && probe_now < idle_next_probe) {
                defer_worker((uint32_t)(idle_next_probe - probe_now));
            } else if (initialized_epoch != epoch) {
                struct gp_frame initial = {.body=bbp_gamepad_neutral(), .context=epoch,
                    .born=k_uptime_get(), .security_requested=idle_security_requested};
                bool captured = target_capture(&initial.target);
                if (captured) {
                    int ret = target_send(&initial);
                    idle_security_requested = initial.security_requested;
                    if (idle_init_attempts < GP_MAX_ATTEMPTS) { idle_init_attempts++; }
                    if (ret == 0) { initialized_epoch = epoch; }
                    else if (!terminal_error(ret)) {
                        /* CCC cfg_changed aggregates all connections. After
                         * the short budget, probe ONLY a fresh neutral at 2Hz
                         * so a later per-peer subscription cannot strand init.
                         * No old frame/ref is retained; L2 is still once/epoch. */
                        uint32_t delay = idle_init_attempts < GP_MAX_ATTEMPTS ? GP_RETRY_MS : 500U;
                        idle_next_probe = k_uptime_get() + delay;
                        defer_worker(delay);
                    }
                }
                target_drop(&initial.target);
            }
            /* While a DOWN is owned, periodically reconcile with canonical
             * state and retry a failed release even without another key edge. */
            uint32_t motion_left = expire_motion();
            if (last_valid || motion_left) {
                defer_worker(motion_left && motion_left < 50U ? motion_left : 50U);
            }
            return;
        }
        bool stale = pending.context != epoch || !game || !target_selected(&pending.target) ||
                     k_uptime_get() - pending.born > GP_FRAME_MAX_AGE_MS;
        if (stale) {
            bool cancel_current = pending.context == epoch;
            target_drop(&pending.target); pending_valid = false;
            /* Never let an expired held action reappear in a later snapshot
             * merely because another key was pressed after transport recovery. */
            if (cancel_current) { bbp_gamepad_transport_invalidate(); }
            continue;
        }
        if (initialized_epoch != epoch) {
            struct gp_frame initial = pending;
            initial.body = bbp_gamepad_neutral();
            int ret = target_send(&initial);
            pending.security_requested = initial.security_requested;
            if (ret == 0) { initialized_epoch = epoch; }
            else {
                pending.attempts++;
                if (terminal_error(ret) || pending.attempts >= GP_MAX_ATTEMPTS) {
                    target_drop(&pending.target); pending_valid = false;
                    bbp_gamepad_transport_invalidate();
                }
                defer_worker(GP_RETRY_MS); return;
            }
        }
        refresh_axes(&pending);
        int ret = target_send(&pending);
        pending.attempts++;
        if (ret == 0) {
            if (last_valid) { target_drop(&last.target); }
            last_valid = !bbp_gamepad_is_neutral(&pending.body);
            if (last_valid) {
                last = pending;
                pending.target.conn = NULL; /* Transfer one reference, not clone. */
            }
            target_drop(&pending.target); pending_valid = false; releasing = false;
        } else if (terminal_error(ret) || pending.attempts >= GP_MAX_ATTEMPTS) {
            LOG_WRN("Gamepad snapshot rejected: %d", ret);
            target_drop(&pending.target); pending_valid = false;
            bbp_gamepad_transport_invalidate();
        } else { defer_worker(GP_RETRY_MS); return; }
    }
    wake_worker(0); /* Bounded FIFO drain, no busy wait for a report or peer. */
}

int bbp_gamepad_key_event(uint32_t position, uint32_t action, bool pressed, int64_t timestamp) {
    if ((pressed && action >= BBP_GAMEPAD_ACTION_COUNT) || position >= BBP_GAMEPAD_POSITIONS) {
        return -EINVAL;
    }
    if (!bbp_gamepad_layer_active()) { return 0; }
    uint32_t epoch = bbp_gamepad_context_epoch();
    struct gp_target target;
    if (!target_capture(&target)) {
        target_drop(&target); bbp_gamepad_transport_invalidate();
        return 0;
    }
    int64_t now = k_uptime_get();
    if (timestamp < 0 || timestamp > now || now - timestamp > GP_FRAME_MAX_AGE_MS) {
        target_drop(&target); bbp_gamepad_transport_invalidate();
        return 0;
    }
    bool game_now = bbp_gamepad_layer_active();
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool current = bbp_gamepad_context_valid(epoch) && game_now;
    bool mismatched = current && state_destination_valid && !state_matches(&target, epoch);
    bool changed = current && !mismatched &&
                   bbp_gamepad_key(&gamepad, position, action, pressed);
    bool full = changed && count == GP_QUEUE_CAPACITY;
    if (changed && !full) {
        (void)bbp_gamepad_expire(&gamepad, k_uptime_get());
        state_adopt(&target, epoch);
        uint8_t tail = (uint8_t)((head + count) % GP_QUEUE_CAPACITY);
        ring[tail] = (struct gp_frame){.target=target, .body=bbp_gamepad_snapshot(&gamepad),
                                      .context=epoch, .born=now};
        count++;
        target.conn = NULL; /* Ring now owns the captured reference. */
    }
    k_spin_unlock(&state_lock, key);
    target_drop(&target);
    if (mismatched) { bbp_gamepad_transport_invalidate(); }
    else if (full) {
        LOG_WRN("Gamepad FIFO full: cancel context, release old owner, require fresh keys");
        bbp_gamepad_transport_invalidate();
    } else if (changed) { wake_worker(0); }
    return 0;
}
static int context_changed(const zmk_event_t *event) {
    (void)event;
    bool game = bbp_gamepad_layer_active();
    bool was_game = atomic_set(&context_was_game, game) != 0;
    if (game != was_game) { atomic_inc(&pointer_mode_epoch); }
#if IS_ENABLED(CONFIG_A320_TAP_OUTPUT)
    /* Spinlock + schedule only, no HID/SDK call or wait. Stop a bridge job
     * already queued before enter, not merely the next sensor poll's gesture. */
    if (game || was_game) { (void)tp_tap_output_cancel(); }
#endif
#if IS_ENABLED(CONFIG_A320_MICROTAP)
    /* Also cancel a game enter+exit entirely between sensor polls. No gesture
     * threshold or saved ON/OFF setting changes; this is pointer ownership. */
    if (game || was_game) { tp_microtap_runtime_interrupt(); }
#endif
    bbp_gamepad_transport_invalidate();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(bbp_gamepad_context, context_changed);
ZMK_SUBSCRIPTION(bbp_gamepad_context, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(bbp_gamepad_context, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(bbp_gamepad_context, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(bbp_gamepad_context, zmk_ble_active_profile_changed);
static void gp_connected(struct bt_conn *conn, uint8_t error) {
    (void)conn; if (!error) { bbp_gamepad_transport_invalidate(); }
}
static void gp_disconnected(struct bt_conn *conn, uint8_t reason) {
    (void)conn; (void)reason; bbp_gamepad_transport_invalidate();
}
static void gp_security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    (void)conn;
    if (err || level < BT_SECURITY_L2) { bbp_gamepad_transport_invalidate(); }
    else { atomic_set(&idle_rearm, 1); wake_worker(0); }
}
BT_CONN_CB_DEFINE(bbp_gamepad_connection) = {
    .connected = gp_connected, .disconnected = gp_disconnected, .security_changed = gp_security_changed,
};
#endif
static int gamepad_init(void) {
    static const struct k_work_queue_config cfg = {.name="9981 Gamepad Send Work"};
    k_work_queue_start(&gamepad_work_q, gamepad_stack, K_THREAD_STACK_SIZEOF(gamepad_stack), 5, &cfg);
    atomic_set(&ready, 1);
    wake_worker(0);
    LOG_INF("GAMEPAD_POLICY report=4 body=9 layer=3 hat=8 xbox=13 axes=6");
    LOG_INF("GAMEPAD_STICK gain=%u deadzone=%u quiet=%u default=left",
            BBP_GAMEPAD_STICK_GAIN, BBP_GAMEPAD_STICK_DEADZONE, BBP_GAMEPAD_STICK_QUIET_MS);
    return 0;
}
SYS_INIT(gamepad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
