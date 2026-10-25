#pragma once
#include <stdint.h>
#include <zephyr/sys/util.h>
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
void zmk_usb_hid_set_protocol(uint8_t protocol);
#endif
