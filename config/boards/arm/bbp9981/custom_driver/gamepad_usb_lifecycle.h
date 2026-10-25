/* SPDX-License-Identifier: MIT
 * Force-include ONLY into pinned app/src/usb.c, AFTER trackpad_usb_epoch.h.
 * Chain its existing usb_enable alias: one real enable, unchanged stock status
 * delegation. The gamepad needs the raw CONFIGURED parameter because stock
 * ZMK treats CONFIGURED(0) as ready. This flag applies to HID_1 only.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/usb/usb_device.h>
#include "gamepad_runtime.h"
#ifndef usb_enable
#error "Gamepad raw lifecycle must follow the existing USB epoch prefix"
#endif
static atomic_t bbp_gamepad_configured = ATOMIC_INIT(0);
static usb_dc_status_callback bbp_gamepad_usb_status_delegate;
bool bbp_gamepad_usb_is_configured(void) {
    return atomic_get(&bbp_gamepad_configured) != 0;
}
static void bbp_gamepad_usb_raw_status(enum usb_dc_status_code status, const uint8_t *params) {
    switch (status) {
    case USB_DC_CONFIGURED:
        atomic_set(&bbp_gamepad_configured, params && *params != 0U);
        break;
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        atomic_set(&bbp_gamepad_configured, 0);
        bbp_gamepad_usb_bus_reset();
        break;
    case USB_DC_ERROR:
    case USB_DC_CONNECTED:
        atomic_set(&bbp_gamepad_configured, 0);
        break;
    default:
        break; /* SUSPEND preserves configuration, never grants send readiness. */
    }
    if (status != USB_DC_SOF) { bbp_gamepad_transport_invalidate(); }
    if (bbp_gamepad_usb_status_delegate) { bbp_gamepad_usb_status_delegate(status, params); }
}
static int bbp_gamepad_usb_enable_raw(usb_dc_status_callback delegate) {
    bbp_gamepad_usb_status_delegate = delegate;
    /* Expanded NOW to bbp9981_usb_enable_epoch, before replacing this alias.
     * That wrapper still calls the one genuine usb_enable and owns bus epochs. */
    return usb_enable(bbp_gamepad_usb_raw_status);
}
#undef usb_enable
#define usb_enable bbp_gamepad_usb_enable_raw
