/* SPDX-License-Identifier: MIT
 * Snapshot-only stability regressions against the UNMODIFIED pinned USB TUs.
 * Reuse the deferred-pointer SDK fixture, not a rewritten production sender.
 * No assertion depends on new snapshot-private storage/state: the same binary
 * sources can include the old prefix to demonstrate behavioral failures.
 *
 * cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter
 *    -Wno-unused-function -Wno-unused-variable -fsanitize=undefined
 *    -fno-sanitize-recover=all -Itests/sdk_snapshot_mocks
 *    tests/test_stability_usb_snapshot.c -o /tmp/test_stability_usb_snapshot
 * Cases: normal, legacy, lifetime, protocol-wait, busy-ready, submitting,
 *        submit-retry, reset-abort, endpoints, isr, errors, status, stock.
 * Compile BOOT=0/1. Host contracts only, never a device/host-ACK claim.
 */
#define BBP_USB_SNAPSHOT_NO_MAIN 1
#include "test_sdk_usb.c"

static void set_mouse(uint8_t buttons) {
    global_mouse = (struct zmk_hid_mouse_report){.report_id = ZMK_HID_REPORT_ID_MOUSE,
        .body = {.buttons = buttons, .d_x = 91, .d_y = -47, .d_scroll_x = 4, .d_scroll_y = 5}};
}
static int send_current(void) {
    return bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(
        &global_mouse.body, bbp9981_usb_hid_connection_epoch());
}
static void expect_pending(uint8_t buttons) {
    assert(dma.pending && dma.len == sizeof(struct zmk_hid_mouse_report));
    assert(nrf_busy[dma.ep] && ((uintptr_t)dma.source % sizeof(uint32_t)) == 0U);
    assert(dma.source != (const uint8_t *)&global_mouse);
    struct zmk_hid_mouse_report wire;
    memcpy(&wire, dma.source, sizeof(wire));
    assert(wire.report_id == ZMK_HID_REPORT_ID_MOUSE && wire.body.buttons == buttons &&
           !wire.body.d_x && !wire.body.d_y && !wire.body.d_scroll_x && !wire.body.d_scroll_y);
}
static void finish_snapshot(uint8_t buttons) {
    expect_pending(buttons);
    complete_dma();
    assert_snapshot_wire(buttons);
    assert(hid_sem.value == 1);
}
static void test_deferred_source(void) {
    set_mouse(0x15);
    unsigned int before_gives = gives;
    assert(send_caller_stack(bbp9981_usb_hid_connection_epoch()) == 0);
    assert(dma.pending && !hid_sem.value && gives == before_gives);
    memset(&global_mouse, 0x7B, sizeof(global_mouse));
    clobber_stack();
    finish_snapshot(0x15);
    /* RAM retirement on the NEXT successful take must not mint a second permit. */
    set_mouse(0x0A); before_gives = gives;
    assert(send_current() == 0 && !hid_sem.value && gives == before_gives);
    finish_snapshot(0x0A);
}
static void test_busy_ready(void) {
    set_mouse(1);
    assert(send_current() == 0 && !hid_sem.value);
    const uint8_t *saved = dma.source;
    unsigned int before_writes = writes, before_gives = gives, before_queries = busy_queries;
    assert(send_current() == -EAGAIN && writes == before_writes && gives == before_gives);
    assert(busy_queries == before_queries); /* Failed take never gives/reclaims. */
    /* Preserve stock's ignored timeout: its rejected write gives a false permit.
     * It must not grant access to the snapshot's already-owned source RAM. */
    assert(zmk_usb_hid_send_keyboard_report() == -EAGAIN && hid_sem.value == 1);
    assert(writes == before_writes + 1U && gives == before_gives + 1U);
    expect_pending(1);
    registered_ops->int_in_ready(&device); /* Unchanged stock callback: old/stray ready. */
    set_mouse(0x12);
    before_writes = writes; before_gives = gives;
    unsigned int before_takes = takes;
    assert(send_current() == -EBUSY && writes == before_writes && dma.source == saved);
    assert(takes == before_takes + 1U && gives == before_gives + 1U && hid_sem.value == 1);
    assert(last_busy_ep == NRFX_USBD_EPIN5);
    expect_pending(1);
    /* Repeated old ready notifications cannot authorize a second snapshot. */
    registered_ops->int_in_ready(&device); before_gives = gives;
    assert(send_current() == -EBUSY && writes == before_writes && gives == before_gives + 1U);
    expect_pending(1); finish_snapshot(1);
    before_gives = gives;
    assert(send_current() == 0 && !hid_sem.value && gives == before_gives);
    finish_snapshot(0x12);
}
static void reenter_before_accept(void) {
    assert(!dma.pending && !nrf_busy[actual_eps[0].ep_addr]);
    registered_ops->int_in_ready(&device); /* busy=false, but SDK has NOT returned. */
    unsigned int before_writes = writes, before_queries = busy_queries, before_gives = gives;
    set_mouse(0x1E);
    assert(send_current() == -EBUSY);
    assert(writes == before_writes && busy_queries == before_queries);
    assert(gives == before_gives + 1U && hid_sem.value == 1);
}
static uint8_t early_buttons;
static bool retry_after_early_completion;
static void complete_and_reenter(void) {
    assert(dma.pending);
    const uint8_t *saved_source = dma.source;
    uint32_t saved_len = dma.len;
    uint8_t saved_ep = dma.ep;
    complete_dma(); assert_snapshot_wire(early_buttons);
    assert(!nrf_busy[saved_ep] && hid_sem.value == 1);
    unsigned int before_writes = writes, before_queries = busy_queries, before_gives = gives;
    set_mouse(0x1C);
    assert(send_current() == -EBUSY); /* SUBMITTING cannot be reclaimed even now. */
    assert(writes == before_writes && busy_queries == before_queries && gives == before_gives + 1U);
    if (retry_after_early_completion) {
        /* Model the SAME SDK invocation retaining/retrying its original pointer
         * after an early/old completion and temporary busy=false. No new copy. */
        dma.source = saved_source; dma.len = saved_len; dma.ep = saved_ep;
        dma.pending = true; nrf_busy[saved_ep] = true;
    }
}
static void test_submitting(void) {
    set_mouse(0x05);
    unsigned int before_writes = writes;
    before_driver_accept = reenter_before_accept;
    assert(send_current() == 0 && writes == before_writes + 1U);
    expect_pending(0x05); finish_snapshot(0x05);
    early_buttons = 0x09; retry_after_early_completion = false;
    set_mouse(early_buttons);
    unsigned int before_gives = gives;
    during_driver = complete_and_reenter;
    assert(send_current() == 0 && !dma.pending && hid_sem.value == 1);
    /* Exactly the real ready + reentrant caller's returned permit. The
     * post-write RAM retirement must NOT give the shared semaphore again. */
    assert(gives == before_gives + 2U); assert_snapshot_wire(early_buttons);
    before_gives = gives;
    assert(send_current() == 0 && !hid_sem.value && gives == before_gives);
    finish_snapshot(0x1C);
}
static void test_submit_retry(void) {
    early_buttons = 0x11; retry_after_early_completion = true;
    set_mouse(early_buttons);
    unsigned int before_writes = writes, before_gives = gives;
    during_driver = complete_and_reenter;
    assert(send_current() == 0 && dma.pending && writes == before_writes + 1U);
    assert(gives == before_gives + 2U && hid_sem.value == 1);
    expect_pending(early_buttons);
    before_writes = writes; before_gives = gives;
    assert(send_current() == -EBUSY && writes == before_writes && gives == before_gives + 1U);
    finish_snapshot(early_buttons); retry_after_early_completion = false;
}
static void test_reset_abort(void) {
    set_mouse(1);
    uint32_t old_epoch = bbp9981_usb_hid_connection_epoch();
    assert(send_current() == 0 && !hid_sem.value);
    abort_dma_no_ready(); reconnect(); set_mouse(2);
    unsigned int before_writes = writes, before_takes = takes, before_gives = gives;
    unsigned int before_queries = busy_queries;
    assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&global_mouse.body, old_epoch) == -ESTALE);
    assert(takes == before_takes && gives == before_gives && busy_queries == before_queries);
    /* Known HID_0 limitation: abort without ready loses the stock permit. RAM
     * safety alone must NOT repair it by giving a semaphore somebody else owns. */
    assert(send_current() == -EAGAIN && writes == before_writes && !hid_sem.value);
    assert(gives == before_gives && busy_queries == before_queries);
    registered_ops->int_in_ready(&device); /* Explicit delayed stock ready in this model. */
    before_takes = takes;
    assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&global_mouse.body, old_epoch) == -ESTALE);
    assert(takes == before_takes);
    assert(send_current() == 0 && !hid_sem.value); expect_pending(2);
    registered_ops->int_in_ready(&device); /* Old ready while the new EP is busy. */
    before_writes = writes; before_gives = gives;
    assert(send_current() == -EBUSY && writes == before_writes && gives == before_gives + 1U);
    expect_pending(2);
    old_epoch = bbp9981_usb_hid_connection_epoch(); reconnect();
    assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&global_mouse.body, old_epoch) == -ESTALE);
    assert(send_current() == -EBUSY && writes == before_writes); expect_pending(2);
    abort_dma_no_ready(); set_mouse(8);
    assert(send_current() == 0 && writes == before_writes + 1U && !hid_sem.value);
    finish_snapshot(8);
}
static void test_endpoints(void) {
    set_mouse(1);
    unsigned int before_writes = writes, before_queries = busy_queries;
    const uint8_t invalid[] = {0x00, 0x01, 0x80, 0x88, 0x89, 0x90, 0xFF};
    for (size_t i = 0; i < sizeof(invalid); ++i) {
        actual_eps[0].ep_addr = invalid[i];
        unsigned int before_takes = takes, before_gives = gives;
        assert(send_current() == -EINVAL);
        assert(takes == before_takes + 1U && gives == before_gives + 1U && hid_sem.value == 1);
    }
    actual_eps[1].ep_addr = 0x81; actual_eps[0].ep_addr = 0x02;
    assert(send_current() == -EINVAL); /* Only index0 is IN; do not hunt for another. */
    actual_eps[1].ep_addr = 0x02; actual_eps[0].ep_addr = 0x85;
    actual_cfg.num_endpoints = 0; assert(send_current() == -ENODEV); actual_cfg.num_endpoints = 2;
    actual_cfg.endpoint = NULL; assert(send_current() == -ENODEV); actual_cfg.endpoint = actual_eps;
    device.config = NULL; assert(send_current() == -ENODEV); device.config = &actual_cfg;
    hid_dev = NULL;
    unsigned int before_takes = takes;
    assert(send_current() == -ENODEV && takes == before_takes); hid_dev = &device;
    assert(writes == before_writes && busy_queries == before_queries);
    for (uint8_t ep = 0x81; ep <= 0x87; ++ep) {
        actual_eps[0].ep_addr = ep;
        assert(send_current() == 0 && dma.ep == ep && (unsigned int)last_busy_ep == ep);
        finish_snapshot(1);
    }
    actual_eps[0].ep_addr = 0x85;
    assert(send_current() == 0); const uint8_t *saved_source = dma.source;
    registered_ops->int_in_ready(&device);
    actual_eps[0].ep_addr = 0x87; set_mouse(2); before_writes = writes;
    assert(send_current() == -EBUSY && last_busy_ep == NRFX_USBD_EPIN5 && writes == before_writes);
    assert(dma.source == saved_source); expect_pending(1);
    abort_dma_no_ready(); nrf_busy[0x87] = true; /* Foreign use of the NEW endpoint. */
    unsigned int before_gives = gives;
    assert(send_current() == -EBUSY && last_busy_ep == NRFX_USBD_EPIN7 && writes == before_writes);
    assert(gives == before_gives + 1U && hid_sem.value == 1);
    nrf_busy[0x87] = false;
    assert(send_current() == 0 && dma.ep == 0x87 && !hid_sem.value);
    registered_ops->int_in_ready(&device); before_writes = writes;
    assert(send_current() == -EBUSY && last_busy_ep == NRFX_USBD_EPIN7 && writes == before_writes);
    finish_snapshot(2); actual_eps[0].ep_addr = 0x85;
}
static void test_isr(void) {
    set_mouse(1);
    unsigned int before_takes = takes, before_writes = writes, before_gives = gives;
    unsigned int before_queries = busy_queries;
    mock_in_isr = true;
    assert(send_current() == -EWOULDBLOCK);
    assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(NULL, bbp9981_usb_hid_connection_epoch()) == -EINVAL);
    assert(takes == before_takes && writes == before_writes && gives == before_gives && busy_queries == before_queries);
    mock_in_isr = false;
    assert(send_current() == 0 && !hid_sem.value);
    mock_in_isr = true; before_queries = busy_queries; before_gives = gives;
    registered_ops->int_in_ready(&device); /* Stock callback remains unhooked in ISR too. */
    assert(hid_sem.value == 1 && gives == before_gives + 1U && busy_queries == before_queries);
    assert(send_current() == -EWOULDBLOCK && busy_queries == before_queries);
    mock_in_isr = false; set_mouse(2); before_writes = writes;
    assert(send_current() == -EBUSY && writes == before_writes); finish_snapshot(1);
}
static void test_errors(void) {
    set_mouse(1);
    const int timeout_errors[] = {-EAGAIN, -ETIMEDOUT};
    for (size_t i = 0; i < sizeof(timeout_errors) / sizeof(timeout_errors[0]); ++i) {
        unsigned int before_gives = gives, before_writes = writes, before_queries = busy_queries;
        take_error = timeout_errors[i];
        assert(send_current() == take_error && writes == before_writes);
        assert(gives == before_gives && busy_queries == before_queries && hid_sem.value == 1);
    }
    take_error = 0; write_error = -EIO;
    unsigned int before_gives = gives, before_writes = writes;
    assert(send_current() == -EIO && !dma.pending && writes == before_writes + 1U);
    assert(gives == before_gives + 1U && hid_sem.value == 1);
    write_error = 0;
    assert(send_current() == 0 && !hid_sem.value); finish_snapshot(1);
    /* Even a write error must not imply that an accepted pointer is no longer
     * live. Keep RAM owned while preserving stock's write-error permit give. */
    after_accept_error = -EIO; before_gives = gives;
    assert(send_current() == -EIO && dma.pending && hid_sem.value == 1);
    assert(gives == before_gives + 1U); expect_pending(1);
    after_accept_error = 0; set_mouse(2); before_writes = writes; before_gives = gives;
    assert(send_current() == -EBUSY && writes == before_writes && gives == before_gives + 1U);
    finish_snapshot(1);
    assert(send_current() == 0); finish_snapshot(2);
}
static void suspend_during_wait(void) { actual_cb(USB_DC_SUSPEND, NULL); }
static void test_status_and_wait(void) {
    set_mouse(1);
    const enum usb_dc_status_code invalid[] = {
        USB_DC_ERROR, USB_DC_RESET, USB_DC_DISCONNECTED, USB_DC_UNKNOWN};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        actual_cb(invalid[i], NULL);
        unsigned int before_takes = takes, before_gives = gives, before_writes = writes;
        assert(send_current() == -ENODEV && takes == before_takes && gives == before_gives && writes == before_writes);
        actual_cb(USB_DC_CONFIGURED, NULL);
    }
    actual_cb(USB_DC_SUSPEND, NULL);
    unsigned int before_takes = takes, before_gives = gives, before_writes = writes;
    assert(send_current() == -EAGAIN && takes == before_takes && gives == before_gives && writes == before_writes);
    actual_cb(USB_DC_CONFIGURED, NULL);
    during_wait = suspend_during_wait;
    assert(send_current() == -ENODEV && writes == before_writes && gives == before_gives + 1U && hid_sem.value == 1);
    actual_cb(USB_DC_CONFIGURED, NULL);
    during_wait = reconnect; before_gives = gives;
    assert(send_current() == -ESTALE && writes == before_writes && gives == before_gives + 1U);
    before_irq_lock = reconnect; before_gives = gives;
    assert(send_current() == -ESTALE && writes == before_writes && gives == before_gives + 1U);
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    during_wait = switch_stock_to_boot;
    before_takes = takes; before_gives = gives;
    assert(send_current() == -ENOTSUP && writes == before_writes);
    assert(takes == before_takes + 1U && gives == before_gives + 1U && hid_sem.value == 1);
    zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
    before_irq_lock = switch_stock_to_boot; before_gives = gives;
    assert(send_current() == -ENOTSUP && writes == before_writes && gives == before_gives + 1U);
    zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
#endif
    const enum usb_dc_status_code allowed[] = {
        USB_DC_CONFIGURED, USB_DC_RESUME, USB_DC_CLEAR_HALT, USB_DC_SOF};
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        actual_cb(allowed[i], NULL);
        assert(send_current() == 0 && !hid_sem.value); finish_snapshot(1);
    }
    actual_cb(USB_DC_CONFIGURED, NULL);
    struct zmk_hid_mouse_report_body source = global_mouse.body; source.buttons = 0;
    during_wait = foreign_right;
    assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source, bbp9981_usb_hid_connection_epoch()) == 0);
    assert(source.buttons == 0 && source.d_x == 91 && global_mouse.body.d_x == 91);
    finish_snapshot(3);
    set_mouse(0x1F);
    assert(send_current() == 0); finish_snapshot(0x1F); /* Fresh ALL-owner bits, not tap-only. */
}
static void test_stock_unchanged(void) {
    assert(registered_ops == &ops && registered_ops->int_in_ready == in_ready_cb &&
           registered_ops->get_report == get_report_cb && registered_ops->set_report == set_report_cb);
    keyboard.report_id = ZMK_HID_REPORT_ID_KEYBOARD; keyboard.body[0] = 42;
    assert(zmk_usb_hid_send_keyboard_report() == 0 && dma.source == (const uint8_t *)&keyboard);
    complete_dma(); assert(hid_sem.value == 1 && dma.completed[0] == ZMK_HID_REPORT_ID_KEYBOARD);
    consumer.report_id = ZMK_HID_REPORT_ID_CONSUMER; consumer.body[0] = 7;
    assert(zmk_usb_hid_send_consumer_report() == 0 && dma.source == (const uint8_t *)&consumer);
    complete_dma(); assert(hid_sem.value == 1 && dma.completed[0] == ZMK_HID_REPORT_ID_CONSUMER);
    set_mouse(1);
    assert(zmk_usb_hid_send_mouse_report() == 0 && dma.source == (const uint8_t *)&global_mouse);
    /* Deliberately preserve, do not silently fix, stock's mutable DMA semantics. */
    global_mouse.body.d_x = 812;
    complete_dma(); assert(last_wire.body.d_x == 812 && last_wire.body.d_y == -47);
    set_mouse(2); assert(send_current() == 0); finish_snapshot(2);
    /* A stock sender may take the real permit after snapshot completion. Its
     * missing ready is NOT permission to give hid_sem based on old safe RAM. */
    assert(zmk_usb_hid_send_keyboard_report() == 0 && !hid_sem.value);
    unsigned int before_writes = writes, before_gives = gives, before_queries = busy_queries;
    assert(send_current() == -EAGAIN && writes == before_writes && gives == before_gives);
    assert(busy_queries == before_queries);
    abort_dma_no_ready();
    assert(send_current() == -EAGAIN && !hid_sem.value && writes == before_writes && gives == before_gives);
    assert(busy_queries == before_queries); /* Even busy=false cannot invent a permit. */
    registered_ops->int_in_ready(&device);
    before_gives = gives;
    assert(send_current() == 0 && !hid_sem.value && gives == before_gives); finish_snapshot(2);
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "normal";
    if (!strcmp(mode, "legacy")) { test_sdk_existing_contract(); }
    else if (!strcmp(mode, "lifetime")) { test_snapshot_lifetime(); }
    else if (!strcmp(mode, "protocol-wait")) { test_snapshot_protocol_wait(); }
    else {
        fixture_init();
        if (!strcmp(mode, "busy-ready")) { test_busy_ready(); }
        else if (!strcmp(mode, "submitting")) { test_submitting(); }
        else if (!strcmp(mode, "submit-retry")) { test_submit_retry(); }
        else if (!strcmp(mode, "reset-abort")) { test_reset_abort(); }
        else if (!strcmp(mode, "endpoints")) { test_endpoints(); }
        else if (!strcmp(mode, "isr")) { test_isr(); }
        else if (!strcmp(mode, "errors")) { test_errors(); }
        else if (!strcmp(mode, "status")) { test_status_and_wait(); }
        else if (!strcmp(mode, "stock")) { test_stock_unchanged(); }
        else {
            assert(!strcmp(mode, "normal"));
            test_deferred_source(); test_busy_ready(); test_submitting(); test_submit_retry();
            test_reset_abort(); test_endpoints(); test_isr(); test_errors();
            test_status_and_wait(); test_stock_unchanged();
        }
    }
    assert(!irq_depth && !dma.pending && hid_sem.value == 1);
    printf("PASS snapshot stability BOOT=%d %s: deferred source, real shared permit, saved EP RAM gate, "
           "SUBMITTING/retry, current epoch and post-wait protocol; no host ACK/device claim\n",
           IS_ENABLED(CONFIG_ZMK_USB_BOOT), mode);
    return 0;
}
