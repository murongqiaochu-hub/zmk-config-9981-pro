/* SPDX-License-Identifier: MIT
 * Final-release boundary tests on the unchanged pinned SDK + v2.3.1 prefix.
 * No fake late-ready or direct semaphore repair: only a real modeled stock
 * transfer completion may restore its permit. Not physical USB acceptance.
 */
#define BBP_USB_SNAPSHOT_NO_MAIN 1
#include "test_sdk_usb.c"

static int snapshot_current(void) {
    return bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(
        &global_mouse.body, bbp9981_usb_hid_connection_epoch());
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "keyboard";
    bool keyboard_mode = !strcmp(mode, "keyboard");
    bool consumer_mode = !strcmp(mode, "consumer");
    bool mouse_mode = !strcmp(mode, "mouse");
    bool mutable_mode = !strcmp(mode, "mutable-mouse");
    bool boot_mode = !strcmp(mode, "boot-keyboard");
    assert(keyboard_mode || consumer_mode || mouse_mode || mutable_mode || boot_mode);
#if !IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    assert(!boot_mode);
#endif
    fixture_init();
    for (unsigned int i = 0; i < 500U; ++i) {
        assert(!dma.pending && hid_sem.value == 1 && !irq_depth);
        global_mouse = (struct zmk_hid_mouse_report){.report_id = ZMK_HID_REPORT_ID_MOUSE,
            .body = {.buttons = 1, .d_x = 91, .d_y = -47, .d_scroll_x = 4, .d_scroll_y = 5}};
        assert(snapshot_current() == 0 && dma.pending && !hid_sem.value);
        abort_dma_no_ready(); reconnect();
        unsigned int before_writes = writes, before_gives = gives, before_queries = busy_queries;
        for (unsigned int n = 0; n < 3U; ++n) {
            assert(snapshot_current() == -EAGAIN && !dma.pending && !hid_sem.value);
        }
        assert(writes == before_writes && gives == before_gives && busy_queries == before_queries);
        /* Known stock behavior: even without its semaphore permit, the native
         * sender attempts after its 30ms wait. Recovery below is CONDITIONAL on
         * that write being accepted and actually completing, not on RAM safe. */
        if (keyboard_mode || boot_mode) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
            if (boot_mode) { zmk_usb_hid_set_protocol(HID_PROTOCOL_BOOT); }
#endif
            keyboard.report_id = ZMK_HID_REPORT_ID_KEYBOARD; keyboard.body[0] = (uint8_t)i;
            assert(zmk_usb_hid_send_keyboard_report() == 0);
        } else if (consumer_mode) {
            consumer.report_id = ZMK_HID_REPORT_ID_CONSUMER; consumer.body[0] = (uint8_t)i;
            assert(zmk_usb_hid_send_consumer_report() == 0);
        } else {
            assert(zmk_usb_hid_send_mouse_report() == 0);
            if (mutable_mode) {
                assert(dma.source == (const uint8_t *)&global_mouse);
                /* Mirrors the fixed SDK input listener clearing movement after
                 * send. A deliberately delayed feeder observes the cleared
                 * global body. This is a RETAINED RISK, not a new fix or a claim
                 * that real IRQ ordering always delays consumption this way. */
                global_mouse.body.d_x = global_mouse.body.d_y = 0;
                global_mouse.body.d_scroll_x = global_mouse.body.d_scroll_y = 0;
            }
        }
        assert(dma.pending && !hid_sem.value && writes == before_writes + 1U && gives == before_gives);
        complete_dma(); /* The only ready since abort is this native completion. */
        assert(!dma.pending && hid_sem.value == 1 && gives == before_gives + 1U);
        if (mutable_mode) {
            assert(last_wire.body.buttons == 1 && !last_wire.body.d_x && !last_wire.body.d_y &&
                   !last_wire.body.d_scroll_x && !last_wire.body.d_scroll_y);
        }
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
        if (boot_mode) { zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT); }
#endif
        global_mouse.body.buttons = 0;
        assert(snapshot_current() == 0 && dma.pending && !hid_sem.value);
        complete_dma(); assert_snapshot_wire(0);
    }
    assert(!dma.pending && hid_sem.value == 1 && !irq_depth);
    printf("PASS final USB BOOT=%d %s: 500 uninterrupted abort/reconnect cycles, "
           "snapshot alone stays fail-closed; accepted native completion restores permit and fresh snapshot; "
           "no invented late-ready, no global DMA repair or physical recovery claim\n",
           IS_ENABLED(CONFIG_ZMK_USB_BOOT), mode);
    return 0;
}
