/* SPDX-License-Identifier: MIT
 * One uninterrupted snapshot/stock fixture, deferred pointer consumption.
 * 50000 press/release cycles with bounded faults; not physical USB soak.
 */
#define BBP_USB_SNAPSHOT_NO_MAIN 1
#include "test_sdk_usb.c"

static int send_current_cycle(void) {
    return bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(
        &global_mouse.body, bbp9981_usb_hid_connection_epoch());
}

int main(void) {
    fixture_init();
    for (unsigned int i = 0; i < 50000U; ++i) {
        assert(!dma.pending && hid_sem.value == 1 && !irq_depth);
        if (i % 31U == 0U) {
            uint32_t old = bbp9981_usb_hid_connection_epoch();
            reconnect();
            unsigned int before_writes = writes, before_takes = takes;
            assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&global_mouse.body, old) == -ESTALE);
            assert(writes == before_writes && takes == before_takes);
        }
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
        if (i % 37U == 0U) {
            during_wait = switch_stock_to_boot;
            unsigned int before_writes = writes, before_gives = gives;
            assert(send_current_cycle() == -ENOTSUP);
            assert(writes == before_writes && gives == before_gives + 1U && hid_sem.value == 1);
            zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
        }
#endif
        if (i % 17U == 0U) {
            take_error = -EAGAIN;
            unsigned int before_writes = writes, before_gives = gives;
            assert(send_current_cycle() == -EAGAIN);
            assert(writes == before_writes && gives == before_gives);
            take_error = 0;
        }
        if (i % 23U == 0U) {
            write_error = -EIO;
            assert(send_current_cycle() == -EIO && hid_sem.value == 1 && !dma.pending);
            write_error = 0;
        }
        uint8_t buttons = (uint8_t)(1U + i % 31U);
        global_mouse.body.buttons = buttons;
        if (i % 7U == 0U) { during_wait = foreign_right; buttons |= 2U; }
        assert(send_caller_stack(bbp9981_usb_hid_connection_epoch()) == 0 && dma.pending);
        memset(&global_mouse, 0x7B, sizeof(global_mouse));
        clobber_stack();
        if (i % 19U == 0U) {
            in_ready_cb(&device); /* Delayed/stray ready cannot free live source. */
            const uint8_t *saved = dma.source;
            unsigned int before_writes = writes, before_gives = gives;
            assert(send_current_cycle() == -EBUSY && dma.source == saved);
            assert(writes == before_writes && gives == before_gives + 1U);
        }
        consume_dma_no_ready(); assert_snapshot_wire(buttons);
        if (i % 29U == 0U && !hid_sem.value) {
            unsigned int before_writes = writes, before_gives = gives, before_queries = busy_queries;
            assert(send_current_cycle() == -EAGAIN);
            assert(writes == before_writes && gives == before_gives && busy_queries == before_queries);
        }
        in_ready_cb(&device);
        global_mouse.body.buttons = 0; /* Necessary release still sends on same fixture. */
        assert(send_current_cycle() == 0 && !hid_sem.value);
        complete_dma(); assert_snapshot_wire(0);
        if (i % 41U == 0U) {
            assert(send_current_cycle() == 0 && dma.pending);
            abort_dma_no_ready(); reconnect();
            unsigned int before_writes = writes, before_gives = gives;
            assert(send_current_cycle() == -EAGAIN && !hid_sem.value);
            assert(writes == before_writes && gives == before_gives);
            /* Explicit late STOCK ready; never pretend RAM safe restores permit. */
            in_ready_cb(&device);
            assert(send_current_cycle() == 0); complete_dma(); assert_snapshot_wire(0);
        }
    }
    assert(!dma.pending && hid_sem.value == 1 && !irq_depth);
    printf("PASS USB snapshot continuous BOOT=%d: 50000 press/release cycles, %u writes; "
           "deferred source, global/stack overwrite, stale epoch, errors, stray/delayed ready and abort recovery; no fixture reset or device claim\n",
           IS_ENABLED(CONFIG_ZMK_USB_BOOT), writes);
    return 0;
}
