/*
 * SPDX-License-Identifier: MIT
 *
 * Actual ZMK v0.3.0 output bridge, NOT an implementation of trackpad_click.h's
 * stronger abstract no-evict/atomic-epoch ops contract.
 *
 * Verified v0.3.0 (edf5c0814fd3ea202e43aad2d68fd32e882a518c):
 * app/src/hid.c: button index 0, one reference per press/release, 5 refcounts.
 * app/src/pointing/input_listener.c: official input path uses those same APIs.
 * mouse_button_state_changed has no output listener in this tag; raising it
 * alone does not send a click. We therefore use the public HID reference API.
 * app/src/hog.c: the STOCK public HOG API may wait 100ms, evict oldest, recursively
 * retry, and resolve the ACTIVE profile at drain. It is intentionally NOT used
 * as a fallback here. BLE requires an explicitly provided SDK extension capturing
 * the expected profile's connection and enqueueing without eviction, sharing the
 * ordered mouse queue with normal reports. Absent extension = ENOTSUP.
 * app/src/usb_hid.c: the STOCK send_mouse_report() reads mutable global XY/wheel
 * after a 30ms semaphore wait. USB requires a snapshot SDK extension using that
 * same SDK HID path/semaphore, not a global-report swap or a private-sem bypass.
 * The parent integration supplies the strong implementations; these weak symbols
 * are clearly NOT claimed to be official v0.3 APIs. HID refs ARE official APIs.
 *
 * Producer/FSM does not wait for transport. Only this dedicated workqueue can
 * enter synchronous SDK sends (USB 30ms semaphore; BLE extension may wait for
 * queue space). Never claim the entire bottom half is nonblocking. A stock HOG
 * full-queue recursive retry is not even a strict 100ms bound and is not used.
 * Every DOWN/UP body has current ALL-owner buttons and ZERO XY/wheel. No
 * mouse_clear, motion_set or saved motion replay. Failed DOWN keeps a corrective
 * UP obligation since other inputs may have reported our temporarily exposed ref.
 *
 * Lifecycle protection invalidates even idle candidates and checks selected
 * endpoint/readiness before each SDK call, never replaying our own old candidate
 * or UP on a new epoch. The required BLE hook pins its connection (not active
 * profile-at-drain); no-evict acceptance is still NOT notification/host ACK.
 * A change can race the hook call, copied buttons can age during a wait, and USB
 * events may be coalesced by usb.c. backend_unfenced refers to those remaining SDK
 * epoch/ACK boundaries, NOT to an assertion that a provided pinned hook evicts.
 * Strict lifecycle fencing needs generations checked after waits/at drain and
 * endpoint pre-switch cleanup revoking synthetic ownership BEFORE other inputs
 * can report to a new host; a queue-empty or successful return is not a resync ACK.
 *
 * ZMK's global HID refs are not publicly locked. Our spinlock serializes bridge
 * callers, not unrelated HID users; caller's physical-left context must be
 * authoritative. For a strict concurrent-owner guarantee add a common short HID
 * button-ref/snapshot lock in hid.c + its users (not a lock held during send).
 * The stock single-core, serialized input integration is the practical target.
 */

#include "trackpad_tap_output.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/hid.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/hog.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

#if !IS_ENABLED(CONFIG_ZMK_POINTING)
#error "trackpad_tap_output requires CONFIG_ZMK_POINTING"
#endif

/* Not an SDK symbol: an optional, explicitly supplied usb_hid.c extension. */
extern uint32_t bbp9981_usb_hid_connection_epoch(void) __attribute__((weak));
extern int bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(
    struct zmk_hid_mouse_report_body *body, uint32_t expected_epoch) __attribute__((weak));
extern int bbp9981_ble_hid_send_mouse_buttons_snapshot(struct zmk_hid_mouse_report_body *body,
                                                     int expected_profile) __attribute__((weak));

#define TP_TAP_LEFT_INDEX ((zmk_mouse_button_t)0)
#define TP_TAP_LEFT_FLAG ((zmk_mouse_button_flags_t)1u)
#ifndef TP_TAP_OUTPUT_STACK_SIZE
#define TP_TAP_OUTPUT_STACK_SIZE 1536
#endif

K_THREAD_STACK_DEFINE(tp_tap_output_stack, TP_TAP_OUTPUT_STACK_SIZE);

static struct {
    struct k_spinlock lock;
    struct k_work_q queue;
    struct k_work_delayable work;
    struct tp_tap_output_context context;
    struct tp_tap_output_inflight status;
    struct zmk_endpoint_instance observed_endpoint;
    struct zmk_endpoint_instance request_endpoint;
    uint32_t request_lifecycle_epoch;
    uint32_t request_context_epoch;
    uint32_t request_usb_epoch;
    uint32_t observed_usb_epoch;
    uint32_t last_up_at;
    bool observed_ready;
    bool observed_valid;
    bool up_attempted;
} tap_output;

static void tp_tap_output_worker(struct k_work *work);

static bool endpoint_ready(struct zmk_endpoint_instance endpoint) {
    switch (endpoint.transport) {
    case ZMK_TRANSPORT_USB:
#if IS_ENABLED(CONFIG_ZMK_USB)
        /* usb.c considers SUSPEND "HID ready", but its send helper can return 0
         * for wakeup without sending a report. Reject it explicitly. */
        return zmk_usb_is_hid_ready() && zmk_usb_get_status() != USB_DC_SUSPEND;
#else
        return false;
#endif
    case ZMK_TRANSPORT_BLE:
#if IS_ENABLED(CONFIG_ZMK_BLE)
        return endpoint.ble.profile_index == zmk_ble_active_profile_index() &&
               zmk_ble_active_profile_is_connected();
#else
        return false;
#endif
    default:
        return false;
    }
}

static void invalidate_locked(void) {
    tap_output.status.lifecycle_epoch++;
    if (tap_output.status.phase != TP_TAP_OUTPUT_IDLE) {
        if (!tap_output.status.cancel_requested) {
            tap_output.status.cancelled++;
        }
        tap_output.status.cancel_requested = true;
    }
}

static void observe_locked(struct zmk_endpoint_instance endpoint, bool ready) {
    uint32_t usb_epoch = 0;
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (endpoint.transport == ZMK_TRANSPORT_USB && bbp9981_usb_hid_connection_epoch != NULL) {
        /* Required SDK getter is one nonblocking atomic read. This catches a
         * raw disconnect/reset/reconnect even if ZMK coalesced its notifier. */
        usb_epoch = bbp9981_usb_hid_connection_epoch();
    }
#endif
    if (tap_output.observed_valid &&
        (!zmk_endpoint_instance_eq(endpoint, tap_output.observed_endpoint) ||
         ready != tap_output.observed_ready ||
         (endpoint.transport == ZMK_TRANSPORT_USB && usb_epoch != tap_output.observed_usb_epoch))) {
        invalidate_locked();
    }
    tap_output.observed_endpoint = endpoint;
    tap_output.observed_usb_epoch = usb_epoch;
    tap_output.observed_ready = ready;
    tap_output.observed_valid = true;
}

static bool same_lifecycle_locked(void) {
    return tap_output.observed_ready &&
           tap_output.request_lifecycle_epoch == tap_output.status.lifecycle_epoch &&
           zmk_endpoint_instance_eq(tap_output.request_endpoint, tap_output.observed_endpoint);
}

static bool candidate_valid_locked(void) {
    return same_lifecycle_locked() && !tap_output.status.cancel_requested &&
           tap_output.request_context_epoch == tap_output.context.epoch &&
           tap_output.context.enabled && tap_output.context.physical_left_known &&
           !tap_output.context.physical_left_down;
}

static int schedule_locked(uint32_t delay_ms) {
    int ret = k_work_reschedule_for_queue(&tap_output.queue, &tap_output.work, K_MSEC(delay_ms));
    if (ret < 0) {
        tap_output.status.last_error = ret;
        tap_output.status.phase = (tap_output.status.owns_left || tap_output.status.up_pending)
                                      ? TP_TAP_OUTPUT_FAULT
                                      : TP_TAP_OUTPUT_IDLE;
    }
    return ret < 0 ? ret : 0;
}

static void finish_locked(void) {
    tap_output.status.phase = TP_TAP_OUTPUT_IDLE;
    tap_output.status.down_accepted = false;
    tap_output.status.up_pending = false;
    tap_output.status.cancel_requested = false;
    tap_output.status.up_attempts = 0;
    tap_output.up_attempted = false;
}

static int release_owned_locked(void) {
    if (!tap_output.status.owns_left) {
        return 0;
    }
    int ret = zmk_hid_mouse_button_release(TP_TAP_LEFT_INDEX);
    if (ret != 0) {
        /* v0.3's singular API changes no reference on error. Never forget or
         * re-press this token; explicit cancel may retry the release. */
        tap_output.status.last_error = ret;
        tap_output.status.phase = TP_TAP_OUTPUT_FAULT;
        return ret;
    }
    tap_output.status.owns_left = false;
    return 0;
}

static struct zmk_hid_mouse_report_body buttons_snapshot_locked(void) {
    return (struct zmk_hid_mouse_report_body){
        .buttons = zmk_hid_get_mouse_report()->body.buttons,
        .d_x = 0,
        .d_y = 0,
        .d_scroll_y = 0,
        .d_scroll_x = 0,
    };
}

static int send_buttons_snapshot(struct zmk_endpoint_instance expected,
                                 struct zmk_hid_mouse_report_body *body, bool down) {
    /* Refresh after reference acquisition and immediately before the SDK call;
     * cancellation while an SDK call is already waiting remains a public-API
     * limitation. Never hold our state lock over a transport wait. */
    struct zmk_endpoint_instance current = zmk_endpoints_selected();
    bool ready = endpoint_ready(current);
    k_spinlock_key_t key = k_spin_lock(&tap_output.lock);
    observe_locked(current, ready);
    int ret = !same_lifecycle_locked() || !zmk_endpoint_instance_eq(current, expected)
                  ? -ESTALE
                  : (down && !candidate_valid_locked() ? -ECANCELED : 0);
    if (ret == 0) {
        *body = buttons_snapshot_locked();
    }
    k_spin_unlock(&tap_output.lock, key);
    if (ret != 0) {
        return ret;
    }
    /* Public APIs cannot make check+enqueue atomic. No private HOG attributes,
     * private message queue, HID device, semaphore, or report replacement here. */
    switch (expected.transport) {
    case ZMK_TRANSPORT_USB:
#if IS_ENABLED(CONFIG_ZMK_USB)
        if (bbp9981_usb_hid_connection_epoch != NULL &&
            bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch != NULL) {
            return bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(body,
                                                                     tap_output.request_usb_epoch);
        }
#endif
        return -ENOTSUP;
    case ZMK_TRANSPORT_BLE:
#if IS_ENABLED(CONFIG_ZMK_BLE)
        if (bbp9981_ble_hid_send_mouse_buttons_snapshot != NULL) {
            return bbp9981_ble_hid_send_mouse_buttons_snapshot(body, expected.ble.profile_index);
        }
#endif
        return -ENOTSUP;
    default:
        return -ENOTSUP;
    }
}

int tp_tap_output_init(void) {
    k_spinlock_key_t key = k_spin_lock(&tap_output.lock);
    if (tap_output.status.initialized) {
        k_spin_unlock(&tap_output.lock, key);
        return -EALREADY;
    }
    k_spin_unlock(&tap_output.lock, key);

    /* Init is deliberately thread-context/exclusive; not a runtime reset. */
    k_work_init_delayable(&tap_output.work, tp_tap_output_worker);
    static const struct k_work_queue_config config = {.name = "BBP9981 tap output"};
    k_work_queue_start(&tap_output.queue, tp_tap_output_stack,
                       K_THREAD_STACK_SIZEOF(tp_tap_output_stack),
                       K_LOWEST_APPLICATION_THREAD_PRIO, &config);
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    bool ready = endpoint_ready(endpoint);
    key = k_spin_lock(&tap_output.lock);
    observe_locked(endpoint, ready);
    tap_output.status.initialized = true;
    k_spin_unlock(&tap_output.lock, key);
    return 0;
}

int tp_tap_output_submit(void) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    bool ready = endpoint_ready(endpoint);
    k_spinlock_key_t key = k_spin_lock(&tap_output.lock);
    int ret;
    if (!tap_output.status.initialized) {
        ret = -EACCES;
        goto out;
    }
    observe_locked(endpoint, ready);
    if (tap_output.status.phase != TP_TAP_OUTPUT_IDLE || tap_output.status.owns_left) {
        ret = -EBUSY;
    } else if (!tap_output.context.enabled) {
        ret = -EACCES;
    } else if (!tap_output.context.physical_left_known) {
        ret = -EAGAIN;
    } else if (tap_output.context.physical_left_down ||
               (zmk_hid_get_mouse_report()->body.buttons & TP_TAP_LEFT_FLAG) != 0) {
        ret = -EBUSY;
    } else if (!ready) {
        ret = -ENODEV;
    } else if ((endpoint.transport == ZMK_TRANSPORT_USB &&
                (bbp9981_usb_hid_connection_epoch == NULL ||
                 bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch == NULL)) ||
               (endpoint.transport == ZMK_TRANSPORT_BLE &&
                bbp9981_ble_hid_send_mouse_buttons_snapshot == NULL)) {
        ret = -ENOTSUP;
    } else {
        tap_output.request_endpoint = endpoint;
        tap_output.request_lifecycle_epoch = tap_output.status.lifecycle_epoch;
        tap_output.request_context_epoch = tap_output.context.epoch;
        tap_output.request_usb_epoch = tap_output.observed_usb_epoch;
        tap_output.status.phase = TP_TAP_OUTPUT_QUEUED;
        tap_output.status.cancel_requested = false;
        tap_output.status.last_error = 0;
        ret = schedule_locked(0);
    }
out:
    k_spin_unlock(&tap_output.lock, key);
    return ret;
}

int tp_tap_output_cancel(void) {
    k_spinlock_key_t key = k_spin_lock(&tap_output.lock);
    int ret = 0;
    if (!tap_output.status.initialized) {
        ret = -EACCES;
    } else if (tap_output.status.phase != TP_TAP_OUTPUT_IDLE || tap_output.status.owns_left) {
        if (!tap_output.status.cancel_requested) {
            tap_output.status.cancelled++;
        }
        tap_output.status.cancel_requested = true;
        if (tap_output.status.phase == TP_TAP_OUTPUT_FAULT) {
            tap_output.status.phase = TP_TAP_OUTPUT_RELEASING;
            tap_output.status.up_attempts = 0;
            tap_output.up_attempted = false;
        }
        ret = schedule_locked(0);
    }
    k_spin_unlock(&tap_output.lock, key);
    return ret;
}

int tp_tap_output_update_context(struct tp_tap_output_context context) {
    k_spinlock_key_t key = k_spin_lock(&tap_output.lock);
    int ret = 0;
    if (!tap_output.status.initialized) {
        ret = -EACCES;
    } else {
        bool cancel = context.epoch != tap_output.context.epoch || !context.enabled ||
                      !context.physical_left_known || context.physical_left_down;
        tap_output.context = context;
        if (cancel && tap_output.status.phase != TP_TAP_OUTPUT_IDLE) {
            if (!tap_output.status.cancel_requested) {
                tap_output.status.cancelled++;
            }
            tap_output.status.cancel_requested = true;
            /* A context change is NOT permission to reset/ref-release a fault. */
            if (tap_output.status.phase != TP_TAP_OUTPUT_FAULT) {
                ret = schedule_locked(0);
            }
        }
    }
    k_spin_unlock(&tap_output.lock, key);
    return ret;
}

struct tp_tap_output_inflight tp_tap_output_read_inflight(void) {
    k_spinlock_key_t key = k_spin_lock(&tap_output.lock);
    struct tp_tap_output_inflight status = tap_output.status;
    status.busy = status.phase != TP_TAP_OUTPUT_IDLE || status.owns_left;
    k_spin_unlock(&tap_output.lock, key);
    return status;
}

static void tp_tap_output_worker(struct k_work *work) {
    (void)work;
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    bool ready = endpoint_ready(endpoint);
    struct zmk_hid_mouse_report_body body;
    k_spinlock_key_t key = k_spin_lock(&tap_output.lock);
    observe_locked(endpoint, ready);
    if (tap_output.status.phase == TP_TAP_OUTPUT_IDLE ||
        tap_output.status.phase == TP_TAP_OUTPUT_FAULT) {
        k_spin_unlock(&tap_output.lock, key);
        return;
    }

    if (tap_output.status.phase == TP_TAP_OUTPUT_QUEUED) {
        if (!candidate_valid_locked() ||
            (zmk_hid_get_mouse_report()->body.buttons & TP_TAP_LEFT_FLAG) != 0) {
            tap_output.status.last_error = same_lifecycle_locked() ? -ECANCELED : -ESTALE;
            finish_locked();
            k_spin_unlock(&tap_output.lock, key);
            return;
        }
        int ret = zmk_hid_mouse_button_press(TP_TAP_LEFT_INDEX);
        if (ret != 0) {
            tap_output.status.last_error = ret;
            tap_output.status.last_down_error = ret;
            tap_output.status.down_failures++;
            finish_locked();
            k_spin_unlock(&tap_output.lock, key);
            return;
        }
        tap_output.status.owns_left = true;
        /* A global HID ref is visible to the unmodified input/moving listener
         * while our SDK call waits. Even a failed DOWN may therefore require a
         * current-buttons correction; never equate send failure with host UP. */
        tap_output.status.up_pending = true;
        tap_output.status.backend_unfenced = true; /* Global ref may reach another input's report. */
        tap_output.status.phase = TP_TAP_OUTPUT_SENDING_DOWN;
        body = buttons_snapshot_locked();
        endpoint = tap_output.request_endpoint;
        k_spin_unlock(&tap_output.lock, key);

        /* One backend event per callback. Even an immediate send success cannot
         * fold RELEASE into this same callback. accepted_at is measured AFTER
         * SDK acceptance, not producer submit time or the beginning of a wait. */
        ret = send_buttons_snapshot(endpoint, &body, true);
        key = k_spin_lock(&tap_output.lock);
        if (ret != 0) {
            tap_output.status.last_error = ret;
            tap_output.status.last_down_error = ret;
            tap_output.status.down_failures++;
            if (release_owned_locked() == 0) {
                if (same_lifecycle_locked()) {
                    tap_output.status.phase = TP_TAP_OUTPUT_RELEASING;
                    (void)schedule_locked(0); /* Separate callback, never fake DOWN success. */
                } else {
                    finish_locked();
                }
            }
        } else {
            tap_output.status.last_error = 0;
            tap_output.status.last_down_error = 0;
            tap_output.status.down_accepted = true;
            tap_output.status.backend_unfenced = true;
            tap_output.status.down_reports++;
            tap_output.status.accepted_at = k_uptime_get_32();
            tap_output.status.phase = TP_TAP_OUTPUT_HOLDING;
            (void)schedule_locked(TP_TAP_OUTPUT_HOLD_MS);
        }
        k_spin_unlock(&tap_output.lock, key);
        return;
    }

    if (tap_output.status.down_accepted) {
        uint32_t elapsed = (uint32_t)(k_uptime_get_32() - tap_output.status.accepted_at);
        if (elapsed < TP_TAP_OUTPUT_HOLD_MS) {
            (void)schedule_locked(TP_TAP_OUTPUT_HOLD_MS - elapsed);
            k_spin_unlock(&tap_output.lock, key);
            return;
        }
    }
    tap_output.status.phase = TP_TAP_OUTPUT_RELEASING;
    if (release_owned_locked() != 0) {
        k_spin_unlock(&tap_output.lock, key);
        return;
    }
    if (!tap_output.status.up_pending) {
        finish_locked();
        k_spin_unlock(&tap_output.lock, key);
        return;
    }
    if (!same_lifecycle_locked()) {
        /* Revoke our ONE reference locally. Do not route an old UP to the new
         * endpoint. SDK-hook queue/notify state is NOT a bridge resync ACK. */
        tap_output.status.last_error = -ESTALE;
        finish_locked();
        k_spin_unlock(&tap_output.lock, key);
        return;
    }
    uint32_t now = k_uptime_get_32();
    if (tap_output.up_attempted &&
        (uint32_t)(now - tap_output.last_up_at) < TP_TAP_OUTPUT_RETRY_MS) {
        (void)schedule_locked(TP_TAP_OUTPUT_RETRY_MS - (uint32_t)(now - tap_output.last_up_at));
        k_spin_unlock(&tap_output.lock, key);
        return;
    }
    tap_output.up_attempted = true;
    tap_output.last_up_at = now;
    tap_output.status.up_attempts++;
    body = buttons_snapshot_locked();
    endpoint = tap_output.request_endpoint;
    k_spin_unlock(&tap_output.lock, key);

    int ret = send_buttons_snapshot(endpoint, &body, false);
    key = k_spin_lock(&tap_output.lock);
    tap_output.status.last_error = ret == 0 && !tap_output.status.down_accepted
                                       ? tap_output.status.last_down_error
                                       : ret;
    if (ret == 0) {
        tap_output.status.backend_unfenced = true;
        tap_output.status.up_reports++;
        finish_locked();
    } else if (ret == -ESTALE) {
        finish_locked();
    } else if (tap_output.status.up_attempts >= TP_TAP_OUTPUT_MAX_UP_ATTEMPTS) {
        /* Keep the outstanding UP obligation visible and block new clicks. */
        tap_output.status.phase = TP_TAP_OUTPUT_FAULT;
    } else {
        (void)schedule_locked(TP_TAP_OUTPUT_RETRY_MS);
    }
    k_spin_unlock(&tap_output.lock, key);
}

static int tp_tap_output_lifecycle_listener(const zmk_event_t *event) {
    (void)event;
    /* Subscriptions themselves identify lifecycle events. Even same-endpoint
     * disconnect/reconnect while IDLE invalidates bridge candidates; no fake ACK. */
    k_spinlock_key_t key = k_spin_lock(&tap_output.lock);
    if (tap_output.status.initialized) {
        invalidate_locked();
        if (tap_output.status.phase != TP_TAP_OUTPUT_IDLE &&
            tap_output.status.phase != TP_TAP_OUTPUT_FAULT) {
            (void)schedule_locked(0);
        }
    }
    k_spin_unlock(&tap_output.lock, key);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(tp_tap_output_lifecycle, tp_tap_output_lifecycle_listener);
ZMK_SUBSCRIPTION(tp_tap_output_lifecycle, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(tp_tap_output_lifecycle, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(tp_tap_output_lifecycle, zmk_usb_conn_state_changed);
#endif
