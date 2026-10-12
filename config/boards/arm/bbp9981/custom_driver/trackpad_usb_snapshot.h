/* Inject ONLY into pinned ZMK v0.3.0 usb_hid.c, not into normal callers.
 * Adds a body snapshot sender within the original SDK translation unit.
 * Uses the SAME HID endpoint semaphore as keyboard/consumer/movement reports.
 * Producer stays nonblocking; the dedicated output worker may wait <=30ms.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zmk/hid.h>
#include <zmk/usb.h>
#if IS_ENABLED(CONFIG_ZMK_POINTING)
/* These match the pinned SDK declarations later in this SAME source file. */
static const struct device *hid_dev;
static struct k_sem hid_sem;
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
static uint8_t hid_protocol;
#endif
uint32_t bbp9981_usb_hid_connection_epoch(void);
int bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(struct zmk_hid_mouse_report_body *body,
                                                     uint32_t expected_epoch) {
    if (!body) { return -EINVAL; }
    if (bbp9981_usb_hid_connection_epoch()!=expected_epoch) { return -ESTALE; }
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol == HID_PROTOCOL_BOOT) { return -ENOTSUP; }
#endif
    switch (zmk_usb_get_status()) {
    case USB_DC_CONFIGURED: case USB_DC_RESUME: case USB_DC_CLEAR_HALT: case USB_DC_SOF:
        break;
    case USB_DC_SUSPEND:
        return -EAGAIN; /* Wakeup success is NOT report acceptance. */
    default:
        return -ENODEV;
    }
    if (!hid_dev) { return -ENODEV; }
    struct zmk_hid_mouse_report copy = {.report_id=ZMK_HID_REPORT_ID_MOUSE,.body=*body};
    copy.body.d_x=copy.body.d_y=copy.body.d_scroll_x=copy.body.d_scroll_y=0;
    int err=k_sem_take(&hid_sem,K_MSEC(30));
    if (err) { return err; } /* Unlike stock helper, never write after take timeout. */
    if (bbp9981_usb_hid_connection_epoch()!=expected_epoch) {
        k_sem_give(&hid_sem);
        return -ESTALE;
    }
    /* A physical/foreign owner may have changed during the semaphore wait. */
    copy.body.buttons=zmk_hid_get_mouse_report()->body.buttons;
    switch (zmk_usb_get_status()) {
    case USB_DC_CONFIGURED: case USB_DC_RESUME: case USB_DC_CLEAR_HALT: case USB_DC_SOF:
        err=hid_int_ep_write(hid_dev,(const uint8_t *)&copy,sizeof(copy),NULL);
        break;
    default: err=-ENODEV; break;
    }
    if (err) { k_sem_give(&hid_sem); }
    return err; /* Endpoint accepted a copy, not evidence of host-level click. */
}
#endif
