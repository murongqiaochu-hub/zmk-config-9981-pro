/* SPDX-License-Identifier: MIT
 * Host HID ABI subset: original body field widths. The immutable descriptor is
 * a small representative IDs1..3 fixture, NOT the full pinned HID descriptor.
 * Report Map tests prove byte-for-byte prefix preservation/slicing, not Android
 * enumeration or semantic validation of the stock keyboard/mouse descriptor.
 */
#pragma once
#include <zephyr/sys/util.h>
#define ZMK_HID_REPORT_ID_KEYBOARD 1
#define ZMK_HID_REPORT_ID_LEDS 1
#define ZMK_HID_REPORT_ID_CONSUMER 2
#define ZMK_HID_REPORT_ID_MOUSE 3
static const uint8_t zmk_hid_report_desc[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x75,0x08,0x95,0x08,0x81,0x02,0xc0,
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,0x75,0x10,0x95,0x06,0x81,0x00,0xc0,
    0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x03,0x75,0x08,0x95,0x09,0x81,0x06,0xc0,
};
struct zmk_hid_keyboard_report_body { uint8_t modifiers, _reserved, keys[6]; } __packed;
struct zmk_hid_keyboard_report { uint8_t report_id; struct zmk_hid_keyboard_report_body body; } __packed;
struct zmk_hid_consumer_report_body { uint16_t keys[6]; } __packed;
struct zmk_hid_consumer_report { uint8_t report_id; struct zmk_hid_consumer_report_body body; } __packed;
struct zmk_hid_led_report_body { uint8_t leds; } __packed;
typedef uint8_t zmk_mouse_button_flags_t;
struct zmk_hid_mouse_report_body {
    zmk_mouse_button_flags_t buttons;
    int16_t d_x, d_y, d_scroll_y, d_scroll_x;
} __packed;
struct zmk_hid_mouse_report { uint8_t report_id; struct zmk_hid_mouse_report_body body; } __packed;
struct zmk_hid_mouse_resolution_feature_report_body {
    uint8_t wheel_res : 4;
    uint8_t hwheel_res : 4;
} __packed;
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void);
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void);
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void);
