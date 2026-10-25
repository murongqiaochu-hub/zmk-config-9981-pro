/* Faithful Zephyr v3.5.0+zmk-fixes hid_ops layout and used API signatures:
 * https://github.com/zmkfirmware/zephyr/blob/v3.5.0%2Bzmk-fixes/include/zephyr/usb/class/usb_hid.h
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2018,2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 * Descriptor SHORT ITEM encodings reuse the existing fixed SDK HID harness.
 * The complete, unchanged ZMK hid.h is selected separately by -I include order.
 */
#pragma once
#include <zephyr/usb/usb_device.h>
#include "../../../../hid_snapshot_mocks/zephyr/usb/class/usb_hid.h"
#define HID_PROTOCOL_BOOT 0
#define HID_PROTOCOL_REPORT 1
#define HID_BOOT_IFACE_CODE_KEYBOARD 1

typedef int (*hid_cb_t)(const struct device *, struct usb_setup_packet *, int32_t *, uint8_t **);
typedef void (*hid_int_ready_callback)(const struct device *);
typedef void (*hid_protocol_cb_t)(const struct device *, uint8_t);
typedef void (*hid_idle_cb_t)(const struct device *, uint16_t);
struct hid_ops {
    hid_cb_t get_report;
    hid_cb_t set_report;
    hid_protocol_cb_t protocol_change;
    hid_idle_cb_t on_idle;
    hid_int_ready_callback int_in_ready;
#ifdef CONFIG_ENABLE_HID_INT_OUT_EP
    hid_int_ready_callback int_out_ready;
#endif
};
void usb_hid_register_device(const struct device *, const uint8_t *, size_t, const struct hid_ops *);
int hid_int_ep_write(const struct device *, const uint8_t *, uint32_t, uint32_t *);
int usb_hid_set_proto_code(const struct device *, uint8_t);
int usb_hid_init(const struct device *);
