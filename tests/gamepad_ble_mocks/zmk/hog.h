/* SPDX-License-Identifier: MIT -- pinned public sender ABI. */
#pragma once
#include <zmk/hid.h>
int zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report_body *body);
int zmk_hog_send_consumer_report(struct zmk_hid_consumer_report_body *body);
int zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *body);
