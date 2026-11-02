/* SPDX-License-Identifier: MIT
 * Add only the UUID comparison API absent from the frozen 16-bit host mock.
 * This is not a radio/GATT implementation; original macros stay unchanged.
 */
#pragma once
#include "../../../gamepad_ble_service_mocks/zephyr/bluetooth/gatt.h"
static inline int bt_uuid_cmp(const struct bt_uuid *a, const struct bt_uuid *b) {
    assert(a && b);
    return (a->val > b->val) - (a->val < b->val);
}
