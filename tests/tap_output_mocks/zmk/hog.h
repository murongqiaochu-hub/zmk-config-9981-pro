/* Actual public v0.3.0 HOG API. Production source passes a zero-motion COPY. */
#pragma once
#include <zmk/hid.h>
int zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *body);
