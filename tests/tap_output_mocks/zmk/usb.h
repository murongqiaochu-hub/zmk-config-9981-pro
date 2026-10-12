/* Enum order verified against Zephyr v3.5.0+zmk-fixes drivers/usb/usb_dc.h.
 * Tests still use symbols, never transport/status integer index assumptions. */
#pragma once
#include <stdbool.h>
enum usb_dc_status_code {
    USB_DC_ERROR, USB_DC_RESET, USB_DC_CONNECTED, USB_DC_CONFIGURED,
    USB_DC_DISCONNECTED, USB_DC_SUSPEND, USB_DC_RESUME, USB_DC_INTERFACE,
    USB_DC_SET_HALT, USB_DC_CLEAR_HALT, USB_DC_SOF, USB_DC_UNKNOWN
};
bool zmk_usb_is_hid_ready(void);
enum usb_dc_status_code zmk_usb_get_status(void);
