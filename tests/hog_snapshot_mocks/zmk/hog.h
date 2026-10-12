/* SPDX-License-Identifier: MIT -- SDK declaration precedes wrapper rename */
#ifndef HOG_SNAPSHOT_MOCK_HOG_H
#define HOG_SNAPSHOT_MOCK_HOG_H
#include <zmk/hid.h>
#if IS_ENABLED(CONFIG_ZMK_POINTING)
int zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *body);
#endif
#endif
