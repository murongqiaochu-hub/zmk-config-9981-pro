#pragma once
#include <stdbool.h>
#include <zephyr/usb/usb_device.h>
enum zmk_usb_conn_state { ZMK_USB_CONN_NONE, ZMK_USB_CONN_POWERED, ZMK_USB_CONN_HID };
enum usb_dc_status_code zmk_usb_get_status(void);
enum zmk_usb_conn_state zmk_usb_get_conn_state(void);
bool zmk_usb_is_hid_ready(void);
