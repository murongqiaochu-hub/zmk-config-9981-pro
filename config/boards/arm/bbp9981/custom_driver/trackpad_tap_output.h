/* SPDX-License-Identifier: MIT */
#ifndef BBP9981_TRACKPAD_TAP_OUTPUT_H
#define BBP9981_TRACKPAD_TAP_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#define TP_TAP_OUTPUT_HOLD_MS UINT32_C(12)
#define TP_TAP_OUTPUT_RETRY_MS UINT32_C(10)
#define TP_TAP_OUTPUT_MAX_UP_ATTEMPTS 40u

enum tp_tap_output_phase {
    TP_TAP_OUTPUT_IDLE = 0,
    TP_TAP_OUTPUT_QUEUED,
    TP_TAP_OUTPUT_SENDING_DOWN,
    TP_TAP_OUTPUT_HOLDING,
    TP_TAP_OUTPUT_RELEASING,
    TP_TAP_OUTPUT_FAULT,
};

/* The caller has validated contact/tap-ready. Unknown physical-left is NOT UP.
 * epoch must change on a producer/FSM reset, mode change, or candidate invalidation.
 * Do not infer physical-left from HID bit 0 while the bridge owns that same bit.
 * Other physical/foreign left owners must be included in physical_left_down. */
struct tp_tap_output_context {
    uint32_t epoch;
    bool enabled;
    bool physical_left_known;
    bool physical_left_down;
};

struct tp_tap_output_inflight {
    enum tp_tap_output_phase phase;
    uint32_t lifecycle_epoch;
    uint32_t accepted_at;
    uint32_t down_reports;
    uint32_t up_reports;
    uint32_t down_failures;
    uint32_t cancelled;
    unsigned int up_attempts;
    int last_error;
    int last_down_error;
    bool initialized;
    bool busy;
    bool owns_left;
    bool down_accepted;
    bool up_pending;
    bool cancel_requested;
    bool backend_unfenced;
};

/* Singleton, CENTRAL-side only. Init once on virgin storage, in thread context,
 * before publishing it to producers; repeated init returns -EALREADY, never resets
 * an owned reference. Context starts disabled/unknown. No sensor is enabled here. */
int tp_tap_output_init(void);

/* Nonblocking producer operations (short state critical sections + scheduling).
 * submit: 0 accepts ONE candidate into this bridge, NOT a host ACK or completed
 * click. -EBUSY discards a new candidate; there is no coalescing/drop-oldest here.
 * Missing transport hooks return -ENOTSUP before acquiring a reference.
 * Actual SDK/send failures are observable through last_error/last_down_error.
 * DOWN failure can still leave up_pending: the exposed HID ref could have been
 * reported by another input while the SDK waited. A corrective current-buttons
 * report is separately retried; down_reports never counts a failed DOWN.
 * cancel never memsets state and never forgets a failed reference release.
 * Explicit cancel retries a latched release-ref failure once. */
int tp_tap_output_submit(void);
int tp_tap_output_cancel(void);
int tp_tap_output_update_context(struct tp_tap_output_context context);
struct tp_tap_output_inflight tp_tap_output_read_inflight(void);

/* OPTIONAL, NOT an official ZMK v0.3.0 symbol. A missing weak symbol makes USB
 * fail closed with -ENOTSUP; it is NOT replaced by a mutable-global-report send.
 * Minimal SDK extension in usb_hid.c: pack ZMK_HID_REPORT_ID_MOUSE plus a COPY of
 * this body and reuse the existing HID semaphore/send helper. Reject suspend
 * (usb_wakeup_request()==0 is not report acceptance). Never mutate global motion.
 * USB REQUIRES the generation getter + _epoch send extension below, not the
 * legacy one-argument symbol. Getter must be nonblocking (SDK atomic generation
 * increments on raw RESET/DISCONNECTED callbacks, not coalesced ZMK events).
 * Send compares the captured expected generation/status AFTER semaphore wait,
 * then snapshots CURRENT owners' buttons with zero motion and writes via the
 * original SDK HID path. Consume/copy before return; 0 still is not host ACK.
 *
 * BLE requires its own optional SDK extension (also NOT a v0.3 symbol), capturing
 * the expected profile's connection BEFORE queueing, no-evict button enqueue,
 * and one ordered queue shared with ordinary mouse reports. Missing BLE extension
 * also fails closed; stock HOG's drop-oldest/active-profile-at-drain is NOT a safe
 * fallback. A pinned connection fixes profile rerouting, but does not by itself
 * attest notification success/host ACK, queue flush, or atomic lifecycle fencing.
 */
struct zmk_hid_mouse_report_body;
/* Legacy extension declaration for integration compatibility; NEVER a fallback. */
int bbp9981_usb_hid_send_mouse_buttons_snapshot(struct zmk_hid_mouse_report_body *body);
uint32_t bbp9981_usb_hid_connection_epoch(void);
int bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(struct zmk_hid_mouse_report_body *body,
                                                     uint32_t expected_epoch);
int bbp9981_ble_hid_send_mouse_buttons_snapshot(struct zmk_hid_mouse_report_body *body,
                                               int expected_profile);

#endif /* BBP9981_TRACKPAD_TAP_OUTPUT_H */
