/* Pure gate/accounting tests. The companion runtime test compiles the driver. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_raw_capture.h"

#define WINDOW 20000U
#define INTERVAL 20U
static struct tp_raw_gate_event gate(struct tp_raw_capture *s, uint32_t t,
                                    bool usb, int error, bool high) {
    return tp_raw_capture_poll_begin(s, t, usb, error, high, WINDOW);
}
static void test_transactions(void) {
    struct tp_raw_capture s = {0};
    assert(gate(&s, 1000, true, 0, false).event == TP_RAW_NONE);
    assert(!tp_raw_capture_idle_due(&s, 1000, 1, false, INTERVAL));
    assert(!tp_raw_capture_record(&s, 1000, 0) && s.attempt == 0 && s.seq == 0);
    assert(gate(&s, 1010, true, 0, true).event == TP_RAW_BEGIN);
    assert(s.open && s.session == 1 && s.began_at == 1010);
    assert(tp_raw_capture_idle_due(&s, 1010, 1, false, INTERVAL));
    assert(!tp_raw_capture_idle_due(&s, 1010, 0, false, INTERVAL));
    assert(!tp_raw_capture_idle_due(&s, 1010, -EIO, false, INTERVAL));
    assert(!tp_raw_capture_idle_due(&s, 1010, 1, true, INTERVAL));
    assert(tp_raw_capture_record(&s, 1010, 0) && s.attempt == 1 && s.seq == 1);
    assert(!tp_raw_capture_idle_due(&s, 1029, 1, false, INTERVAL));
    assert(tp_raw_capture_idle_due(&s, 1030, 1, false, INTERVAL));
    assert(tp_raw_capture_record(&s, 1030, -EIO) && s.attempt == 2 && s.seq == 1);
    assert(!tp_raw_capture_idle_due(&s, 1049, 1, false, INTERVAL));
    assert(tp_raw_capture_record(&s, 1040, 0) && s.attempt == 3 && s.seq == 2);
    assert(!tp_raw_capture_idle_due(&s, 1050, 1, false, INTERVAL));
    assert(tp_raw_capture_idle_due(&s, 1060, 1, false, INTERVAL));
    struct tp_raw_gate_event e = gate(&s, 1060, true, 0, false);
    assert(e.event == TP_RAW_END && e.reason == TP_RAW_DTR_LOW && !s.open);
    assert(!tp_raw_capture_record(&s, 1060, 0) && s.attempt == 3 && s.seq == 2);
    assert(gate(&s, 1070, true, 0, true).event == TP_RAW_BEGIN);
    assert(s.session == 2 && s.attempt == 0 && s.seq == 0 && !s.has_transaction);
    assert(gate(&s, 1080, true, 0, true).event == TP_RAW_NONE);
}
static void test_expiry_rearm(void) {
    struct tp_raw_capture s = {0};
    assert(gate(&s, 10, true, 0, true).event == TP_RAW_BEGIN);
    assert(gate(&s, 20009, true, 0, true).event == TP_RAW_NONE && s.open);
    struct tp_raw_gate_event e = gate(&s, 20010, true, 0, true);
    assert(e.event == TP_RAW_END && e.reason == TP_RAW_TIMEOUT && !s.open);
    for (unsigned n = 0; n < 20; n++) {
        assert(gate(&s, 20020 + n * WINDOW, true, 0, true).event == TP_RAW_NONE);
    }
    assert(!tp_raw_capture_idle_due(&s, 500000, 1, false, INTERVAL));
    /* Error returning a zero output value must NOT be interpreted as low. */
    assert(gate(&s, 500010, true, -EIO, false).event == TP_RAW_NONE);
    assert(gate(&s, 500020, true, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, 500030, true, 0, false).event == TP_RAW_NONE);
    assert(gate(&s, 500040, true, 0, true).event == TP_RAW_BEGIN && s.session == 2);
}
static void test_usb_and_error(void) {
    struct tp_raw_capture s = {0};
    assert(gate(&s, 100, false, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, 110, true, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, 120, true, 0, false).event == TP_RAW_NONE);
    assert(gate(&s, 130, true, 0, true).event == TP_RAW_BEGIN);
    struct tp_raw_gate_event e = gate(&s, 140, false, 0, true);
    assert(e.event == TP_RAW_END && e.reason == TP_RAW_USB_OFF);
    assert(gate(&s, 150, true, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, 160, false, 0, false).event == TP_RAW_NONE);
    assert(gate(&s, 170, false, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, 180, true, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, 190, true, 0, false).event == TP_RAW_NONE);
    assert(gate(&s, 200, true, 0, true).event == TP_RAW_BEGIN && s.session == 2);
    e = gate(&s, 210, true, -ENODEV, false);
    assert(e.event == TP_RAW_END && e.reason == TP_RAW_DTR_ERROR);
    assert(gate(&s, 220, true, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, 230, true, -EIO, false).event == TP_RAW_NONE);
    assert(gate(&s, 240, true, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, 250, true, 0, false).event == TP_RAW_NONE);
    assert(gate(&s, 260, true, 0, true).event == TP_RAW_BEGIN && s.session == 3);
}
static void test_rollover(void) {
    struct tp_raw_capture s = {0};
    uint32_t start = UINT32_MAX - 10U;
    assert(gate(&s, start, true, 0, true).event == TP_RAW_BEGIN);
    assert(tp_raw_capture_record(&s, start, 0));
    assert(!tp_raw_capture_idle_due(&s, 8, 1, false, INTERVAL));
    assert(tp_raw_capture_idle_due(&s, 9, 1, false, INTERVAL));
    assert(gate(&s, start + WINDOW - 1U, true, 0, true).event == TP_RAW_NONE);
    struct tp_raw_gate_event e = gate(&s, start + WINDOW, true, 0, true);
    assert(e.event == TP_RAW_END && e.reason == TP_RAW_TIMEOUT);
    assert(gate(&s, start + WINDOW + 10U, true, 0, true).event == TP_RAW_NONE);
    assert(gate(&s, start + WINDOW + 20U, true, 0, false).event == TP_RAW_NONE);
    assert(gate(&s, start + WINDOW + 30U, true, 0, true).event == TP_RAW_BEGIN);
}
int main(void) {
    test_transactions(); test_expiry_rearm(); test_usb_and_error(); test_rollover();
    assert(strcmp(tp_raw_reason_name(TP_RAW_TIMEOUT), "timeout") == 0);
    assert(strcmp(tp_raw_reason_name(TP_RAW_DTR_LOW), "dtr_low") == 0);
    assert(strcmp(tp_raw_reason_name(TP_RAW_USB_OFF), "usb_off") == 0);
    assert(strcmp(tp_raw_reason_name(TP_RAW_DTR_ERROR), "dtr_error") == 0);
    assert(strcmp(tp_raw_source_name(TP_RAW_MOTION), "motion") == 0);
    assert(strcmp(tp_raw_source_name(TP_RAW_IDLE), "idle") == 0);
    puts("PASS Raw capture gate: actual attempts/success counts, immediate/20ms idle, no GPIO-error reads, 20s expiry, true DTR-low rearm, USB/error edge consumption, uint32 uptime rollover");
    return 0;
}
