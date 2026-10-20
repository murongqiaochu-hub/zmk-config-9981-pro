/* SPDX-License-Identifier: MIT -- host-only smooth-scroll feature mock. */
#pragma once
#include <zmk/endpoints_types.h>
#include <zmk/hid.h>
struct zmk_pointing_resolution_multipliers { uint8_t wheel, hor_wheel; };
struct zmk_pointing_resolution_multipliers
zmk_pointing_resolution_multipliers_get_profile(struct zmk_endpoint_instance endpoint);
void zmk_pointing_resolution_multipliers_process_report(
    struct zmk_hid_mouse_resolution_feature_report_body *report,
    struct zmk_endpoint_instance endpoint);
