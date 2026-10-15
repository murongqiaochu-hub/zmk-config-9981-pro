/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "indicator_brightness.h"
#include <zmk/rgb_underglow.h>
#include <zmk/event_manager.h>
#include <zmk/activity.h>
#include <zmk/keymap.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_CHOSEN(zmk_keyboard_backlight),
             "keyboard_backlight: No zmk_keyboard_backlight chosen node found");
BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW) && IS_ENABLED(CONFIG_LED_PWM),
             "9981 Pro indicators require RGB state controls and LED_PWM");

#include "keyboard_light_memory.h"

#define CHILD_COUNT(...) +1
#define DT_NUM_CHILD(node_id) (DT_FOREACH_CHILD(node_id, CHILD_COUNT))
#define INDICATOR_LED_NUM_LEDS (DT_NUM_CHILD(DT_CHOSEN(zmk_keyboard_backlight)))

#define BRT_MAX 90
#define BRT_BLINK_HIGH 100
#define BRT_BLINK_LOW 10
#define BLINK_INTERVAL_MS 500

#define CYCLE_BRT_MIN 10
#define CYCLE_BRT_MAX 100
#define CYCLE_BRT_STEP 5
#define CYCLE_INTERVAL_MS 20

static bool prev_active = false;
static int prev_layer = -1;
static bool blink_on = false;
static bool blink_start_high = true;
static uint8_t cycle_brightness = CYCLE_BRT_MIN;
static bool cycle_direction_up = true;

static struct k_work_delayable polling_work;
static struct k_work_delayable blink_work;
static struct k_work_delayable cycle_work;

#define KB_PWM_SPEC(node_id) PWM_DT_SPEC_GET(node_id),
static const struct pwm_dt_spec kb_pwms[] = {
    DT_FOREACH_CHILD(DT_CHOSEN(zmk_keyboard_backlight), KB_PWM_SPEC)
};
static int last_led_level = -1;
static uint32_t kb_last_pulses[INDICATOR_LED_NUM_LEDS];
static bool kb_pwm_valid;
static void kb_set_fraction(uint32_t fraction) {
    if (fraction > 10000U) { fraction = 10000U; }
    uint32_t pulses[INDICATOR_LED_NUM_LEDS];
    bool changed = !kb_pwm_valid;
    for (int i=0; i<INDICATOR_LED_NUM_LEDS; i++) {
        pulses[i] = (uint32_t)((uint64_t)kb_pwms[i].period * fraction / 10000U);
        if (pulses[i] != kb_last_pulses[i]) { changed = true; }
    }
    if (!changed) { return; }
    bool ok = true;
    for (int i=0; i<INDICATOR_LED_NUM_LEDS; i++) {
        int ret = pwm_is_ready_dt(&kb_pwms[i]) ?
            pwm_set_dt(&kb_pwms[i],kb_pwms[i].period,pulses[i]) : -ENODEV;
        if (ret < 0) { ok=false;LOG_ERR("Keyboard PWM[%d] failed: %d",i,ret); }
    }
    kb_pwm_valid=ok;
    if (ok) {
        for (int i=0; i<INDICATOR_LED_NUM_LEDS; i++) { kb_last_pulses[i]=pulses[i]; }
    }
    last_led_level=ok?(int)((fraction+50U)/100U):-1;
}
static void set_led_brightness(uint8_t level) {
    kb_set_fraction((uint32_t)(level>100U?100U:level)*100U);
}

static uint8_t requested_pattern;
static void set_mode_brightness(uint8_t pattern) {
    requested_pattern = pattern;
    bool on = false;
    if (zmk_rgb_underglow_get_state(&on) < 0) { on = false; }
    uint8_t setting = on ? zmk_rgb_underglow_calc_brt(0).b : 0;
    kb_set_fraction((uint32_t)(pattern>100U?100U:pattern)*indicator_user_limit(setting));
}

/* 层1/层3闪烁 */
static void blink_work_handler(struct k_work *work) {
    if (!prev_active || (prev_layer != 1 && prev_layer != 3)) {
        return;
    }

    blink_on = !blink_on;
    set_mode_brightness(blink_on ? BRT_BLINK_HIGH : BRT_BLINK_LOW);

    uint32_t interval = (prev_layer == 3) ? (BLINK_INTERVAL_MS / 2) : BLINK_INTERVAL_MS;
    k_work_reschedule(&blink_work, K_MSEC(interval));
}

static void cycle_work_handler(struct k_work *work) {
    if (!prev_active || prev_layer != 2) {
        return;
    }

    set_mode_brightness(cycle_brightness);

    if (cycle_direction_up) {
        cycle_brightness += CYCLE_BRT_STEP;
        if (cycle_brightness >= CYCLE_BRT_MAX) {
            cycle_brightness = CYCLE_BRT_MAX;
            cycle_direction_up = false;
        }
    } else {
        if (cycle_brightness <= CYCLE_BRT_MIN + CYCLE_BRT_STEP) {
            cycle_brightness = CYCLE_BRT_MIN;
            cycle_direction_up = true;
        } else {
            cycle_brightness -= CYCLE_BRT_STEP;
        }
    }
    k_work_reschedule(&cycle_work, K_MSEC(CYCLE_INTERVAL_MS));
}

static bool backlight_allowed = false;

static int key_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->state) {
        backlight_allowed = true;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(kb_backlight_key_listener, key_listener_cb);
ZMK_SUBSCRIPTION(kb_backlight_key_listener, zmk_position_state_changed);

static void polling_work_handler(struct k_work *work) {
    kb_mem_observe();
    bool active = (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    int current_layer = zmk_keymap_highest_layer_active();
    bool rgb_on = true;
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    if (zmk_rgb_underglow_get_state(&rgb_on) < 0) {
        rgb_on = true;
    }
#endif

    /* Idle must stop all layer animations, not just the default-layer light. */
    if (!active) {
        backlight_allowed = false;
        prev_active = false;
        prev_layer = current_layer;
        k_work_cancel_delayable(&blink_work);
        k_work_cancel_delayable(&cycle_work);
        set_mode_brightness(0);
        k_work_reschedule(&polling_work, K_MSEC(100));
        return;
    }

    if (current_layer != prev_layer || active != prev_active) {
        prev_layer = current_layer;
        prev_active = active;

        k_work_cancel_delayable(&blink_work);
        k_work_cancel_delayable(&cycle_work);
        blink_on = false;
        cycle_brightness = CYCLE_BRT_MIN;
        cycle_direction_up = true;

        switch (current_layer) {
        case 0:
            /* Only turn on if active AND a key was pressed to authorize it */
            set_mode_brightness(backlight_allowed ? 100 : 0);
            break;

        case 1:
            blink_start_high = !rgb_on ? true : false;
            blink_on = blink_start_high;
            /* Allow blink if active */
            set_mode_brightness(blink_on ? BRT_BLINK_HIGH : BRT_BLINK_LOW);

            k_work_reschedule(&blink_work, K_MSEC(BLINK_INTERVAL_MS / 2));
            break;

        case 2:
            k_work_reschedule(&cycle_work, K_MSEC(100));
            break;

        case 3:
            blink_on = false;
            set_mode_brightness(BRT_BLINK_LOW);
            k_work_reschedule(&blink_work, K_MSEC(BLINK_INTERVAL_MS / 2));
            break;

        default:
            set_mode_brightness(0);
            break;
        }
    }

    /* Reapply the current pattern in EVERY layer when C/M/B changes the setting. */
    if (current_layer == 0) {
        set_mode_brightness(backlight_allowed ? 100 : 0);
    } else {
        set_mode_brightness(requested_pattern);
    }

    k_work_reschedule(&polling_work, K_MSEC(100));
}

static int keyboardbacklight_init(void) {
    /* Settings h_commit may still run if the LED device fails initialization. */
    kb_mem_init();
    for (int i=0;i<INDICATOR_LED_NUM_LEDS;i++) {
        if (!pwm_is_ready_dt(&kb_pwms[i])) { return -ENODEV; }
    }
    kb_pwm_valid=false;
    prev_active = (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    prev_layer = -1;

    k_work_init_delayable(&polling_work, polling_work_handler);
    k_work_init_delayable(&blink_work, blink_work_handler);
    k_work_init_delayable(&cycle_work, cycle_work_handler);

    k_work_reschedule(&polling_work, K_MSEC(100));
    return 0;
}

SYS_INIT(keyboardbacklight_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
