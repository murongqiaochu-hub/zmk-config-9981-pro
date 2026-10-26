/* SPDX-License-Identifier: MIT
 * Compile the UNMODIFIED pinned stock hog.c with the staged candidate prefix.
 * Link GC drops its deliberately unreachable legacy implementation. Public
 * APIs must fail closed without needing a kernel, connection or notification.
 */
#include <assert.h>
#include <stdio.h>
#define BBP9981_PURE_BLE_CANDIDATE 1
#ifndef CONFIG_ZMK_POINTING
#define CONFIG_ZMK_POINTING 1
#endif
#ifndef CONFIG_ZMK_POINTING_SMOOTH_SCROLLING
#define CONFIG_ZMK_POINTING_SMOOTH_SCROLLING 0
#endif
#ifndef CONFIG_ZMK_HID_INDICATORS
#define CONFIG_ZMK_HID_INDICATORS 1
#endif
#define CONFIG_ZMK_BLE_THREAD_STACK_SIZE 512
#define CONFIG_ZMK_BLE_THREAD_PRIORITY 5
#define CONFIG_ZMK_BLE_INIT_PRIORITY 50
#define CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_LOG_LEVEL 0
#include "../experiments/gamepad_protocol/pure_hog_guard.h"
#include "gamepad_ble_mocks/sdk_v0_3_0_hog.c"
#undef zmk_hog_send_keyboard_report
#undef zmk_hog_send_consumer_report
#undef zmk_hog_send_mouse_report

_Static_assert(ARRAY_SIZE(attr_hog_svc) >= 15U, "complete stock table still compiles");
int main(void) {
    struct zmk_hid_keyboard_report_body keyboard={0};
    struct zmk_hid_consumer_report_body consumer={0};
    assert(zmk_hog_send_keyboard_report(&keyboard)==-ENOTSUP);
    assert(zmk_hog_send_consumer_report(&consumer)==-ENOTSUP);
#if IS_ENABLED(CONFIG_ZMK_POINTING)
    struct zmk_hid_mouse_report_body mouse={.buttons=1,.d_x=123};
    assert(zmk_hog_send_mouse_report(&mouse)==-ENOTSUP && mouse.d_x==123);
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&mouse,0)==-ENOTSUP);
    struct bbp9981_hog_mouse_stats stats;
    assert(bbp9981_ble_hid_mouse_report_stats_get(&stats)==-ENOTSUP);
    assert(!stats.outstanding && !stats.accepted && stats.last_send_error==-ENOTSUP);
    assert(bbp9981_ble_hid_mouse_report_stats_get(NULL)==-EINVAL);
#endif
    puts("PASS staged stock HOG guard: unchanged SDK TU compiles; public BLE keyboard/consumer/mouse fail closed without SDK calls; USB untouched");
    return 0;
}
