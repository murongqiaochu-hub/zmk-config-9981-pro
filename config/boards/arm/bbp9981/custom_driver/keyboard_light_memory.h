/* SPDX-License-Identifier: MIT
 * Independent keyboard brightness record. Never save animation/idle output.
 * Initialized before settings_load(); h_commit restores after all h_set calls.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zmk/rgb_underglow.h>

static struct k_work_delayable kb_mem_work;
static bool kb_mem_loaded, kb_mem_ready, kb_mem_dirty;
static uint8_t kb_mem_value, kb_mem_observed, kb_mem_failures;
static void kb_mem_save(struct k_work *work) {
    (void)work;
    if (!kb_mem_ready || !kb_mem_dirty) { return; }
    uint8_t value = kb_mem_value;
    int ret = settings_save_one("bbp9981/kb/brightness", &value, sizeof(value));
    if (ret >= 0) {
        kb_mem_dirty = false;
        kb_mem_failures = 0;
    } else {
        kb_mem_failures++;
        LOG_ERR("Keyboard brightness save failed (%u/3): %d", (unsigned)kb_mem_failures, ret);
        if (kb_mem_failures < 3U) { k_work_reschedule(&kb_mem_work, K_MSEC(1000)); }
    }
}
static void kb_mem_init(void) {
    k_work_init_delayable(&kb_mem_work, kb_mem_save);
}
static int kb_mem_set(const char *name, size_t len, settings_read_cb cb, void *arg) {
    const char *next;
    if (!settings_name_steq(name, "brightness", &next) || next) { return -ENOENT; }
    if (len != sizeof(uint8_t)) { return -EINVAL; }
    uint8_t value;
    ssize_t n = cb(arg, &value, sizeof(value));
    if (n < 0) { return (int)n; }
    if (n != (ssize_t)sizeof(value) || value > 100U) { return -EINVAL; }
    kb_mem_value = value;
    kb_mem_loaded = true;
    return 0;
}
static int kb_mem_commit(void) {
    struct zmk_led_hsb color = zmk_rgb_underglow_calc_brt(0);
    if (kb_mem_loaded) {
        color.b = kb_mem_value;
        int ret = zmk_rgb_underglow_set_hsb(color);
        if (ret < 0) { return ret; }
    } else {
        kb_mem_value = color.b > 100U ? 100U : color.b;
        kb_mem_dirty = true;
    }
    kb_mem_observed = kb_mem_value;
    kb_mem_ready = true;
    if (kb_mem_dirty) { k_work_reschedule(&kb_mem_work, K_MSEC(250)); }
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(bbp9981_kb_light, "bbp9981/kb", NULL,
                               kb_mem_set, kb_mem_commit, NULL);
static void kb_mem_observe(void) {
    if (!kb_mem_ready) { return; } /* Never overwrite a stored value before load. */
    uint8_t value = zmk_rgb_underglow_calc_brt(0).b;
    if (value > 100U) { value = 100U; }
    if (value == kb_mem_observed) { return; }
    kb_mem_observed = kb_mem_value = value;
    kb_mem_dirty = true;
    kb_mem_failures = 0;
    k_work_reschedule(&kb_mem_work, K_MSEC(250));
}
