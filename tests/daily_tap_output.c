/* SPDX-License-Identifier: MIT
 * Daily CURRENT bridge: the entire old mouse oracle remains frozen and runs
 * against the actual current production C included by test_tap_output.c.
 * The layer observation is an SDK mock, not a replacement output predicate.
 */
static int daily_layer;
static int zmk_keymap_highest_layer_active(void) { return daily_layer; }
#define main daily_frozen_bridge_oracle
#include "test_tap_output.c"
#undef main

int main(void) {
    assert(daily_frozen_bridge_oracle() == 0);
    for (unsigned usb = 0U; usb < 2U; ++usb) {
        cold_boot();
        mock_endpoint.transport = usb ? ZMK_TRANSPORT_USB : ZMK_TRANSPORT_BLE;
        daily_layer = 3;
        start_down(100U);
        assert(mock_report_count == 1U && mock_reports[0].body.buttons == 1U);
        run_worker_at(112U);
        assert(mock_report_count == 2U && mock_reports[1].body.buttons == 0U);
        assert_idle();

        cold_boot();
        mock_endpoint.transport = usb ? ZMK_TRANSPORT_USB : ZMK_TRANSPORT_BLE;
        daily_layer = 0;
        assert(tp_tap_output_submit() == 0);
        daily_layer = 3; /* Queued ordinary click must not disappear before DOWN. */
        run_worker_at(100U);
        assert(mock_report_count == 1U && tap_output.status.owns_left);
        run_worker_at(112U);
        assert(mock_report_count == 2U && mock_refs[0] == 0);
        assert_idle();
    }
    puts("PASS DAILY CURRENT bridge: USB/BLE MEDIA DOWN/12ms UP and queued layer-entry remain live; frozen necessary-UP/fault/foreign-owner oracle retained");
    return 0;
}
