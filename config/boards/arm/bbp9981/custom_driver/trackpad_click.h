/*
 * Pure, SDK-independent single-click output scheduler. SPDX-License-Identifier: MIT
 *
 * No I/O, sleeps, threads, allocation, motion data, or global HID clear here.
 * All public calls must run serialized and callbacks must be NONBLOCKING.
 * Existing ZMK USB/BLE send APIs are NOT asserted to satisfy this contract.
 *
 * Callback contract:
 * - ready returns 0 and a stable transport/connection epoch when usable; >0
 *   means not ready, <0 means an error. Every reconnect/endpoint switch changes
 *   the epoch, even if the endpoint number is unchanged.
 * - left_busy reports physical-left OR foreign-left ownership, excluding our
 *   one reference if ours=true. Unknown state is an error, not an unpressed key.
 * - press_ref/release_ref operate on exactly ONE left-button reference. Zero
 *   means success; nonzero MUST mean no reference was changed. If the real SDK
 *   cannot guarantee that, its adapter must fail closed and resolve ownership
 *   externally; it must never clear somebody else's reference.
 * - send_buttons snapshots ALL CURRENT owners' buttons, uses zero X/Y/scroll,
 *   atomically checks expected_epoch, and returns 0 ONLY if an ordered report
 *   FIFO accepted a COPY. Nonzero means nothing was enqueued. It must not reuse
 *   stale movement, overwrite buttons with zero, evict old reports, or block.
 *   FIFO acceptance is not a host ACK (especially over BLE).
 *
 * A successful DOWN stays in flight for >=12 ms, then our reference is released
 * exactly once. A failed UP enqueue retries current buttons at 10-ms intervals,
 * never the original snapshot. Forty failed attempts require explicit resync.
 * No synthetic reference means neither no physical finger nor button UP.
 *
 * An epoch change/error revokes our reference and latches needs_resync. Never
 * replay old DOWN/UP to a new epoch. The adapter must flush/quarantine all old
 * reports AND queue a fresh current-button-only snapshot before acknowledging
 * resync. This is a required external transport contract, not an SDK guarantee.
 * reset is cancellation, NOT memset: it preserves an outstanding UP obligation.
 * init is ONLY for virgin/quiescent storage with no outstanding reference/report.
 * Millisecond clocks may wrap; call gaps must remain shorter than 2^31 ms.
 */
#ifndef BBP9981_TRACKPAD_CLICK_H
#define BBP9981_TRACKPAD_CLICK_H

#include <stdbool.h>
#include <stdint.h>

#define TP_CLICK_HOLD_MS UINT32_C(12)
#define TP_CLICK_RETRY_MS UINT32_C(10)
#define TP_CLICK_MAX_UP_ATTEMPTS 40u

enum tp_click_result {
    TP_CLICK_OK = 0,
    TP_CLICK_BUSY,
    TP_CLICK_NOT_READY,
    TP_CLICK_DISABLED,
    TP_CLICK_RETRY,
    TP_CLICK_NEEDS_RESYNC,
};

enum tp_click_fault {
    TP_CLICK_FAULT_NONE = 0,
    TP_CLICK_FAULT_OPS,
    TP_CLICK_FAULT_READY,
    TP_CLICK_FAULT_EPOCH,
    TP_CLICK_FAULT_LEFT_STATE,
    TP_CLICK_FAULT_PRESS_REF,
    TP_CLICK_FAULT_RELEASE_REF,
    TP_CLICK_FAULT_SEND_DOWN,
    TP_CLICK_FAULT_UP_EXHAUSTED,
};

struct tp_click_ops {
    void *ctx;
    int (*ready)(void *ctx, uint32_t *epoch);
    int (*left_busy)(void *ctx, bool ours, bool *busy);
    int (*press_ref)(void *ctx);
    int (*release_ref)(void *ctx);
    int (*send_buttons)(void *ctx, uint32_t expected_epoch);
};

struct tp_click {
    uint32_t epoch;
    uint32_t held_at;
    uint32_t last_up_attempt;
    unsigned int up_attempts;
    enum tp_click_fault fault;
    bool epoch_valid;
    bool owns_left;
    bool held;
    bool down_accepted;
    bool up_pending;
    bool up_attempted;
    bool needs_resync;
};

/* These booleans are attestations by an adapter that actually performed the
 * flush/current-state enqueue, NOT permission to silently discard old reports. */
struct tp_click_resync_ack {
    uint32_t epoch;
    bool old_reports_flushed;
    bool current_buttons_queued;
};

static inline void tp_click_init(struct tp_click *s) {
    *s = (struct tp_click){0};
}

static inline bool tp_click_ops_valid(const struct tp_click_ops *ops) {
    return ops && ops->ready && ops->left_busy && ops->press_ref &&
           ops->release_ref && ops->send_buttons;
}

static inline void tp_click_fault_set(struct tp_click *s, enum tp_click_fault fault) {
    s->fault = fault;
    s->needs_resync = true;
}

static inline bool tp_click_inflight(const struct tp_click *s) {
    return s->owns_left || s->held || s->down_accepted || s->up_pending;
}

/* Only successful release consumes our token. A failure leaves ownership
 * explicit and blocked; ordinary polls never busy-loop release_ref failures. */
static inline enum tp_click_result tp_click_drop_owned(struct tp_click *s,
                                                       const struct tp_click_ops *ops) {
    s->held = false;
    if (s->down_accepted && !s->up_pending) {
        s->up_pending = true;
        s->up_attempted = false;
        s->up_attempts = 0;
    }
    if (!s->owns_left) {
        return s->needs_resync ? TP_CLICK_NEEDS_RESYNC : TP_CLICK_OK;
    }
    if (!ops || !ops->release_ref) {
        tp_click_fault_set(s, TP_CLICK_FAULT_OPS);
        return TP_CLICK_NEEDS_RESYNC;
    }
    if (ops->release_ref(ops->ctx) != 0) {
        tp_click_fault_set(s, TP_CLICK_FAULT_RELEASE_REF);
        return TP_CLICK_NEEDS_RESYNC;
    }
    s->owns_left = false;
    return s->needs_resync ? TP_CLICK_NEEDS_RESYNC : TP_CLICK_OK;
}

/* Cancellation preserves epoch, retry budget, error latch and UP obligation.
 * Explicit resets may retry a failed reference release under the no-change-on-
 * failure callback contract; a successful token is NEVER released again. */
static inline enum tp_click_result tp_click_reset(struct tp_click *s,
                                                  const struct tp_click_ops *ops) {
    return tp_click_drop_owned(s, ops);
}

static inline enum tp_click_result tp_click_observe_epoch(struct tp_click *s,
                                                          const struct tp_click_ops *ops) {
    uint32_t epoch = 0;
    int ret = ops->ready(ops->ctx, &epoch);
    if (ret != 0) {
        if (tp_click_inflight(s) || ret < 0) {
            tp_click_fault_set(s, TP_CLICK_FAULT_READY);
            (void)tp_click_drop_owned(s, ops);
            return TP_CLICK_NEEDS_RESYNC;
        }
        return TP_CLICK_NOT_READY;
    }
    if (s->epoch_valid && epoch != s->epoch) {
        tp_click_fault_set(s, TP_CLICK_FAULT_EPOCH);
        (void)tp_click_drop_owned(s, ops);
        return TP_CLICK_NEEDS_RESYNC;
    }
    s->epoch = epoch;
    s->epoch_valid = true;
    return TP_CLICK_OK;
}

static inline enum tp_click_result tp_click_try_up(struct tp_click *s,
                                                   const struct tp_click_ops *ops,
                                                   uint32_t now) {
    if (!s->up_pending) {
        return TP_CLICK_OK;
    }
    if (s->owns_left || s->needs_resync) {
        return TP_CLICK_NEEDS_RESYNC;
    }
    if (s->up_attempted && (uint32_t)(now - s->last_up_attempt) < TP_CLICK_RETRY_MS) {
        return TP_CLICK_RETRY;
    }
    s->up_attempted = true;
    s->last_up_attempt = now;
    s->up_attempts++;
    if (ops->send_buttons(ops->ctx, s->epoch) == 0) {
        s->up_pending = false;
        s->down_accepted = false;
        s->up_attempted = false;
        s->up_attempts = 0;
        return TP_CLICK_OK;
    }
    if (s->up_attempts >= TP_CLICK_MAX_UP_ATTEMPTS) {
        tp_click_fault_set(s, TP_CLICK_FAULT_UP_EXHAUSTED);
        return TP_CLICK_NEEDS_RESYNC;
    }
    return TP_CLICK_RETRY;
}

/* A request is never buffered or coalesced: BUSY discards this candidate.
 * Caller supplies an already-validated tap, not raw contact/motion/zero packets. */
static inline enum tp_click_result tp_click_request(struct tp_click *s,
                                                    const struct tp_click_ops *ops,
                                                    uint32_t now, bool enabled) {
    bool busy = false;
    enum tp_click_result ret;
    if (!tp_click_ops_valid(ops)) {
        tp_click_fault_set(s, TP_CLICK_FAULT_OPS);
        (void)tp_click_drop_owned(s, ops);
        return TP_CLICK_NEEDS_RESYNC;
    }
    if (!enabled) {
        (void)tp_click_reset(s, ops);
        return s->needs_resync ? TP_CLICK_NEEDS_RESYNC : TP_CLICK_DISABLED;
    }
    if (s->needs_resync) {
        return TP_CLICK_NEEDS_RESYNC;
    }
    if (tp_click_inflight(s)) {
        return TP_CLICK_BUSY;
    }
    ret = tp_click_observe_epoch(s, ops);
    if (ret != TP_CLICK_OK) {
        return ret;
    }
    if (ops->left_busy(ops->ctx, false, &busy) != 0) {
        tp_click_fault_set(s, TP_CLICK_FAULT_LEFT_STATE);
        return TP_CLICK_NEEDS_RESYNC;
    }
    if (busy) {
        return TP_CLICK_BUSY;
    }
    if (ops->press_ref(ops->ctx) != 0) {
        tp_click_fault_set(s, TP_CLICK_FAULT_PRESS_REF);
        return TP_CLICK_NEEDS_RESYNC;
    }
    s->owns_left = true;
    if (ops->send_buttons(ops->ctx, s->epoch) != 0) {
        tp_click_fault_set(s, TP_CLICK_FAULT_SEND_DOWN);
        (void)tp_click_drop_owned(s, ops);
        return TP_CLICK_NEEDS_RESYNC;
    }
    s->down_accepted = true;
    s->held = true;
    s->held_at = now;
    return TP_CLICK_OK;
}

/* One bounded poll: at most one snapshot enqueue; no loops/waits.
 * Disable/foreign-left takeover may cancel earlier than the normal hold time. */
static inline enum tp_click_result tp_click_poll(struct tp_click *s,
                                                 const struct tp_click_ops *ops,
                                                 uint32_t now, bool enabled) {
    bool busy = false;
    enum tp_click_result ret;
    if (!tp_click_ops_valid(ops)) {
        tp_click_fault_set(s, TP_CLICK_FAULT_OPS);
        (void)tp_click_drop_owned(s, ops);
        return TP_CLICK_NEEDS_RESYNC;
    }
    if (s->needs_resync) {
        return TP_CLICK_NEEDS_RESYNC;
    }
    if (!enabled) {
        (void)tp_click_reset(s, ops);
        if (s->needs_resync) {
            return TP_CLICK_NEEDS_RESYNC;
        }
        if (!tp_click_inflight(s)) {
            return TP_CLICK_DISABLED;
        }
    }
    ret = tp_click_observe_epoch(s, ops);
    if (ret != TP_CLICK_OK) {
        return ret;
    }
    if (s->held) {
        if (ops->left_busy(ops->ctx, true, &busy) != 0) {
            tp_click_fault_set(s, TP_CLICK_FAULT_LEFT_STATE);
            (void)tp_click_drop_owned(s, ops);
            return TP_CLICK_NEEDS_RESYNC;
        }
        if (busy || (uint32_t)(now - s->held_at) >= TP_CLICK_HOLD_MS) {
            (void)tp_click_drop_owned(s, ops);
            if (s->needs_resync) {
                return TP_CLICK_NEEDS_RESYNC;
            }
        } else {
            return TP_CLICK_BUSY;
        }
    }
    ret = tp_click_try_up(s, ops, now);
    if (ret != TP_CLICK_OK) {
        return ret;
    }
    return enabled ? TP_CLICK_OK : TP_CLICK_DISABLED;
}

/* Never call this merely because an endpoint is ready again. Even FIFO
 * accepted DOWN+UP may remain queued when an epoch changes, so the external
 * old-queue flush and current-button resync are mandatory, including idle. */
static inline enum tp_click_result tp_click_ack_resync(struct tp_click *s,
                                                       const struct tp_click_ops *ops,
                                                       struct tp_click_resync_ack ack) {
    uint32_t epoch = 0;
    if (s->owns_left || !ack.old_reports_flushed || !ack.current_buttons_queued) {
        return TP_CLICK_NEEDS_RESYNC;
    }
    if (!tp_click_ops_valid(ops) || ops->ready(ops->ctx, &epoch) != 0 || epoch != ack.epoch) {
        tp_click_fault_set(s, TP_CLICK_FAULT_READY);
        return TP_CLICK_NEEDS_RESYNC;
    }
    s->epoch = epoch;
    s->epoch_valid = true;
    s->held = false;
    s->down_accepted = false;
    s->up_pending = false;
    s->up_attempted = false;
    s->up_attempts = 0;
    s->needs_resync = false;
    s->fault = TP_CLICK_FAULT_NONE;
    return TP_CLICK_OK;
}

#endif /* BBP9981_TRACKPAD_CLICK_H */
