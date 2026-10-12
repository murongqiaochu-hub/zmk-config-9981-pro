/* Shapes/signatures mirrored from ZMK v0.3.0 app/include/zmk/hid.h, pointing.h. */
#pragma once
#include <stdint.h>
typedef uint8_t zmk_mouse_button_flags_t;
typedef uint16_t zmk_mouse_button_t;
#define ZMK_HID_MOUSE_NUM_BUTTONS 0x05
#define ZMK_HID_REPORT_ID_MOUSE 0x03
struct zmk_hid_mouse_report_body {
    zmk_mouse_button_flags_t buttons;
    int16_t d_x;
    int16_t d_y;
    int16_t d_scroll_y;
    int16_t d_scroll_x;
} __attribute__((packed));
struct zmk_hid_mouse_report {
    uint8_t report_id;
    struct zmk_hid_mouse_report_body body;
} __attribute__((packed));
int zmk_hid_mouse_button_press(zmk_mouse_button_t button);
int zmk_hid_mouse_button_release(zmk_mouse_button_t button);
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void);
