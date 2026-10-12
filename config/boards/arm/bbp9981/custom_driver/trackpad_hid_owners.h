/* Inject ONLY into pinned ZMK v0.3.0 hid.c. Reference state, not GPIO/contact.
 * Existing official press/release paths remain unchanged.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <errno.h>
#include <zephyr/irq.h>
#include <zmk/hid.h>
#if IS_ENABLED(CONFIG_ZMK_POINTING)
static int explicit_button_counts[5]; /* Exact pinned SDK storage declaration. */
int bbp9981_hid_mouse_button_references(unsigned int button) {
    if (button>=5U) { return -EINVAL; }
    unsigned int key=irq_lock();
    int count=explicit_button_counts[button];
    irq_unlock(key);
    return count;
}
#endif
