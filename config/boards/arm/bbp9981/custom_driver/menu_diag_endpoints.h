/* SPDX-License-Identifier: MIT
 * Inject ONLY into pinned ZMK v0.3.0 app/src/endpoints.c.
 * Observe stock routing; NEVER select, clear, send or save on our own behalf.
 */
#pragma once
#include <zephyr/irq.h>
#include <zmk/endpoints.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include "menu_click_diag.h"

_Static_assert(ZMK_TRANSPORT_USB == 0 && ZMK_TRANSPORT_BLE == 1,
               "MDIAG v2 transport enum changed; review pinned endpoints_types.h");
/* These tentative declarations bind the SAME initialized TU-local objects in
 * stock endpoints.c, not copies in the diagnostic worker's translation unit. */
static enum zmk_transport preferred_transport;
static struct zmk_endpoint_instance current_instance;

struct md_endpoint_snapshot bbp_menu_diag_endpoints_snapshot(void) {
    struct md_endpoint_snapshot s = {.active_ble_index = -1};
    unsigned int key = irq_lock();
    s.selected = (int32_t)current_instance.transport;
    s.preferred = (int32_t)preferred_transport;
    irq_unlock(key);
    /* BLE may obtain/ref/unref a bt_conn. All SDK reads stay OUTSIDE our lock.
     * Readiness and index are a sampled observation, not an atomic radio claim. */
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (zmk_usb_is_hid_ready()) { s.ready_bits |= 1; }
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (zmk_ble_active_profile_is_connected()) { s.ready_bits |= 2; }
    s.active_ble_index = zmk_ble_active_profile_index();
#endif
    return s;
}

int bbp9981_stock_zmk_endpoints_select_transport(enum zmk_transport transport);
int bbp9981_stock_zmk_endpoints_toggle_transport(void);

int zmk_endpoints_select_transport(enum zmk_transport transport) {
    bbp_menu_diag_arm();
    /* phase=0 has no return yet: b=0 is only a placeholder, not success. */
    bbp_menu_diag_note(MD_ROUTE_REQUEST, transport, 0, 0, current_instance.transport);
    int ret = bbp9981_stock_zmk_endpoints_select_transport(transport);
    bbp_menu_diag_note(MD_ROUTE_REQUEST, transport, ret, 1, current_instance.transport);
    return ret;
}

int zmk_endpoints_toggle_transport(void) {
    enum zmk_transport request = preferred_transport == ZMK_TRANSPORT_USB
                                    ? ZMK_TRANSPORT_BLE : ZMK_TRANSPORT_USB;
    bbp_menu_diag_arm();
    bbp_menu_diag_note(MD_ROUTE_REQUEST, request, 0, 0, current_instance.transport);
    int ret = bbp9981_stock_zmk_endpoints_toggle_transport();
    bbp_menu_diag_note(MD_ROUTE_REQUEST, request, ret, 1, current_instance.transport);
    return ret;
}

/* Rename AFTER true public declarations and wrappers. Stock toggle's internal
 * select remains stock: exactly one invocation, no recursive/double observer.
 * All stock send/clear/update/settings code is left byte-for-byte unchanged. */
#define zmk_endpoints_select_transport bbp9981_stock_zmk_endpoints_select_transport
#define zmk_endpoints_toggle_transport bbp9981_stock_zmk_endpoints_toggle_transport
