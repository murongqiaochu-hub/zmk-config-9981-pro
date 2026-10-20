/* SPDX-License-Identifier: MIT -- host-only LED report endpoint mock. */
#pragma once
#include <zmk/endpoints_types.h>
#include <zmk/hid.h>
void zmk_hid_indicators_process_report(struct zmk_hid_led_report_body *report,
                                       struct zmk_endpoint_instance endpoint);
