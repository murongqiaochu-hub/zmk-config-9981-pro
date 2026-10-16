/* Host-only Zephyr boundary. This is a mock, not the nRF GPIO driver.
 * SDK matrix/getter/debounce source + two public headers remain original bytes.
 * No devicetree instance expansion, scheduler threads, hardware or extra scan.
 */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#define CONFIG_ZMK_LOG_LEVEL 0
#define CONFIG_ZMK_KSCAN_MATRIX_POLLING 0
#define CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS 10
#define CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS 10
#define CONFIG_ZMK_KSCAN_MATRIX_WAIT_BEFORE_INPUTS 0
#define CONFIG_ZMK_KSCAN_MATRIX_WAIT_BETWEEN_OUTPUTS 0
#define CONFIG_PM_DEVICE 0
#define CONFIG_PM_DEVICE_RUNTIME 0
#define IS_ENABLED(x) (x)
#define DT_INST_FOREACH_STATUS_OKAY(fn) /* intentionally no DT instances */
#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
#define __ASSERT(test, ...) assert(test)
#define BIT(n) (UINT32_C(1) << (n))
#define BIT_MASK(n) (BIT(n) - 1U)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define CONTAINER_OF(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))

struct device { const char *name; void *data; const void *config; };
typedef uint32_t gpio_flags_t;
typedef uint32_t gpio_port_pins_t;
typedef uint32_t gpio_port_value_t;
/* Distinct mock bits; no claim that these configure real silicon. */
#define GPIO_INPUT BIT(8)
#define GPIO_OUTPUT BIT(9)
#define GPIO_DISCONNECTED BIT(10)
#define GPIO_ACTIVE_HIGH 0U
#define GPIO_ACTIVE_LOW BIT(0)
#define GPIO_PULL_DOWN BIT(5)
#define GPIO_OPEN_SOURCE (BIT(1) | BIT(2))
#define GPIO_INT_LEVEL_ACTIVE BIT(16)
#define GPIO_INT_DISABLE 0U
struct gpio_dt_spec { const struct device *port; uint8_t pin; gpio_flags_t dt_flags; };
struct gpio_callback;
typedef void (*gpio_callback_handler_t)(const struct device *, struct gpio_callback *, gpio_port_pins_t);
struct gpio_callback { gpio_callback_handler_t handler; gpio_port_pins_t pin_mask; };
int gpio_pin_set_dt(const struct gpio_dt_spec *, int);
int gpio_port_get(const struct device *, gpio_port_value_t *);
int gpio_pin_configure_dt(const struct gpio_dt_spec *, gpio_flags_t);
int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *, gpio_flags_t);
int gpio_add_callback(const struct device *, struct gpio_callback *);
static inline bool device_is_ready(const struct device *dev) { return dev != NULL; }
static inline void gpio_init_callback(struct gpio_callback *cb, gpio_callback_handler_t handler,
                                      gpio_port_pins_t mask) { cb->handler = handler; cb->pin_mask = mask; }

typedef void (*kscan_callback_t)(const struct device *, uint32_t, uint32_t, bool);
struct kscan_driver_api {
    int (*config)(const struct device *, kscan_callback_t);
    int (*enable_callback)(const struct device *);
    int (*disable_callback)(const struct device *);
};
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable { struct k_work work; bool armed; int64_t deadline; unsigned schedules; };
#define K_NO_WAIT INT64_C(0)
#define K_TIMEOUT_ABS_MS(ms) (ms)
int64_t k_uptime_get(void);
static inline void k_work_init_delayable(struct k_work_delayable *w, void (*fn)(struct k_work *)) {
    w->work.handler = fn; w->armed = false; w->schedules = 0;
}
static inline int k_work_reschedule(struct k_work_delayable *w, int64_t due) {
    w->armed = true; w->deadline = due; w->schedules++; return 0;
}
static inline int k_work_cancel_delayable(struct k_work_delayable *w) { w->armed = false; return 0; }
static inline struct k_work_delayable *k_work_delayable_from_work(struct k_work *w) {
    return CONTAINER_OF(w, struct k_work_delayable, work);
}
