/* Used declaration from ZMK v0.3.0; implementation is a recording test stub. */
#pragma once
#include <zmk/hid.h>
#include <zmk/endpoints_types.h>
void zmk_hid_indicators_process_report(struct zmk_hid_led_report_body *report,
                                      struct zmk_endpoint_instance endpoint);
