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
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include "a320_0x57.h"
#include "trackpad_key_state.h"
#include "trackpad_pointer.h"
#include "aa_sym_ctrl.h"
#ifdef CONFIG_BBP9981_GAMEPAD
#include "gamepad_runtime.h"
/* Late ownership check also covers a layer change during an I2C read/replay.
 * This does not revoke a mouse packet already admitted by the input/USB SDK. */
#define a320_report_rel(dev, code, value, sync, timeout) \
    do { if (!bbp_gamepad_layer_active()) { \
        (void)input_report_rel(dev, code, value, sync, timeout); \
    } } while (0)
static uint32_t last_gamepad_pointer_epoch;
#else
#define a320_report_rel input_report_rel
#endif
#ifdef CONFIG_A320_TAP_OUTPUT
#include "trackpad_tap_pipeline.h"
#endif
#ifdef CONFIG_A320_MICROTAP
#include "trackpad_microtap_runtime.h"
extern void tp_microtap_shoulder_motion(uint32_t now);
#endif
#ifdef CONFIG_A320_CONTACT_PROBE
#include <zephyr/drivers/uart.h>
#include <zmk/usb.h>
#include <string.h>
#include "trackpad_contact_probe.h"
#ifdef CONFIG_A320_BUS_COMPARE
#include "trackpad_bus_probe.h"
#endif
#endif


LOG_MODULE_REGISTER(a320, CONFIG_A320_LOG_LEVEL);

static struct tp_key_state scroll_keys;
/* Coherent mode + transition epoch, also safe for future cross-thread readers. */
static atomic_t scroll_mode_snapshot;

/* === Configure Motion GPIO === */
#define MOTION_GPIO_NODE DT_NODELABEL(gpio0)
#define MOTION_GPIO_PIN 2
static const struct device *motion_gpio_dev;

/* Atomic publication is defensive; current non-split callbacks share one workqueue. */
static atomic_t touched;

static int16_t sum_dx = 0, sum_dy = 0;
static uint16_t sample_cnt = 0;
static uint32_t last_scroll_snapshot;
static uint32_t last_read_time = 0;
static struct tp_pointer_state cursor_motion;
static uint32_t last_cursor_speed_snapshot;

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
BUILD_ASSERT(CONFIG_A320_POLL_INTERVAL_MS > 0, "A320 poll interval must be positive");

static void a320_poll_work_handler(struct k_work *work);
static int a320_read_motion(const struct device *dev, int16_t *dx, int16_t *dy);

#ifdef CONFIG_A320_CONTACT_PROBE
static struct tp_probe_cache contact_probe;
static bool probe_open;
static uint32_t probe_at, probe_session;
static uint8_t legacy_bytes[7];
static uint32_t legacy_at, legacy_seq, legacy_errors;
#ifndef CONFIG_A320_BUS_COMPARE
static int probe_read_one(void *ctx, uint8_t reg, uint8_t *out) {
    /* Register-pointer/read transaction only; never writes configuration values. */
    return i2c_reg_read_byte_dt((const struct i2c_dt_spec *)ctx, reg, out);
}
#else
static uint32_t bus_compare_at, bus_compare_round;
static int compare_pointer_stop(void *ctx, uint8_t reg) {
    return i2c_write_dt((const struct i2c_dt_spec *)ctx, &reg, 1);
}
static int compare_read_only(void *ctx, uint8_t *out) {
    return i2c_read_dt((const struct i2c_dt_spec *)ctx, out, 1);
}
static int compare_combined(void *ctx, uint8_t reg, uint8_t *out) {
    return i2c_reg_read_byte_dt((const struct i2c_dt_spec *)ctx, reg, out);
}
static void compare_bus(const struct i2c_dt_spec *bus, uint32_t now) {
    if ((uint32_t)(now-bus_compare_at) < CONFIG_A320_BUS_COMPARE_INTERVAL_MS) { return; }
    bus_compare_at=now;
    const struct tp_bus_ops ops={compare_pointer_stop,compare_read_only,compare_combined};
    /* Rotate order to expose previous-transaction dependence. SAME pointer 00 only. */
    for (unsigned slot=0;slot<TP_BUS_MODE_COUNT;slot++) {
        enum tp_bus_mode mode=(enum tp_bus_mode)((bus_compare_round+slot)%TP_BUS_MODE_COUNT);
        struct tp_bus_observation r=tp_bus_observe(&ops,(void *)bus,mode,0x00);
        LOG_INF("TPB t=%u round=%u slot=%u mode=%s reg=00 value=%02x obtained=%d err=%d",
                now,bus_compare_round,slot,tp_bus_mode_name(mode),r.value,r.obtained,r.error);
    }
    bus_compare_round++;
}
#endif
static void probe_legacy(const uint8_t *bytes, size_t size) {
    if (!probe_open || size != sizeof(legacy_bytes)) { return; }
    memcpy(legacy_bytes, bytes, sizeof(legacy_bytes));
    legacy_at = k_uptime_get_32();
    legacy_seq++;
}
static void probe_poll(const struct i2c_dt_spec *bus, int motion_gpio) {
    const struct device *console = DEVICE_DT_GET(DT_NODELABEL(debug_cdc_acm_uart));
    uint32_t dtr = 0, now = k_uptime_get_32();
    if (!zmk_usb_is_hid_ready() || !device_is_ready(console) ||
        uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr) || !dtr) {
        if (probe_open) { tp_probe_reset(&contact_probe); }
        probe_open = false;
        return; /* No blocking wait and no extra sensor reads without the log port. */
    }
    if (!probe_open) {
        tp_probe_reset(&contact_probe);
        probe_open = true;probe_session++;
        legacy_seq = legacy_errors = 0;legacy_at = now;
        memset(legacy_bytes, 0, sizeof(legacy_bytes));
        probe_at = now - CONFIG_A320_CONTACT_PROBE_INTERVAL_MS;
#ifdef CONFIG_A320_BUS_COMPARE
        bus_compare_at=now-CONFIG_A320_BUS_COMPARE_INTERVAL_MS;
        bus_compare_round=0;
        LOG_INF("TPMODE compare=1 register=00 readonly=1 click=0 contact_decoded=0");
#endif
        LOG_INF("TPBEGIN session=%u readonly=1 click=0 interval_ms=%u", probe_session,
                (unsigned)CONFIG_A320_CONTACT_PROBE_INTERVAL_MS);
    }
    if ((uint32_t)(now - probe_at) < CONFIG_A320_CONTACT_PROBE_INTERVAL_MS) { return; }
    probe_at = now;
#ifdef CONFIG_A320_BUS_COMPARE
    compare_bus(bus,now);
#else
    struct tp_probe_sample f = tp_probe_sample(&contact_probe, now, probe_read_one, (void *)bus);
    LOG_INF("TPID t=%u pid=%02x rev=%02x match=%d err=%d", now, f.pid, f.revision, f.matched, f.error);
    LOG_INF("TPD t=%u valid=%d contact=%d engine=%02x cfg=%02x status=%02x quality=%u shutter=%u err=%d",
            now, f.valid, f.contact, f.engine, f.fpd_control, f.motion, f.quality, f.shutter, f.error);
#endif
    LOG_INF("TPL t=%u seq=%u age=%u errors=%u irq=%d b=%02x%02x%02x%02x%02x%02x%02x",
            now, legacy_seq, (uint32_t)(now-legacy_at), legacy_errors, motion_gpio,
            legacy_bytes[0],legacy_bytes[1],legacy_bytes[2],legacy_bytes[3],
            legacy_bytes[4],legacy_bytes[5],legacy_bytes[6]);
}
#else
#define probe_legacy(bytes, size) do { } while (0)
#endif


/* Bit 0 is physical Ctrl; upper bits record every speed transition.
 * Detect press+release between polls without clearing pointer state in the listener.
 */
static atomic_t cursor_speed_snapshot;
static bool sym_physically_pressed;
#define TP_SYM_POSITION 40

/* ==== position_state_changed event listener ==== */
static int ctrl_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->position == 37) {
        /* Keep original physical Ctrl slow-pointer behavior, separate from SYM Ctrl. */
        uint32_t speed = (uint32_t)atomic_get(&cursor_speed_snapshot);
        if (((speed & 1U) != 0) != ev->state) {
            uint32_t next = ((speed + 2U) & ~1U) | (ev->state ? 1U : 0U);
            atomic_set(&cursor_speed_snapshot, (int32_t)next);
        }
    }
    bool was_latched = scroll_keys.latched;
    tp_key_state_update(&scroll_keys, ev->position, ev->state, ev->timestamp);
    atomic_set(&scroll_mode_snapshot, scroll_keys.snapshot);
    if (was_latched != scroll_keys.latched) {
        LOG_INF("Scroll lock %s (aA double tap)", scroll_keys.latched ? "ON" : "OFF");
    }
    if (ev->position == TP_SYM_POSITION) { sym_physically_pressed = ev->state; }
    if (ev->position == TP_AA_POSITION || ev->position == TP_SYM_POSITION) {
        if (scroll_keys.pressed && sym_physically_pressed) {
            /* SYM-first is also a chord, never an aA double-tap scroll toggle. */
            scroll_keys.interrupted = true;
            scroll_keys.tap_pending = false;
            scroll_keys.double_candidate = false;
        }
        int err = tp_sym_ctrl_update(scroll_keys.pressed, sym_physically_pressed, ev->timestamp);
        if (err < 0) {
            LOG_ERR("aA/SYM Ctrl state update failed: %d", err);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(a320_ctrl_listener, ctrl_listener_cb);
ZMK_SUBSCRIPTION(a320_ctrl_listener, zmk_position_state_changed);

/* =========================
 *   Work handler (polling)
 * ========================= */

#ifdef CONFIG_A320_MICROTAP
struct micro_cursor_context { const struct device *dev; bool slow; };
static void micro_emit_cursor(void *opaque, int16_t dx, int16_t dy, uint32_t at_ms) {
    const struct micro_cursor_context *ctx = opaque;
    int16_t x, y;
    /* Only committed raw frames reach the ORIGINAL fractional pointer state.
     * A discarded gesture has never spent a fraction or moved the host cursor. */
    tp_pointer_motion(&cursor_motion, (int8_t)dx, (int8_t)dy, ctx->slow, at_ms, &x, &y);
    a320_report_rel(ctx->dev, INPUT_REL_X, x, false, K_FOREVER);
    a320_report_rel(ctx->dev, INPUT_REL_Y, y, true, K_FOREVER);
}
#endif
static void a320_poll_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct a320_data *data = CONTAINER_OF(dwork, struct a320_data, poll_work);
    const struct device *dev = data->dev;

    int pin_state = gpio_pin_get(motion_gpio_dev, MOTION_GPIO_PIN);
#ifdef CONFIG_A320_TAP_OUTPUT
    int32_t tap_dx=0,tap_dy=0;
    bool tap_motion_fault=pin_state<0;
#endif

    uint32_t snapshot = (uint32_t)atomic_get(&scroll_mode_snapshot);
    bool scroll_mode = (snapshot & TP_MODE_FLAGS) != 0;
    uint32_t speed = (uint32_t)atomic_get(&cursor_speed_snapshot);
    bool slow_pointer = (speed & 1U) != 0;

#ifdef CONFIG_BBP9981_GAMEPAD
    uint32_t game_epoch = bbp_gamepad_pointer_epoch();
    if (game_epoch != last_gamepad_pointer_epoch) {
        sum_dx = sum_dy = 0; sample_cnt = 0; last_read_time = 0;
        tp_pointer_reset(&cursor_motion);
        last_gamepad_pointer_epoch = game_epoch;
    }
    if (bbp_gamepad_layer_active()) {
        int16_t game_dx = 0, game_dy = 0;
        bool valid = pin_state >= 0;
        if (pin_state == 0) { valid = a320_read_motion(dev, &game_dx, &game_dy) == 0; }
        atomic_set(&touched, valid && pin_state == 0);
        bbp_gamepad_trackpad_poll(game_dx, game_dy, valid, k_uptime_get());
        /* Never spend old cursor fractions or wheel accumulation in a stick. */
        sum_dx = sum_dy = 0; sample_cnt = 0; last_read_time = 0;
        tp_pointer_reset(&cursor_motion);
        last_scroll_snapshot = snapshot; last_cursor_speed_snapshot = speed;
#ifdef CONFIG_A320_MICROTAP
        /* Reuse the old blocked/cancel path without changing any tap policy.
         * scroll=true here means no XY replay; it does NOT emit wheel data. */
        tp_microtap_runtime_interrupt();
        tp_microtap_runtime_poll(k_uptime_get_32(), 0, 0, false, true, snapshot, speed, NULL, NULL);
#elif defined(CONFIG_A320_TAP_OUTPUT)
        tp_tap_pipeline_poll(k_uptime_get_32(), 0, 0, true, true, snapshot);
#endif
#ifdef CONFIG_A320_CONTACT_PROBE
        const struct a320_dev_config *game_probe_cfg = data->dev->config;
        probe_poll(&game_probe_cfg->i2c, pin_state);
#endif
        k_work_reschedule(&data->poll_work, K_MSEC(CONFIG_A320_POLL_INTERVAL_MS));
        return;
    }
#endif

    /* Epoch also detects enter+exit entirely between two sensor polls. */
    if ((snapshot ^ last_scroll_snapshot) & TP_MODE_EPOCH_MASK) {
        sum_dx = 0;
        sum_dy = 0;
        sample_cnt = 0;
        last_read_time = 0;
        tp_pointer_reset(&cursor_motion);
    }
    if (speed != last_cursor_speed_snapshot) {
        tp_pointer_reset(&cursor_motion);
    }
    last_scroll_snapshot = snapshot;
    last_cursor_speed_snapshot = speed;
    tp_pointer_prepare(&cursor_motion, slow_pointer);
#ifndef CONFIG_A320_MICROTAP
    tp_pointer_expire(&cursor_motion, k_uptime_get_32());
#endif

    if (pin_state == 0) {
        int16_t dx = 0, dy = 0;

        if (a320_read_motion(dev, &dx, &dy) == 0) {
#ifdef CONFIG_A320_TAP_OUTPUT
            tap_dx=dx;tap_dy=dy; /* RAW, before gain/scroll changes. */
#endif
            atomic_set(&touched, 1);

            /* === Normal cursor movement while local scroll mode is off === */
            if (!scroll_mode) {
#ifdef CONFIG_A320_MICROTAP
                if (dx != 0 || dy != 0) {
                    tp_microtap_shoulder_motion(k_uptime_get_32());
                }
#else
                int16_t cursor_x, cursor_y;
                tp_pointer_motion(&cursor_motion, (int8_t)dx, (int8_t)dy,
                                  slow_pointer, k_uptime_get_32(), &cursor_x, &cursor_y);
                a320_report_rel(dev, INPUT_REL_X, cursor_x, false, K_FOREVER);
                a320_report_rel(dev, INPUT_REL_Y, cursor_y, true, K_FOREVER);
#endif
            }

            /* === Scroll mode while aA is held or latched on === */
            else {
                /* Keep the original scroll-side Ctrl truncation and curve unchanged. */
                if (slow_pointer) {
                    dx /= 2;
                    dy /= 2;
                }

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

                /* Preserve the current 40 ms window; never allow a zero threshold. */
                uint16_t threshold = MAX(1, 40 / CONFIG_A320_POLL_INTERVAL_MS);

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

                    a320_report_rel(dev, INPUT_REL_HWHEEL, -scroll_x, false, K_FOREVER);
                    a320_report_rel(dev, INPUT_REL_WHEEL, scroll_y, true, K_FOREVER);
                }
            }
        } else {
#ifdef CONFIG_A320_TAP_OUTPUT
            tap_motion_fault=true;
#endif
            /* Do not leave touch/scroll residue latched after a sensor read failure. */
            atomic_set(&touched, 0);
            sum_dx = sum_dy = 0;
            sample_cnt = 0;
            last_read_time = 0;
            tp_pointer_reset(&cursor_motion);
        }
    } else {
        atomic_set(&touched, 0);
        if (pin_state < 0) {
            tp_pointer_reset(&cursor_motion);
        }
    }

#ifdef CONFIG_A320_CONTACT_PROBE
    const struct a320_dev_config *probe_cfg = data->dev->config;
    probe_poll(&probe_cfg->i2c, pin_state);
#endif
#ifdef CONFIG_A320_MICROTAP
    struct micro_cursor_context cursor_ctx = {.dev = dev, .slow = slow_pointer};
    tp_microtap_runtime_poll(k_uptime_get_32(), (int16_t)tap_dx, (int16_t)tap_dy,
                            !tap_motion_fault, scroll_mode, snapshot, speed,
                            micro_emit_cursor, &cursor_ctx);
    if (!scroll_mode && pin_state == 0 && !tap_motion_fault && tap_dx == 0 && tap_dy == 0 &&
        !tp_microtap_runtime_enabled() && !tp_microtap_runtime_pending()) {
        /* Preserve the stock successful zero-frame report while the feature is OFF. */
        micro_emit_cursor(&cursor_ctx, 0, 0, k_uptime_get_32());
    }
    if (!tp_microtap_runtime_pending()) {
        tp_pointer_expire(&cursor_motion, k_uptime_get_32());
    }
#elif defined(CONFIG_A320_TAP_OUTPUT)
    tp_tap_pipeline_poll(k_uptime_get_32(),tap_dx,tap_dy,tap_motion_fault,scroll_mode,snapshot);
#endif
    k_work_reschedule(&data->poll_work, K_MSEC(CONFIG_A320_POLL_INTERVAL_MS));
}

/* =========================
 *   I2C read sequence
 * ========================= */
static int a320_read_motion(const struct device *dev, int16_t *dx, int16_t *dy) {
    const struct a320_dev_config *cfg = dev->config;
    uint8_t buf[7] = {0};
    uint8_t reg = 0x0A;
    int ret;

    ret = i2c_write_dt(&cfg->i2c, &reg, 1);
    if (ret < 0) {
#ifdef CONFIG_A320_CONTACT_PROBE
        if (probe_open) { legacy_errors++; }
#endif
        LOG_ERR("i2c write 0x0A failed: %d", ret);
        return ret;
    }

    ret = i2c_burst_read_dt(&cfg->i2c, 0x0A, buf, sizeof(buf));
    if (ret < 0) {
#ifdef CONFIG_A320_CONTACT_PROBE
        if (probe_open) { legacy_errors++; }
#endif
        LOG_ERR("i2c burst read from 0x0A failed: %d", ret);
        return ret;
    }

    probe_legacy(buf, sizeof(buf));
    *dy = (int8_t)buf[1];
    *dx = (int8_t)buf[3];
    return 0;
}

bool tp_is_touched(void) { return atomic_get(&touched) != 0; }
bool tp_scroll_mode_active(void) {
    return ((uint32_t)atomic_get(&scroll_mode_snapshot) & TP_MODE_FLAGS) != 0;
}
bool tp_aa_is_pressed(void) {
    return ((uint32_t)atomic_get(&scroll_mode_snapshot) & TP_MODE_PRESSED) != 0;
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
    int err = gpio_pin_configure(motion_gpio_dev, MOTION_GPIO_PIN, GPIO_INPUT | GPIO_PULL_UP);
    if (err < 0) {
        LOG_ERR("Motion GPIO configuration failed: %d", err);
        return err;
    }

    data->dev = dev;
#ifdef CONFIG_A320_MICROTAP
    tp_microtap_runtime_init();
#elif defined(CONFIG_A320_TAP_OUTPUT)
    tp_tap_pipeline_init();
#endif

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
