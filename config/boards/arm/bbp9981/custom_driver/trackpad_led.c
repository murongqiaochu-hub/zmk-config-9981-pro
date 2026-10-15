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

#include <zmk/endpoints.h>
#include <zmk/backlight.h>
#include <zmk/activity.h>
#include "indicator_brightness.h"
#include "trackpad_led.h"
#include "a320_0x57.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_CHOSEN(zmk_trackpad_led),
             "CONFIG_ZMK_TRACKPAD_LED enabled but no zmk,trackpad_led chosen node found");

#define CHILD_COUNT(...) +1
#define DT_NUM_CHILD(node_id) (DT_FOREACH_CHILD(node_id, CHILD_COUNT))
#define INDICATOR_LED_NUM_LEDS (DT_NUM_CHILD(DT_CHOSEN(zmk_trackpad_led)))
#define TRACKPAD_PWM_SPEC(node_id) PWM_DT_SPEC_GET(node_id),
static const struct pwm_dt_spec trackpad_pwms[] = {
    DT_FOREACH_CHILD(DT_CHOSEN(zmk_trackpad_led), TRACKPAD_PWM_SPEC)
};
static uint32_t last_pwm_pulse_ns[INDICATOR_LED_NUM_LEDS];
static bool pwm_cache_valid;

#define BRT_MIN 10
#define BRT_MAX 100
#define BRT_LOW 20
#define BRT_STEP 5

#define ANIMATION_INTERVAL_MS 20
#define POLLING_INTERVAL_MS 5
#define AUTO_OFF_DELAY_MS 5000

/* Same full period and peak as the former 100 ms ON / 900 ms OFF flash.
 * Smooth breathing changes the average brightness, not the user's peak limit. */
#define USB_BREATH_PERIOD_MS 1000U
#define USB_BREATH_INTERVAL_MS 20

static struct k_work_delayable polling_work;
static struct k_work_delayable animation_work;
static struct k_work_delayable auto_off_work;
static struct k_work_delayable usb_breath_work;

static bool scroll_mode_on = false;
static bool touch_active = false;
static bool animation_increasing = true;
static uint8_t brightness = BRT_MIN;

/* User limit cache: also survives the upstream keyboard-idle auto-off. */
static uint8_t last_valid_brt;
static uint8_t requested_pattern;
static int last_led_level = -1;
static uint8_t last_backlight_brt = 0;
static bool manual_override = false;
static bool keyboard_active = false;

static uint32_t usb_breath_last_ms;
static uint32_t usb_breath_phase_ms;
static bool usb_mode = false;

static void set_led_brightness(uint8_t pattern) {
    requested_pattern = pattern;
    uint8_t level = indicator_pattern_level(pattern, last_valid_brt);
    uint32_t pulses[INDICATOR_LED_NUM_LEDS];
    bool changed = !pwm_cache_valid;
    const uint32_t bounded_pattern = pattern > 100U ? 100U : pattern;
    const uint32_t limit = indicator_user_limit(last_valid_brt);
    for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) {
        /* Keep sub-percent precision through to the hardware. The old LED API
         * rounded this to integer percent first: at limit=1, only OFF/ON remain.
         * BLE's existing integer levels are preserved; only USB envelope gains
         * fractional duty. No low-frequency software dithering or higher cap. */
        uint64_t scaled = (uint64_t)trackpad_pwms[i].period *
                          (usb_mode ? bounded_pattern * limit : (uint32_t)level * 100U);
        pulses[i] = (uint32_t)(scaled / 10000U);
        if (last_pwm_pulse_ns[i] != pulses[i]) { changed = true; }
    }
    if (!changed) { return; }
    bool all_written = true;
    for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) {
        int err = pwm_is_ready_dt(&trackpad_pwms[i]) ?
                  pwm_set_dt(&trackpad_pwms[i], trackpad_pwms[i].period, pulses[i]) : -ENODEV;
        if (err < 0) {
            all_written = false;
            LOG_ERR("Failed to set trackpad PWM[%d]: %d", i, err);
        }
    }
    pwm_cache_valid = all_written;
    if (all_written) {
        for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) { last_pwm_pulse_ns[i] = pulses[i]; }
    }
    last_led_level = all_written ? level : -1;
}

static uint8_t usb_breathing_pattern(uint32_t phase_ms) {
    const uint32_t half_period = USB_BREATH_PERIOD_MS / 2U;
    /* 100 * (3*t*t - 2*t*t*t), t in [0, 1], folded into each half.
     * For the fixed 500 ms ramp all intermediates fit in uint32_t. */
    phase_ms %= USB_BREATH_PERIOD_MS;
    uint32_t ramp = phase_ms <= half_period ? phase_ms : USB_BREATH_PERIOD_MS - phase_ms;
    const uint32_t divisor = half_period * half_period * half_period / BRT_MAX;
    return (uint8_t)((ramp * ramp * (3U * half_period - 2U * ramp) + divisor / 2U) /
                     divisor);
}

static void usb_breath_work_handler(struct k_work *work) {
    /* Also guard the transport before the next 5 ms poll observes a switch. */
    if (!usb_mode || zmk_endpoints_selected().transport != ZMK_TRANSPORT_USB) {
        return;
    }

    uint32_t now = k_uptime_get_32();
    uint32_t elapsed = now - usb_breath_last_ms; /* Unsigned uptime rollover is intentional. */
    usb_breath_last_ms = now;
    usb_breath_phase_ms = (usb_breath_phase_ms + elapsed % USB_BREATH_PERIOD_MS) %
                          USB_BREATH_PERIOD_MS;
    set_led_brightness(usb_breathing_pattern(usb_breath_phase_ms));
    k_work_reschedule(&usb_breath_work, K_MSEC(USB_BREATH_INTERVAL_MS));
}

static void auto_off_work_handler(struct k_work *work) {
    if (!usb_mode && !scroll_mode_on && !touch_active) {
        manual_override = false;
        set_led_brightness(0);
        LOG_DBG("Auto-off triggered after inactivity");
    }
}

static void animation_work_handler(struct k_work *work) {
    if (usb_mode || !scroll_mode_on)
        return;

    if (animation_increasing) {
        brightness += BRT_STEP;
        if (brightness >= BRT_MAX) {
            brightness = BRT_MAX;
            animation_increasing = false;
        }
    } else {
        brightness -= BRT_STEP;
        if (brightness <= BRT_LOW) {
            brightness = BRT_LOW;
            animation_increasing = true;
        }
    }

    set_led_brightness(brightness);
    k_work_reschedule(&animation_work, K_MSEC(ANIMATION_INTERVAL_MS));
}

static void polling_work_handler(struct k_work *work) {
    enum zmk_transport transport = zmk_endpoints_selected().transport;
    bool current_scroll_mode = tp_scroll_mode_active();
    bool current_touch = tp_is_touched();
    bool current_active = (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    uint8_t current_brt = zmk_backlight_get_brt();
    /* Physical V/N presses wake activity, so explicit zero is observed here.
     * During idle, upstream returns zero automatically; retain the last user limit. */
    if (current_active || current_brt > 0) { last_valid_brt = current_brt; }

    /* ---------------- USB Mode ---------------- */
    if (transport == ZMK_TRANSPORT_USB) {
        if (!usb_mode) {
            /* USB breathing takes precedence over the local scroll animation. */
            usb_mode = true;
            scroll_mode_on = false;
            k_work_cancel_delayable(&animation_work);
            k_work_cancel_delayable(&auto_off_work);
            usb_breath_last_ms = k_uptime_get_32();
            usb_breath_phase_ms = 0;
            set_led_brightness(0); /* Never replay the previous mode's peak on entry. */
            k_work_reschedule(&usb_breath_work, K_MSEC(USB_BREATH_INTERVAL_MS));
            LOG_INF("Entered USB breathing mode");
        } else {
            set_led_brightness(requested_pattern); /* Live user limit and write retry. */
        }
        k_work_reschedule(&polling_work, K_MSEC(POLLING_INTERVAL_MS));
        return;
    }

    /* ---------------- BLE Output Mode ---------------- */
    if (usb_mode) {
        usb_mode = false;
        k_work_cancel_delayable(&usb_breath_work);
        scroll_mode_on = false;
        touch_active = false;
        manual_override = false;
        set_led_brightness(0);
        LOG_INF("Exited USB breathing mode");
    } else {
        set_led_brightness(requested_pattern);
    }

    if (current_active != keyboard_active) {
        keyboard_active = current_active;
        if (keyboard_active) {
            last_backlight_brt = current_brt;
        }
    }

    /* Local scroll mode */
    if (current_scroll_mode != scroll_mode_on) {
        scroll_mode_on = current_scroll_mode;
        if (scroll_mode_on) {
            brightness = BRT_MIN;
            animation_increasing = true;
            k_work_reschedule(&animation_work, K_NO_WAIT);
        } else {
            k_work_cancel_delayable(&animation_work);
            manual_override = false;

            if (current_touch) {
                touch_active = true;
                manual_override = true;
                set_led_brightness(100);
                k_work_cancel_delayable(&auto_off_work);
            } else {
                set_led_brightness(0);
            }
        }
    }

    /* touched */
    if (!scroll_mode_on && current_touch != touch_active) {
        touch_active = current_touch;
        if (touch_active) {
            manual_override = true;
            set_led_brightness(100);
            k_work_cancel_delayable(&auto_off_work);
        } else {
            k_work_reschedule(&auto_off_work, K_MSEC(AUTO_OFF_DELAY_MS));
        }
    }

    if (!scroll_mode_on && !touch_active && current_brt != last_backlight_brt && keyboard_active) {
        last_backlight_brt = current_brt;
        manual_override = current_brt > 0;
        set_led_brightness(current_brt > 0 ? 100 : 0);
        k_work_reschedule(&auto_off_work, K_MSEC(AUTO_OFF_DELAY_MS));
    }

    k_work_reschedule(&polling_work, K_MSEC(POLLING_INTERVAL_MS));
}

uint8_t indicator_tp_get_last_valid_brightness(void) { return last_valid_brt; }

static int indicator_tp_init(void) {
    for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) {
        if (!pwm_is_ready_dt(&trackpad_pwms[i])) {
            LOG_ERR("Trackpad PWM device not ready");
            return -ENODEV;
        }
    }

    last_valid_brt = zmk_backlight_get_brt();
    last_led_level = -1;
    pwm_cache_valid = false;
    usb_mode = false;
    set_led_brightness(0);
    usb_breath_last_ms = k_uptime_get_32();
    usb_breath_phase_ms = 0;
    last_backlight_brt = zmk_backlight_get_brt();
    scroll_mode_on = touch_active = manual_override = keyboard_active = false;

    k_work_init_delayable(&polling_work, polling_work_handler);
    k_work_init_delayable(&animation_work, animation_work_handler);
    k_work_init_delayable(&auto_off_work, auto_off_work_handler);
    k_work_init_delayable(&usb_breath_work, usb_breath_work_handler);

    k_work_reschedule(&polling_work, K_NO_WAIT);
    return 0;
}

SYS_INIT(indicator_tp_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
