/* Bounded, read-only 0A/7-byte capture. No contact or input-event inference. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TP_RAW_FRAME_SIZE 7U

enum tp_raw_event {
    TP_RAW_NONE,
    TP_RAW_BEGIN,
    TP_RAW_END,
};
enum tp_raw_end_reason {
    TP_RAW_TIMEOUT,
    TP_RAW_DTR_LOW,
    TP_RAW_USB_OFF,
    TP_RAW_DTR_ERROR,
};
enum tp_raw_source {
    TP_RAW_MOTION,
    TP_RAW_IDLE,
};

struct tp_raw_gate_event {
    enum tp_raw_event event;
    enum tp_raw_end_reason reason;
};

struct tp_raw_capture {
    bool open;
    /* A failed DTR read is NOT a low edge. USB loss/timeout never clears this. */
    bool dtr_high;
    uint32_t session;
    uint32_t attempt;
    uint32_t seq;
    uint32_t began_at;
    uint32_t transaction_at;
    bool has_transaction;
};

static inline const char *tp_raw_reason_name(enum tp_raw_end_reason reason) {
    switch (reason) {
    case TP_RAW_TIMEOUT: return "timeout";
    case TP_RAW_DTR_LOW: return "dtr_low";
    case TP_RAW_USB_OFF: return "usb_off";
    case TP_RAW_DTR_ERROR: return "dtr_error";
    }
    return "dtr_error";
}

static inline const char *tp_raw_source_name(enum tp_raw_source source) {
    return source == TP_RAW_MOTION ? "motion" : "idle";
}

/* Call at poll BEGIN, before the normal motion transaction. The first valid
 * high can open the first session; every later reopening needs an observed
 * successful DTR-low followed by high. High observed while USB is unavailable
 * consumes its edge too; USB reconnect is not permission to rearm.
 */
static inline struct tp_raw_gate_event tp_raw_capture_poll_begin(
    struct tp_raw_capture *state, uint32_t now, bool usb_ready,
    int dtr_error, bool dtr_high, uint32_t window_ms) {
    struct tp_raw_gate_event result = {TP_RAW_NONE, TP_RAW_TIMEOUT};
    bool rising = dtr_error == 0 && dtr_high && !state->dtr_high;
    state->dtr_high = dtr_error != 0 || dtr_high;

    if (state->open) {
        if (!usb_ready) {
            result.reason = TP_RAW_USB_OFF;
        } else if (dtr_error != 0) {
            result.reason = TP_RAW_DTR_ERROR;
        } else if (!dtr_high) {
            result.reason = TP_RAW_DTR_LOW;
        } else if ((uint32_t)(now - state->began_at) >= window_ms) {
            result.reason = TP_RAW_TIMEOUT;
        } else {
            return result;
        }
        state->open = false;
        result.event = TP_RAW_END;
        return result;
    }

    if (usb_ready && rising) {
        state->open = true;
        state->session++;
        state->attempt = state->seq = 0;
        state->began_at = now;
        state->transaction_at = now;
        state->has_transaction = false;
        result.event = TP_RAW_BEGIN;
    }
    return result;
}

/* GPIO errors are missing observations, NOT UP and NOT permission to read.
 * Normal reads also reset this cadence, including failed transactions.
 */
static inline bool tp_raw_capture_idle_due(const struct tp_raw_capture *state,
    uint32_t now, int irq, bool motion_read_this_poll, uint32_t interval_ms) {
    return state->open && irq == 1 && !motion_read_this_poll &&
           (!state->has_transaction ||
            (uint32_t)(now - state->transaction_at) >= interval_ms);
}

/* Exactly one call per actual pointer0A/STOP + burst(0A,7) attempt, including
 * pointer-write failure. seq counts only successful reads, not fresh sensor
 * updates. There is deliberately no cached frame in this state.
 */
static inline bool tp_raw_capture_record(struct tp_raw_capture *state,
                                        uint32_t now, int error) {
    if (!state->open) { return false; }
    state->attempt++;
    if (error == 0) { state->seq++; }
    state->transaction_at = now;
    state->has_transaction = true;
    return true;
}
