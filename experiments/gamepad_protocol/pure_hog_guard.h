/* SPDX-License-Identifier: MIT
 * Force-include ONLY into pinned ZMK hog.c for the explicit staged candidate.
 * Keep the original TU and API ABI, but do not register its HIDS and do not
 * enqueue keyboard/consumer/mouse input. USB remains outside this adapter.
 */
#pragma once
#ifndef BBP9981_PURE_BLE_CANDIDATE
#error "pure_hog_guard requires the explicit isolated candidate build"
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/hog.h>
#define BBP9981_HOG_SNAPSHOT_API_ONLY
#include "../../config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h"
#undef BBP9981_HOG_SNAPSHOT_API_ONLY

int zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report_body *body) {
    (void)body; return -ENOTSUP;
}
int zmk_hog_send_consumer_report(struct zmk_hid_consumer_report_body *body) {
    (void)body; return -ENOTSUP;
}
#if IS_ENABLED(CONFIG_ZMK_POINTING)
int zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *body) {
    (void)body; return -ENOTSUP;
}
int bbp9981_ble_hid_send_mouse_buttons_snapshot(struct zmk_hid_mouse_report_body *body,
                                              int expected_profile) {
    (void)body; (void)expected_profile; return -ENOTSUP;
}
int bbp9981_ble_hid_mouse_report_stats_get(struct bbp9981_hog_mouse_stats *out) {
    if (!out) { return -EINVAL; }
    memset(out, 0, sizeof(*out));
    out->last_enqueue_error = out->last_send_error = -ENOTSUP;
    return -ENOTSUP;
}
#endif

/* Define a normal object, NOT STRUCT_SECTION_ITERABLE: the stock attribute
 * table remains a compilation fixture, never enters the registered GATT list.
 * Do not apply this macro override to the separate pure_runtime.c TU.
 */
#undef BT_GATT_SERVICE_DEFINE
#define BT_GATT_SERVICE_DEFINE(_name, ...) \
    const struct bt_gatt_attr attr_##_name[] = { __VA_ARGS__ }; \
    const struct bt_gatt_service_static _name = BT_GATT_SERVICE(attr_##_name)

/* Official declarations and our fail-closed public entrypoints are already
 * above. Only the subsequent unused stock implementations receive new names.
 */
#define zmk_hog_send_keyboard_report bbp_exp_unregistered_hog_keyboard
#define zmk_hog_send_consumer_report bbp_exp_unregistered_hog_consumer
#if IS_ENABLED(CONFIG_ZMK_POINTING)
#define zmk_hog_send_mouse_report bbp_exp_unregistered_hog_mouse
#endif
