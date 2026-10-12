/* SPDX-License-Identifier: MIT -- ZMK v0.3.0 mouse body layout */
#ifndef HOG_SNAPSHOT_MOCK_HID_H
#define HOG_SNAPSHOT_MOCK_HID_H
#include <stdint.h>
typedef uint8_t zmk_mouse_button_flags_t;
struct zmk_hid_mouse_report_body {
    zmk_mouse_button_flags_t buttons;
    int16_t d_x, d_y, d_scroll_y, d_scroll_x;
} __attribute__((packed));
#endif
