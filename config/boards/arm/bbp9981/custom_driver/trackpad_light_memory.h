/* SPDX-License-Identifier: MIT
 * Independent TP user-limit record, not an animation/effective-output record.
 * ZMK v0.3.0 calc_brt(0) reads state.brightness even when state.on is false.
 * All native h_set callbacks finish before our h_commit restores the limit.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zmk/backlight.h>

#if IS_ENABLED(CONFIG_SETTINGS)
static struct k_work_delayable tp_mem_work;
static bool tp_mem_initialized, tp_mem_driver_ready;
static bool tp_mem_loaded, tp_mem_ready, tp_mem_dirty;
static uint8_t tp_mem_value, tp_mem_observed, tp_mem_failures;
/* Implemented by the driver; called only after its works are initialized. */
static void tp_mem_start(uint8_t value);

static void tp_mem_save(struct k_work *work) {
    (void)work;
    if (!tp_mem_initialized || !tp_mem_ready || !tp_mem_dirty || tp_mem_failures >= 3U) {
        return;
    }
    uint8_t value = tp_mem_value;
    int ret = settings_save_one("bbp9981/tp/brightness", &value, sizeof(value));
    if (ret >= 0) {
        tp_mem_failures = 0;
        tp_mem_dirty = (tp_mem_value != value);
        if (tp_mem_dirty) { k_work_reschedule(&tp_mem_work, K_MSEC(250)); }
    } else {
        tp_mem_failures++;
        LOG_ERR("Trackpad brightness save failed (%u/3): %d", (unsigned)tp_mem_failures, ret);
        if (tp_mem_failures < 3U) { k_work_reschedule(&tp_mem_work, K_MSEC(1000)); }
    }
}

static void tp_mem_init(void) {
    k_work_init_delayable(&tp_mem_work, tp_mem_save);
    tp_mem_initialized = true;
    tp_mem_driver_ready = false;
}

static int tp_mem_set(const char *name, size_t len, settings_read_cb cb, void *arg) {
    const char *next;
    if (!settings_name_steq(name, "brightness", &next) || next) { return -ENOENT; }
    if (len != sizeof(uint8_t)) { return -EINVAL; }
    uint8_t value;
    ssize_t n = cb(arg, &value, sizeof(value));
    if (n < 0) { return (int)n; }
    if (n != (ssize_t)sizeof(value) || value > 100U) { return -EINVAL; }
    tp_mem_value = value;
    tp_mem_loaded = true;
    return 0;
}

static int tp_mem_commit(void) {
    /* A failed PWM init must never start uninitialized LED works or native API. */
    if (!tp_mem_initialized || !tp_mem_driver_ready) { return -ENODEV; }
    if (tp_mem_ready) { return 0; }
    uint8_t native_value = zmk_backlight_calc_brt(0);
    if (tp_mem_loaded) {
        if (native_value != tp_mem_value) {
            bool was_on = zmk_backlight_is_on();
            int ret = zmk_backlight_set_brt(tp_mem_value);
            if (ret < 0) { return ret; }
            /* Restore the user limit without turning a native/manual OFF into ON. */
            if (!was_on && tp_mem_value > 0U) {
                ret = zmk_backlight_off();
                if (ret < 0) { return ret; }
            }
        }
    } else {
        tp_mem_value = native_value > 100U ? 100U : native_value;
        tp_mem_dirty = true; /* First boot migrates the already-loaded native state. */
    }
    tp_mem_observed = tp_mem_value;
    tp_mem_ready = true;
    tp_mem_start(tp_mem_value);
    if (tp_mem_dirty) { k_work_reschedule(&tp_mem_work, K_MSEC(250)); }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(bbp9981_tp_light, "bbp9981/tp", NULL,
                               tp_mem_set, tp_mem_commit, NULL);

static void tp_mem_observe(void) {
    if (!tp_mem_ready) { return; }
    /* Unlike get_brt(), this is not zeroed by idle/USB/manual OFF. User-set 0 is real. */
    uint8_t value = zmk_backlight_calc_brt(0);
    if (value > 100U) { value = 100U; }
    if (value == tp_mem_observed) { return; }
    tp_mem_observed = tp_mem_value = value;
    tp_mem_dirty = true;
    tp_mem_failures = 0;
    k_work_reschedule(&tp_mem_work, K_MSEC(250));
}
#endif
