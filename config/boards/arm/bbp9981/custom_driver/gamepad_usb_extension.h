/* SPDX-License-Identifier: MIT
 * Standalone USB Game Pad. Include from gamepad_usb.c ONLY, never as a ZMK
 * usb_hid.c prefix. HID_0's descriptor, callbacks, semaphore and writes remain
 * untouched. CONFIG_USB_HID_DEVICE_COUNT creates the separate HID_1 instance.
 *
 * This RAM lifetime proof is specific to nRF52840 + pinned Zephyr/nrfx USB:
 * hal_nordic 884c4d nrfx_usbd_ep_is_busy() covers waiting | ~ready; RAM feeder
 * and EasyDMA run in the USBD IRQ, which waits for ENDEPIN before returning.
 * In THREAD context, with that IRQ masked by irq_lock(), busy=false proves
 * that the saved endpoint no longer reads our 10-byte RAM source. It does NOT
 * prove host/application receipt, completion status, or a drained event queue.
 *
 * SUBMITTING starts BEFORE copying staging and covers the ENTIRE SDK write
 * (including usb_write retries). No callback can retire it. INFLIGHT can retire
 * only with saved-valid-endpoint busy=false under the same short IRQ lock.
 * Consequently an old ready with busy=true cannot release a newer transfer,
 * and reset/abort can recover without permanently disabling the Game Pad.
 * Never hold IRQ lock across the blocking SDK call, never use this oracle in
 * ISR context or on another controller/SoC, and never share this storage/sem
 * with keyboard/mouse. Already-entered SDK DMA cannot be revoked by an epoch.
 */
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <nrfx_usbd.h>
#include <zmk/usb.h>
#include "gamepad_runtime.h"

_Static_assert(IS_ENABLED(CONFIG_ZMK_USB), "Compile Game Pad USB TU only with ZMK_USB");
_Static_assert(IS_ENABLED(CONFIG_SOC_NRF52840_QIAA) && IS_ENABLED(CONFIG_USB_NRFX),
               "Game Pad RAM retirement proof requires nRF52840 QIAA + USB_NRFX");
_Static_assert(CONFIG_USB_HID_DEVICE_COUNT >= 2, "Game Pad requires separate HID_1");
_Static_assert(CONFIG_HID_INTERRUPT_EP_MPS >= sizeof(struct bbp_gamepad_report), "Game Pad USB MPS");
_Static_assert(!IS_ENABLED(CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT), "Register HID_1 before USB enable");
_Static_assert(CONFIG_KERNEL_INIT_PRIORITY_DEFAULT < 80 && 80 < CONFIG_APPLICATION_INIT_PRIORITY,
               "HID devices < pad init80 < USB descriptor fixup priority");
#if IS_ENABLED(CONFIG_USB_DEVICE_SOF)
_Static_assert(CONFIG_USB_HID_REPORTS >= BBP_GAMEPAD_REPORT_ID, "HID idle arrays must cover ID4");
#endif
uint32_t bbp9981_usb_hid_connection_epoch(void);
static const uint8_t bbp_usb_gamepad_desc[] = {BBP_GAMEPAD_DESCRIPTOR_BYTES};
static const struct device *bbp_usb_gamepad_dev;
static K_SEM_DEFINE(bbp_usb_gamepad_sem, 1, 1);
static uint8_t bbp_usb_gamepad_protocol = HID_PROTOCOL_REPORT;
/* EP0 requests are serialized by the stack. Never alias runtime or IN DMA. */
static struct bbp_gamepad_report bbp_usb_control_snapshot;
static union {
    uint32_t dma_alignment;
    struct bbp_gamepad_report report;
} bbp_usb_tx_storage;
_Static_assert(sizeof(struct bbp_gamepad_report) == 10U, "USB Game Pad ID4 + nine-byte body");
_Static_assert(sizeof(bbp_usb_control_snapshot) == 10U, "EP0 snapshot excludes DMA padding");
_Static_assert(sizeof(bbp_usb_tx_storage) == 12U, "Private uint32-aligned DMA storage");
enum bbp_usb_tx_state { BBP_USB_TX_IDLE, BBP_USB_TX_SUBMITTING, BBP_USB_TX_INFLIGHT };
static enum bbp_usb_tx_state bbp_usb_tx_state;
static uint32_t bbp_usb_tx_epoch;
static nrfx_usbd_ep_t bbp_usb_tx_ep;
static bool bbp_usb_tx_ep_known;

static bool bbp_usb_valid_in_ep(uint32_t ep) {
    /* Exclude control, ISO8, OUT, reserved/high bits and AUTO_EP_IN. */
    return ep >= 0x81U && ep <= 0x87U;
}
static int bbp_usb_gamepad_endpoint(nrfx_usbd_ep_t *out) {
    const struct usb_cfg_data *cfg = bbp_usb_gamepad_dev ? bbp_usb_gamepad_dev->config : NULL;
    /* In the pinned HID core, index0 is HID_INT_IN_EP_IDX. Read ONLY after the
     * USB descriptor fixup/enable path assigned the real automatic EP number. */
    if (!cfg || !cfg->endpoint || !cfg->num_endpoints) { return -ENODEV; }
    uint8_t ep = cfg->endpoint[0].ep_addr;
    if (!bbp_usb_valid_in_ep(ep)) { return -EINVAL; }
    *out = (nrfx_usbd_ep_t)ep;
    return 0;
}
static int bbp_usb_gamepad_guard(uint32_t usb_epoch, uint32_t context_epoch,
                                 bool needs_context) {
    if (!bbp_usb_gamepad_dev) { return -ENODEV; }
    if (bbp9981_usb_hid_connection_epoch() != usb_epoch) { return -ESTALE; }
    if (!bbp_gamepad_usb_is_configured()) { return -ENODEV; }
    if (bbp_usb_gamepad_protocol != HID_PROTOCOL_REPORT) { return -ENOTSUP; }
    if (needs_context && !bbp_gamepad_context_valid(context_epoch)) { return -ESTALE; }
    switch (zmk_usb_get_status()) {
    case USB_DC_CONFIGURED:
    case USB_DC_RESUME:
    case USB_DC_CLEAR_HALT:
    case USB_DC_SOF:
        return 0;
    case USB_DC_SUSPEND:
        return -EAGAIN; /* Wakeup success is NOT report acceptance. */
    default:
        return -ENODEV;
    }
}
static bool bbp_usb_retire_ram_locked(void) {
    /* THREAD context + IRQ lock are prerequisites, never an ISR busy oracle.
     * A missing/corrupt saved address is fail-closed, not permission to reuse. */
    if (bbp_usb_tx_state != BBP_USB_TX_INFLIGHT || !bbp_usb_tx_ep_known ||
        !bbp_usb_valid_in_ep((uint32_t)bbp_usb_tx_ep) ||
        nrfx_usbd_ep_is_busy(bbp_usb_tx_ep)) { return false; }
    bbp_usb_tx_state = BBP_USB_TX_IDLE;
    bbp_usb_tx_ep_known = false;
    return true;
}
static void bbp_usb_gamepad_in_ready(const struct device *dev) {
    if (dev != bbp_usb_gamepad_dev || k_is_in_isr() || zmk_usb_get_status() == USB_DC_SUSPEND) {
        return;
    }
    unsigned int key = irq_lock();
    /* SUBMITTING is immutable here even when this is an old callback and the
     * endpoint temporarily reads busy=false before SDK's next internal retry. */
    bool release = bbp_usb_retire_ram_locked();
    irq_unlock(key);
    if (release) { k_sem_give(&bbp_usb_gamepad_sem); }
}
static int bbp_usb_stage_gamepad(const struct bbp_gamepad_report *report,
                                 uint32_t usb_epoch, uint32_t context_epoch,
                                 bool needs_context, bool *entered_driver) {
    *entered_driver = false;
    int err = bbp_usb_gamepad_guard(usb_epoch, context_epoch, needs_context);
    nrfx_usbd_ep_t ep;
    if (!err) { err = bbp_usb_gamepad_endpoint(&ep); }
    if (err) { return err; }
    unsigned int key = irq_lock();
    if (bbp_usb_tx_state != BBP_USB_TX_IDLE) {
        err = -EBUSY;
    } else if (bbp9981_usb_hid_connection_epoch() != usb_epoch) {
        err = -ESTALE;
    } else if (nrfx_usbd_ep_is_busy(ep)) {
        err = -EBUSY;
    } else {
        /* Reserve ownership BEFORE the copy. This phase persists across all
         * preflight checks and the entire real hid_int_ep_write call. */
        bbp_usb_tx_state = BBP_USB_TX_SUBMITTING;
        bbp_usb_tx_epoch = usb_epoch;
        bbp_usb_tx_ep = ep;
        bbp_usb_tx_ep_known = true;
        bbp_usb_tx_storage.report = *report;
    }
    irq_unlock(key);
    if (err) { return err; }

    err = bbp_usb_gamepad_guard(usb_epoch, context_epoch, needs_context);
    key = irq_lock();
    if (!err && bbp9981_usb_hid_connection_epoch() != usb_epoch) { err = -ESTALE; }
    if (!err && !bbp_gamepad_usb_is_configured()) { err = -ENODEV; }
    if (!err && zmk_usb_get_status() == USB_DC_SUSPEND) { err = -EAGAIN; }
    if (!err && bbp_usb_gamepad_protocol != HID_PROTOCOL_REPORT) { err = -ENOTSUP; }
    if (!err && needs_context && !bbp_gamepad_context_valid(context_epoch)) { err = -ESTALE; }
    if (err) {
        bbp_usb_tx_state = BBP_USB_TX_IDLE; /* No SDK write was entered. */
        bbp_usb_tx_ep_known = false;
        irq_unlock(key);
        return err;
    }
    *entered_driver = true;
    irq_unlock(key);
    /* SDK may wait for a mutex/retry. IRQs must be enabled here. Epoch changes
     * during this call are detected, NOT claimed to revoke an entered DMA.
     * Send the 10-byte report subobject, never the aligned union's 12 bytes. */
    err = hid_int_ep_write(bbp_usb_gamepad_dev, (const uint8_t *)&bbp_usb_tx_storage.report,
                           sizeof(bbp_usb_tx_storage.report), NULL);
    key = irq_lock();
    bbp_usb_tx_state = BBP_USB_TX_INFLIGHT;
    bool release = bbp_usb_retire_ram_locked();
    if (!err && bbp9981_usb_hid_connection_epoch() != usb_epoch) { err = -ESTALE; }
    irq_unlock(key);
    /* Even error paths keep ownership if the actual saved EP is still busy.
     * Rejected writes normally retire immediately; early completion is equally
     * safe because busy=false, not because its callback was treated as ACK. */
    if (release) { k_sem_give(&bbp_usb_gamepad_sem); }
    return err;
}
int bbp_gamepad_usb_send(const struct bbp_gamepad_report_body *body,
                         uint32_t expected_usb_epoch, uint32_t expected_context_epoch) {
    if (!body) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    const struct bbp_gamepad_report report = {.report_id = BBP_GAMEPAD_REPORT_ID, .body = *body};
    bool needs_context = !bbp_gamepad_is_neutral(&report.body);
    int err = bbp_usb_gamepad_guard(expected_usb_epoch, expected_context_epoch, needs_context);
    nrfx_usbd_ep_t ep;
    if (!err) { err = bbp_usb_gamepad_endpoint(&ep); }
    if (err) { return err; }
    /* Retry/reset recovery uses the SAME RAM proof as completion. Do not
     * retire on SUSPEND, stale epoch or BOOT merely to make the sem look free. */
    unsigned int key = irq_lock();
    bool release = bbp_usb_retire_ram_locked();
    irq_unlock(key);
    if (release) { k_sem_give(&bbp_usb_gamepad_sem); }
    err = k_sem_take(&bbp_usb_gamepad_sem, K_MSEC(30));
    if (err) { return err; }
    bool entered_driver = false;
    /* Neutral may release an old context, but never a different USB epoch. */
    err = bbp_usb_gamepad_guard(expected_usb_epoch, expected_context_epoch, needs_context);
    if (!err) {
        err = bbp_usb_stage_gamepad(&report, expected_usb_epoch, expected_context_epoch,
                                    needs_context, &entered_driver);
    }
    if (err && !entered_driver) { k_sem_give(&bbp_usb_gamepad_sem); }
    return err;
}
static int bbp_usb_gamepad_get_report(const struct device *dev, struct usb_setup_packet *setup,
                                      int32_t *len, uint8_t **data) {
    if (dev != bbp_usb_gamepad_dev) { return -ENODEV; }
    if (!setup || !len || !data) { return -EINVAL; }
    if (setup->wValue != (0x0100U | BBP_GAMEPAD_REPORT_ID)) { return -ENOTSUP; }
    if (bbp_usb_gamepad_protocol != HID_PROTOCOL_REPORT) { return -ENOTSUP; }
    bbp_usb_control_snapshot.report_id = BBP_GAMEPAD_REPORT_ID;
    bbp_gamepad_get_report(0U, &bbp_usb_control_snapshot.body);
    *len = sizeof(bbp_usb_control_snapshot);
    *data = (uint8_t *)&bbp_usb_control_snapshot;
    return 0;
}
static int bbp_usb_gamepad_set_report(const struct device *dev, struct usb_setup_packet *setup,
                                      int32_t *len, uint8_t **data) {
    return -ENOTSUP; /* No output/feature/rumble report advertised. */
}
void bbp_gamepad_usb_bus_reset(void) {
    unsigned int key = irq_lock();
    bbp_usb_gamepad_protocol = HID_PROTOCOL_REPORT;
    irq_unlock(key);
    /* The class resets protocol on a new bus lifetime; never retire TX RAM. */
}
static void bbp_usb_gamepad_protocol_change(const struct device *dev, uint8_t protocol) {
    if (dev != bbp_usb_gamepad_dev) { return; }
    unsigned int key = irq_lock();
    bool changed = protocol != bbp_usb_gamepad_protocol;
    bbp_usb_gamepad_protocol = protocol;
    irq_unlock(key);
    if (changed) { bbp_gamepad_transport_invalidate(); }
    /* Protocol changes alone never retire source RAM. */
}
static const struct hid_ops bbp_usb_gamepad_ops = {
    .get_report = bbp_usb_gamepad_get_report,
    .set_report = bbp_usb_gamepad_set_report,
    .protocol_change = bbp_usb_gamepad_protocol_change,
    .int_in_ready = bbp_usb_gamepad_in_ready,
};
static bool bbp_usb_gamepad_init_attempted;
static int bbp_usb_gamepad_init_result = -ENODEV;
static int bbp_gamepad_usb_init(void) {
    /* Never append HID_1 twice to Zephyr's internal HID device list. */
    if (bbp_usb_gamepad_init_attempted) { return bbp_usb_gamepad_init_result; }
    bbp_usb_gamepad_init_attempted = true;
    const struct device *dev = device_get_binding("HID_1");
    if (!dev) { return bbp_usb_gamepad_init_result; }
    const struct usb_cfg_data *cfg = dev->config;
    struct usb_if_descriptor *iface = cfg ? cfg->interface_descriptor : NULL;
    if (!iface || iface->bLength != sizeof(*iface) ||
        iface->bDescriptorType != USB_DESC_INTERFACE || iface->bInterfaceClass != USB_BCC_HID) {
        bbp_usb_gamepad_init_result = -EINVAL;
        return bbp_usb_gamepad_init_result;
    }
    /* Public cfg/descriptor edits are legal ONLY before usb_set_config/enable.
     * Global BOOT initializes every HID subclass1; configure ONLY our HID_1 as
     * standard non-Boot HID 0/0. Leave HID_0 and all SDK sources untouched. */
    iface->bInterfaceSubClass = 0U;
    iface->bInterfaceProtocol = 0U;
    usb_hid_register_device(dev, bbp_usb_gamepad_desc, sizeof(bbp_usb_gamepad_desc),
                            &bbp_usb_gamepad_ops);
    bbp_usb_gamepad_init_result = usb_hid_init(dev);
    if (!bbp_usb_gamepad_init_result) { bbp_usb_gamepad_dev = dev; }
    return bbp_usb_gamepad_init_result;
}
/* HID device init/default <80, USB descriptor fixup/APP priority >80, and ZMK
 * enables USB only later at APPLICATION/USB_INIT_PRIORITY. No new server/enable. */
SYS_INIT(bbp_gamepad_usb_init, POST_KERNEL, 80);
