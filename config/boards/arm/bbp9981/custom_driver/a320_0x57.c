/*
 * A320 optical sensor driver (polling via motion GPIO, Zephyr input subsystem)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT avago_a320

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <stdlib.h>
#include <string.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include "a320_0x57.h"
#include "trackpad_led.h"

LOG_MODULE_REGISTER(a320, CONFIG_A320_LOG_LEVEL);

/* Stock Pro matrix: the bottom-right aA key is physical position 41. */
#define A320_SCROLL_MODE_POSITION 41
#define A320_AA_TAP_TERM_MS 450
static atomic_t scroll_mode_pressed;
static atomic_t scroll_mode_latched;
static uint32_t scroll_mode_press_time;

/* === Configure Motion GPIO === */
#define MOTION_GPIO_NODE DT_NODELABEL(gpio0)
#define MOTION_GPIO_PIN 2
static const struct device *motion_gpio_dev;

/* ==== Touch status flag ==== */
static bool touched = false;

static int16_t sum_dx = 0, sum_dy = 0;
static uint8_t sample_cnt = 0;
static bool last_scroll_mode = false;
static uint32_t last_read_time = 0;

/* =========================
 *   Data & Config structs
 * ========================= */
struct a320_dev_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec motion_gpio; /* Motion pin: active-low */
    uint16_t x_input_code;
    uint16_t y_input_code;
};

struct a320_data {
    const struct device *dev;
    struct k_work_delayable poll_work;
};

#ifndef CONFIG_A320_POLL_INTERVAL_MS
#define CONFIG_A320_POLL_INTERVAL_MS 10
#endif

enum a320_read_phase {
    A320_READ_PHASE_NONE = 0,
    A320_READ_PHASE_REGISTER_WRITE = 1,
    A320_READ_PHASE_BURST_READ = 2,
};

#if defined(CONFIG_A320_DIAGNOSTIC_CAPTURE) && CONFIG_A320_DIAGNOSTIC_CAPTURE
static uint32_t a320_diag_read_sequence;
static uint32_t a320_diag_last_state_ms;
static int a320_diag_last_gpio;
static bool a320_diag_gpio_seen;
#endif

static void a320_poll_work_handler(struct k_work *work);
static int a320_read_motion(const struct device *dev, int16_t *dx, int16_t *dy,
                            uint8_t raw[7], uint8_t *failed_phase);

static bool ctrl_pressed = false;

/* ==== position_state_changed event listener ==== */
static int ctrl_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) {
        return 0;
    }

    if (ev->position == 37) {
        ctrl_pressed = ev->state;
        LOG_INF("Ctrl position=37 %s", ctrl_pressed ? "PRESSED" : "RELEASED");
    }
    if (ev->position == A320_SCROLL_MODE_POSITION) {
        if (ev->state) {
            if (atomic_get(&scroll_mode_pressed) == 0) {
                scroll_mode_press_time = k_uptime_get_32();
            }
            atomic_set(&scroll_mode_pressed, 1);
            LOG_DBG("Local scroll key PRESSED");
        } else if (atomic_get(&scroll_mode_pressed) != 0) {
            uint32_t held_ms = k_uptime_get_32() - scroll_mode_press_time;
            atomic_set(&scroll_mode_pressed, 0);

            if (held_ms < A320_AA_TAP_TERM_MS) {
                atomic_val_t next_latched = atomic_get(&scroll_mode_latched) ? 0 : 1;
                atomic_set(&scroll_mode_latched, next_latched);
                LOG_INF("Persistent scroll %s", next_latched ? "ON" : "OFF");
            } else {
                LOG_DBG("Long aA hold ended without changing persistent scroll");
            }
        }
    }
    return 0;
}
ZMK_LISTENER(a320_ctrl_listener, ctrl_listener_cb);
ZMK_SUBSCRIPTION(a320_ctrl_listener, zmk_position_state_changed);

/* =========================
 *   Work handler (polling)
 * ========================= */

static void a320_poll_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct a320_data *data = CONTAINER_OF(dwork, struct a320_data, poll_work);
    const struct device *dev = data->dev;

    int pin_state = gpio_pin_get(motion_gpio_dev, MOTION_GPIO_PIN);

    bool scroll_mode = tp_scroll_mode_active();
#if defined(CONFIG_A320_DIAGNOSTIC_CAPTURE) && CONFIG_A320_DIAGNOSTIC_CAPTURE
    uint32_t diag_now_ms = k_uptime_get_32();
    if (!a320_diag_gpio_seen || pin_state != a320_diag_last_gpio ||
        (uint32_t)(diag_now_ms - a320_diag_last_state_ms) >= 1000U) {
        int gpio_err = pin_state < 0 ? pin_state : 0;
        LOG_INF("DIAG gpio uptime_ms=%u gpio=%d gpio_err=%d raw=none scroll=%u last_seq=%u",
                (unsigned int)diag_now_ms, pin_state, gpio_err, scroll_mode ? 1U : 0U,
                (unsigned int)a320_diag_read_sequence);
        a320_diag_gpio_seen = true;
        a320_diag_last_gpio = pin_state;
        a320_diag_last_state_ms = diag_now_ms;
    }
#endif

    /* Only this poll worker owns the accumulated motion state. */
    if (scroll_mode != last_scroll_mode) {
        sum_dx = 0;
        sum_dy = 0;
        sample_cnt = 0;
        last_read_time = 0;
        last_scroll_mode = scroll_mode;
    }

    if (pin_state == 0) {
        int16_t dx = 0, dy = 0;
        uint8_t raw[7] = {0};
        uint8_t failed_phase = A320_READ_PHASE_NONE;
#if defined(CONFIG_A320_DIAGNOSTIC_CAPTURE) && CONFIG_A320_DIAGNOSTIC_CAPTURE
        uint32_t read_seq = ++a320_diag_read_sequence;
        uint32_t read_ms = k_uptime_get_32();
        uint32_t read_start_cycles = k_cycle_get_32();
#endif
        int read_ret = a320_read_motion(dev, &dx, &dy, raw, &failed_phase);
#if defined(CONFIG_A320_DIAGNOSTIC_CAPTURE) && CONFIG_A320_DIAGNOSTIC_CAPTURE
        uint32_t read_elapsed_us =
            k_cyc_to_us_floor32(k_cycle_get_32() - read_start_cycles);
#endif

        if (read_ret == 0) {
#if defined(CONFIG_A320_DIAGNOSTIC_CAPTURE) && CONFIG_A320_DIAGNOSTIC_CAPTURE
            LOG_INF("DIAG sample seq=%u uptime_ms=%u gpio=%d gpio_err=0 read=ok read_elapsed_us=%u raw=%02x,%02x,%02x,%02x,%02x,%02x,%02x dx=%d dy=%d scroll=%u",
                    (unsigned int)read_seq, (unsigned int)read_ms, pin_state,
                    (unsigned int)read_elapsed_us,
                    (unsigned int)raw[0], (unsigned int)raw[1], (unsigned int)raw[2],
                    (unsigned int)raw[3], (unsigned int)raw[4], (unsigned int)raw[5],
                    (unsigned int)raw[6], (int)dx, (int)dy, scroll_mode ? 1U : 0U);
#endif
            touched = true;

            if (ctrl_pressed) {
                dx /= 2;
                dy /= 2;
            }

            /* === Normal cursor movement while local scroll mode is off === */
            if (!scroll_mode) {
                uint8_t tp_led_brt = indicator_tp_get_last_valid_brightness();
                float tp_factor = 0.4f + 0.01f * tp_led_brt;

                dx = dx * 3 / 2 * tp_factor;
                dy = dy * 3 / 2 * tp_factor;

                input_report_rel(dev, INPUT_REL_X, dx, false, K_FOREVER);
                input_report_rel(dev, INPUT_REL_Y, dy, true, K_FOREVER);
            }

            /* === Scroll mode while aA is held or latched on === */
            else {

                uint32_t now = k_uptime_get_32();

                /* --- Condition 1: dx=0 && dy=0 → clear accumulated scroll, avoid direction residue
                 * --- */
                if (dx == 0 && dy == 0) {
                    sum_dx = 0;
                    sum_dy = 0;
                    sample_cnt = 0;
                }

                /* --- Condition 2: time since last read > 50ms → treat as new swipe, clear
                   accumulation --- */
                else if (now - last_read_time > 50) {
                    sum_dx = 0;
                    sum_dy = 0;
                    sample_cnt = 0;
                }

                /* Update timestamp (must be after successful data read) */
                last_read_time = now;

                /* Normal accumulation logic */
                sum_dx += dx;
                sum_dy += dy;
                sample_cnt++;

                uint8_t threshold = 40 / CONFIG_A320_POLL_INTERVAL_MS;

                if (sample_cnt >= threshold) {
                    int16_t sx = sum_dx;
                    int16_t sy = sum_dy;

                    sum_dx = 0;
                    sum_dy = 0;
                    sample_cnt = 0;

                    int16_t scroll_x = 0, scroll_y = 0;

                    if (abs(sy) >= 128) {
                        scroll_x = -sx / 24;
                        scroll_y = -sy / 24;
                    } else if (abs(sy) >= 64) {
                        scroll_x = -sx / 16;
                        scroll_y = -sy / 16;
                    } else if (abs(sy) >= 32) {
                        scroll_x = -sx / 12;
                        scroll_y = -sy / 12;
                    } else if (abs(sy) >= 21) {
                        scroll_x = -sx / 8;
                        scroll_y = -sy / 8;
                    } else if (abs(sy) >= 3) {
                        scroll_x = (sx > 0) ? -1 : (sx < 0) ? 1 : 0;
                        scroll_y = (sy > 0) ? -1 : (sy < 0) ? 1 : 0;
                    } else {
                        scroll_x = (sx > 0) ? -1 : (sx < 0) ? 1 : 0;
                        scroll_y = 0;
                    }

                    input_report_rel(dev, INPUT_REL_HWHEEL, -scroll_x, false, K_FOREVER);
                    input_report_rel(dev, INPUT_REL_WHEEL, scroll_y, true, K_FOREVER);
                }
            }
        } else {
#if defined(CONFIG_A320_DIAGNOSTIC_CAPTURE) && CONFIG_A320_DIAGNOSTIC_CAPTURE
            LOG_WRN("DIAG sample seq=%u uptime_ms=%u gpio=%d gpio_err=0 read=error phase=%u rc=%d read_elapsed_us=%u raw=none scroll=%u",
                    (unsigned int)read_seq, (unsigned int)read_ms, pin_state,
                    (unsigned int)failed_phase, read_ret, (unsigned int)read_elapsed_us,
                    scroll_mode ? 1U : 0U);
#endif
        }
    } else {
        touched = false;
    }

    k_work_reschedule(&data->poll_work, K_MSEC(CONFIG_A320_POLL_INTERVAL_MS));
}

/* =========================
 *   I2C read sequence
 * ========================= */
static int a320_read_motion(const struct device *dev, int16_t *dx, int16_t *dy,
                            uint8_t raw[7], uint8_t *failed_phase) {
    const struct a320_dev_config *cfg = dev->config;
    uint8_t buf[7] = {0};
    uint8_t reg = 0x0A;
    int ret;

    *failed_phase = A320_READ_PHASE_REGISTER_WRITE;
    ret = i2c_write_dt(&cfg->i2c, &reg, 1);
    if (ret < 0) {
#if !defined(CONFIG_A320_DIAGNOSTIC_CAPTURE) || !CONFIG_A320_DIAGNOSTIC_CAPTURE
        LOG_ERR("i2c write 0x0A failed: %d", ret);
#endif
        return ret;
    }

    *failed_phase = A320_READ_PHASE_BURST_READ;
    ret = i2c_burst_read_dt(&cfg->i2c, 0x0A, buf, sizeof(buf));
    if (ret < 0) {
#if !defined(CONFIG_A320_DIAGNOSTIC_CAPTURE) || !CONFIG_A320_DIAGNOSTIC_CAPTURE
        LOG_ERR("i2c burst read from 0x0A failed: %d", ret);
#endif
        return ret;
    }

    memcpy(raw, buf, sizeof(buf));
    *failed_phase = A320_READ_PHASE_NONE;
    *dy = (int8_t)buf[1];
    *dx = (int8_t)buf[3];
    return 0;
}

bool tp_is_touched(void) { return touched; }
bool tp_scroll_mode_active(void) {
    return atomic_get(&scroll_mode_pressed) != 0 || atomic_get(&scroll_mode_latched) != 0;
}

/* =========================
 *   Device init
 * ========================= */
static int a320_init(const struct device *dev) {
    const struct a320_dev_config *cfg = dev->config;
    struct a320_data *data = dev->data;

    LOG_INF("A320 init: %s", dev->name);

    if (!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    motion_gpio_dev = DEVICE_DT_GET(MOTION_GPIO_NODE);
    if (!device_is_ready(motion_gpio_dev)) {
        LOG_ERR("Motion GPIO device not ready");
        return -ENODEV;
    }
    gpio_pin_configure(motion_gpio_dev, MOTION_GPIO_PIN, GPIO_INPUT | GPIO_PULL_UP);

    data->dev = dev;

    k_work_init_delayable(&data->poll_work, a320_poll_work_handler);
    k_work_schedule(&data->poll_work, K_MSEC(CONFIG_A320_POLL_INTERVAL_MS));

    return 0;
}

#define A320_INIT_PRIORITY CONFIG_INPUT_A320_INIT_PRIORITY

#define A320_DEFINE(inst)                                                                          \
    static struct a320_data a320_data_##inst;                                                      \
    static const struct a320_dev_config a320_config_##inst = {                                     \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                                         \
        .motion_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, motion_gpios, {0}),                          \
        .x_input_code = DT_PROP_OR(DT_DRV_INST(inst), x_input_code, INPUT_REL_X),                  \
        .y_input_code = DT_PROP_OR(DT_DRV_INST(inst), y_input_code, INPUT_REL_Y),                  \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(inst, a320_init, NULL, &a320_data_##inst, &a320_config_##inst,           \
                          POST_KERNEL, A320_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(A320_DEFINE)
