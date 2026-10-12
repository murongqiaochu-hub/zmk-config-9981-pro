/* Inject only into pinned SDK usb.c. Preserve its callback; record real bus lifetimes.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/usb/usb_device.h>
static atomic_t bbp9981_usb_epoch=ATOMIC_INIT(1);
static usb_dc_status_callback bbp9981_usb_delegate;
uint32_t bbp9981_usb_hid_connection_epoch(void) {
    return (uint32_t)atomic_get(&bbp9981_usb_epoch);
}
static void bbp9981_usb_status_epoch(enum usb_dc_status_code status,const uint8_t *params) {
    if (status==USB_DC_RESET || status==USB_DC_DISCONNECTED || status==USB_DC_UNKNOWN) {
        atomic_inc(&bbp9981_usb_epoch);
    }
    if (bbp9981_usb_delegate) { bbp9981_usb_delegate(status,params); }
}
static int bbp9981_usb_enable_epoch(usb_dc_status_callback delegate) {
    bbp9981_usb_delegate=delegate;
    return usb_enable(bbp9981_usb_status_epoch);
}
/* Only this SDK translation unit is affected; no early/second USB initialization. */
#define usb_enable bbp9981_usb_enable_epoch
