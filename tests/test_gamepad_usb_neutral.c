/* SPDX-License-Identifier: MIT
 * Original USB fixture + actual separately linked neutral shim.
 * No runtime mock definitions in this build. This is NOT device enumeration.
 */
#define BBP_TEST_REAL_NEUTRAL 1
#define main original_usb_test_main
#include "test_gamepad_usb.c"
#undef main

static void assert_neutral_read(void) {
    const uint8_t expected[] = {4, 0, 0, 8, 0, 0, 0, 0, 0, 0};
    int32_t len = -1;
    uint8_t *data = NULL;
    assert(control_get(1, 0x104, &len, &data) == 0);
    assert(len == 10 && !memcmp(data, expected, sizeof(expected)));
    assert(data == (uint8_t *)&bbp_usb_control_snapshot);
    assert(!dma[1].writes && !dma[1].pending && !wakeup_calls);
    assert(bbp_gamepad_context_epoch() == 0U);
}
int main(void) {
    init_fixture();
    test_descriptors();
    for (unsigned int transport = 0; transport <= UINT8_MAX; ++transport) {
        struct bbp_gamepad_report_body body;
        memset(&body, 0xA5, sizeof(body));
        bbp_gamepad_get_report((uint8_t)transport, &body);
        assert(bbp_gamepad_is_neutral(&body));
    }
    bbp_gamepad_get_report(0, NULL);
    const uint32_t epochs[] = {0U, 1U, UINT32_MAX};
    for (size_t i = 0; i < sizeof(epochs) / sizeof(epochs[0]); ++i) {
        assert(!bbp_gamepad_context_valid(epochs[i]));
        bbp_gamepad_transport_invalidate();
        assert_neutral_read();
    }
    /* No keyboard/trackpad input ownership: even polluted fixture state cannot
     * change the separately linked production shim's report. */
    runtime_body = pressed_report().body;
    media_active = true; usb_selected = true;
    assert_neutral_read();
    struct bbp_gamepad_report pressed = pressed_report();
    assert(bbp_gamepad_usb_send(&pressed.body, bbp9981_usb_hid_connection_epoch(), 0) == -ESTALE);
    assert(!takes && !dma[1].writes);
    assert_stock_works();
    assert_neutral_read();
    uint8_t zero = 0;
    actual_usb_callback(USB_DC_CONFIGURED, &zero);
    assert(!bbp_gamepad_usb_is_configured() && zmk_usb_is_hid_ready());
    actual_usb_callback(USB_DC_CONFIGURED, NULL);
    assert(!bbp_gamepad_usb_is_configured());
    assert_neutral_read();
    bus_event(USB_DC_CONFIGURED);
    assert(bbp_gamepad_usb_is_configured());
    bus_event(USB_DC_SUSPEND);
    assert(bbp_gamepad_usb_is_configured());
    assert_neutral_read();
    bus_event(USB_DC_RESUME);
    bus_event(USB_DC_SOF);
    assert_neutral_read();
    pad_boot();
    int32_t len = 0; uint8_t *data = NULL;
    assert(control_get(1, 0x104, &len, &data) == -ENOTSUP);
    const enum usb_dc_status_code reset[] = {USB_DC_RESET, USB_DC_DISCONNECTED, USB_DC_UNKNOWN};
    for (size_t i = 0; i < sizeof(reset) / sizeof(reset[0]); ++i) {
        pad_boot();
        bus_event(reset[i]);
        assert(!bbp_gamepad_usb_is_configured());
        assert(bbp_usb_gamepad_protocol == HID_PROTOCOL_REPORT);
        assert_neutral_read();
        bus_event(USB_DC_CONFIGURED);
    }
    const enum usb_dc_status_code other[] = {USB_DC_ERROR, USB_DC_CONNECTED};
    for (size_t i = 0; i < sizeof(other) / sizeof(other[0]); ++i) {
        bus_event(other[i]);
        assert(!bbp_gamepad_usb_is_configured());
        assert_neutral_read();
        bus_event(USB_DC_CONFIGURED);
    }
    assert_stock_works();
    assert_neutral_read();
    assert(enable_calls == 1U && registration_calls[0] == 1U && registration_calls[1] == 1U);
    assert(!irq_depth && !dma[1].writes && !dma[1].pending);
    printf("PASS actual neutral shim + original lifecycle BOOT=%d: ID4 neutral GET_REPORT, zero HID_1 writes, HID_0 preserved\n", CONFIG_ZMK_USB_BOOT);
    return 0;
}
