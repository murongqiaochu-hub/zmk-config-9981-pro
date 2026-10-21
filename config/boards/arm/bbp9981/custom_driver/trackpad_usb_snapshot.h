/* Inject ONLY into pinned ZMK v0.3.0 usb_hid.c, not into normal callers.
 * Adds a body snapshot sender within the original SDK translation unit.
 * Uses the SAME HID endpoint semaphore as keyboard/consumer/movement reports.
 * Producer stays nonblocking; the dedicated output worker may wait <=30ms.
 *
 * The pinned nRF52840/USB_NRFX write retains a RAM pointer, not a copy. Private
 * uint32-aligned staging therefore stays owned across the ENTIRE SDK write
 * (including retries) and until its saved actual IN endpoint is no longer busy.
 * In THREAD context under irq_lock(), the pinned USBD IRQ/feeder's busy=false
 * proves RAM is no longer read. It is NOT host ACK or a semaphore-release proof:
 * a stock sender may already own hid_sem, so RAM retirement NEVER gives it.
 * Keep stock callbacks, ignored-timeout behavior and mutable reports untouched.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zmk/hid.h>
#include <zmk/usb.h>
#if IS_ENABLED(CONFIG_ZMK_POINTING)
#include <zephyr/irq.h>
#include <nrfx_usbd.h>
_Static_assert(IS_ENABLED(CONFIG_SOC_NRF52840_QIAA) && IS_ENABLED(CONFIG_USB_NRFX),
               "Mouse snapshot RAM retirement requires nRF52840 QIAA + USB_NRFX");
/* These match the pinned SDK declarations later in this SAME source file. */
static const struct device *hid_dev;
static struct k_sem hid_sem;
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
static uint8_t hid_protocol;
#endif
static union {
    uint32_t dma_alignment;
    struct zmk_hid_mouse_report report;
} bbp9981_snapshot_storage;
enum bbp9981_snapshot_owner {
    BBP9981_SNAPSHOT_IDLE, BBP9981_SNAPSHOT_SUBMITTING, BBP9981_SNAPSHOT_INFLIGHT
};
static enum bbp9981_snapshot_owner bbp9981_snapshot_owner;
static nrfx_usbd_ep_t bbp9981_snapshot_ep;
static bool bbp9981_snapshot_ep_known;

static bool bbp9981_snapshot_valid_ep(uint32_t ep) {
    /* Exclude control, ISO8, OUT, reserved bits and unassigned AUTO_EP_IN. */
    return ep >= 0x81U && ep <= 0x87U;
}
static int bbp9981_snapshot_endpoint(nrfx_usbd_ep_t *out) {
    const struct usb_cfg_data *cfg = hid_dev ? hid_dev->config : NULL;
    /* Pinned HID core: index0 is HID_INT_IN_EP_IDX; use the assigned address. */
    if (!cfg || !cfg->endpoint || !cfg->num_endpoints) { return -ENODEV; }
    uint8_t ep = cfg->endpoint[0].ep_addr;
    if (!bbp9981_snapshot_valid_ep(ep)) { return -EINVAL; }
    *out = (nrfx_usbd_ep_t)ep;
    return 0;
}
static void bbp9981_snapshot_retire_ram_locked(void) {
    /* THREAD + the same short IRQ lock are required. No retirement while the
     * SDK could still retry, even if an old ready reports temporary busy=false. */
    if (bbp9981_snapshot_owner == BBP9981_SNAPSHOT_INFLIGHT &&
        bbp9981_snapshot_ep_known &&
        bbp9981_snapshot_valid_ep((uint32_t)bbp9981_snapshot_ep) &&
        !nrfx_usbd_ep_is_busy(bbp9981_snapshot_ep)) {
        bbp9981_snapshot_owner = BBP9981_SNAPSHOT_IDLE;
        bbp9981_snapshot_ep_known = false;
    }
    /* Deliberately NO k_sem_give(): this retires only our private source RAM. */
}
uint32_t bbp9981_usb_hid_connection_epoch(void);
int bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(struct zmk_hid_mouse_report_body *body,
                                                     uint32_t expected_epoch) {
    if (!body) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
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
    int err=k_sem_take(&hid_sem,K_MSEC(30));
    if (err) { return err; } /* Unlike stock helper, never write/give after timeout. */

    unsigned int key=irq_lock();
    /* Recheck the entire preflight after the wait, including REPORT -> BOOT.
     * Preserve the original post-wait status error (including SUSPEND). */
    if (bbp9981_usb_hid_connection_epoch()!=expected_epoch) { err=-ESTALE; }
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (!err && hid_protocol == HID_PROTOCOL_BOOT) { err=-ENOTSUP; }
#endif
    if (!err) {
        switch (zmk_usb_get_status()) {
        case USB_DC_CONFIGURED: case USB_DC_RESUME: case USB_DC_CLEAR_HALT: case USB_DC_SOF:
            break;
        default: err=-ENODEV; break;
        }
    }
    nrfx_usbd_ep_t ep;
    if (!err) { err=bbp9981_snapshot_endpoint(&ep); }
    if (!err) {
        bbp9981_snapshot_retire_ram_locked();
        if (bbp9981_snapshot_owner != BBP9981_SNAPSHOT_IDLE || nrfx_usbd_ep_is_busy(ep)) {
            err=-EBUSY;
        } else {
            /* Reserve BEFORE copying. Neither reentry nor early completion can
             * reclaim this storage until the complete SDK call has returned. */
            bbp9981_snapshot_owner=BBP9981_SNAPSHOT_SUBMITTING;
            bbp9981_snapshot_ep=ep;
            bbp9981_snapshot_ep_known=true;
            bbp9981_snapshot_storage.report=(struct zmk_hid_mouse_report){
                .report_id=ZMK_HID_REPORT_ID_MOUSE,.body=*body};
            /* A physical/foreign owner may have changed during the wait. */
            bbp9981_snapshot_storage.report.body.buttons=zmk_hid_get_mouse_report()->body.buttons;
            bbp9981_snapshot_storage.report.body.d_x=0;
            bbp9981_snapshot_storage.report.body.d_y=0;
            bbp9981_snapshot_storage.report.body.d_scroll_x=0;
            bbp9981_snapshot_storage.report.body.d_scroll_y=0;
        }
    }
    irq_unlock(key);
    if (err) { k_sem_give(&hid_sem); return err; } /* Only OUR just-taken permit. */

    /* Never hold IRQ lock across the potentially blocking/retrying SDK call.
     * Send just the report, not alignment padding from the storage union. */
    err=hid_int_ep_write(hid_dev,(const uint8_t *)&bbp9981_snapshot_storage.report,
                         sizeof(bbp9981_snapshot_storage.report),NULL);
    key=irq_lock();
    bbp9981_snapshot_owner=BBP9981_SNAPSHOT_INFLIGHT;
    bbp9981_snapshot_retire_ram_locked();
    irq_unlock(key);
    if (err) { k_sem_give(&hid_sem); } /* Original SDK write-error permit handling. */
    return err; /* SDK acceptance, not evidence of host-level click. */
}
#endif
