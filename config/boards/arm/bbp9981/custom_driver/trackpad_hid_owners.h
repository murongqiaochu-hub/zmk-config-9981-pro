/* Inject ONLY into pinned ZMK v0.3.0 hid.c, never into its callers.
 * Reference state, not GPIO/contact. Preserve the official ref/error semantics.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <errno.h>
#include <zephyr/irq.h>
#include <zmk/hid.h>
#include "menu_click_diag.h"
#if IS_ENABLED(CONFIG_ZMK_POINTING)
/* Tentative declaration of the SAME TU-local storage initialized in stock hid.c. */
static int explicit_button_counts[5];
static inline void bbp_menu_button_sample(bool down,int ret) {
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
    int refs=explicit_button_counts[0];
    int32_t packed=(int32_t)((uint32_t)zmk_hid_get_keyboard_report()->body.modifiers<<16);
    packed|=(int32_t)((uint32_t)refs&0xffffU);
    bbp_menu_diag_note(MD_HID,down,ret,zmk_hid_get_mouse_report()->body.buttons,packed);
#else
    (void)down;(void)ret;
#endif
}

int bbp9981_stock_zmk_hid_mouse_button_press(zmk_mouse_button_t button);
int bbp9981_stock_zmk_hid_mouse_button_release(zmk_mouse_button_t button);
int bbp9981_stock_zmk_hid_mouse_buttons_press(zmk_mouse_button_flags_t buttons);
int bbp9981_stock_zmk_hid_mouse_buttons_release(zmk_mouse_button_flags_t buttons);

/* Only the synchronous stock state mutation runs under irq_lock; no send/wait. */
int zmk_hid_mouse_button_press(zmk_mouse_button_t button) {
    unsigned int key = irq_lock();
    int ret = bbp9981_stock_zmk_hid_mouse_button_press(button);
    if (button==0U) { bbp_menu_button_sample(true,ret); }
    irq_unlock(key);
    return ret;
}

int zmk_hid_mouse_button_release(zmk_mouse_button_t button) {
    unsigned int key = irq_lock();
    int ret = bbp9981_stock_zmk_hid_mouse_button_release(button);
    if (button==0U) { bbp_menu_button_sample(false,ret); }
    irq_unlock(key);
    return ret;
}

int zmk_hid_mouse_buttons_press(zmk_mouse_button_flags_t buttons) {
    unsigned int key = irq_lock();
    int ret = bbp9981_stock_zmk_hid_mouse_buttons_press(buttons);
    if (buttons&1U) { bbp_menu_button_sample(true,ret); }
    irq_unlock(key);
    return ret;
}

int zmk_hid_mouse_buttons_release(zmk_mouse_button_flags_t buttons) {
    unsigned int key = irq_lock();
    int ret = bbp9981_stock_zmk_hid_mouse_buttons_release(buttons);
    if (buttons&1U) { bbp_menu_button_sample(false,ret); }
    irq_unlock(key);
    return ret;
}

int bbp9981_hid_mouse_button_references(unsigned int button) {
    if (button >= 5U) { return -EINVAL; }
    unsigned int key = irq_lock();
    int count = explicit_button_counts[button];
    irq_unlock(key);
    return count;
}

/* Applied AFTER the official declarations and public wrappers above. Stock
 * definitions and multi-button internal calls are renamed together: an external
 * multi-button wrapper takes ONE lock covering all its stock single calls.
 * Mouse clear/movement/scroll and keyboard APIs deliberately remain stock. */
#define zmk_hid_mouse_button_press bbp9981_stock_zmk_hid_mouse_button_press
#define zmk_hid_mouse_button_release bbp9981_stock_zmk_hid_mouse_button_release
#define zmk_hid_mouse_buttons_press bbp9981_stock_zmk_hid_mouse_buttons_press
#define zmk_hid_mouse_buttons_release bbp9981_stock_zmk_hid_mouse_buttons_release
#endif
