/* Host-only mocks for compiling the actual firmware sources. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#define CONFIG_ZMK_BEHAVIOR_METADATA 1
#define CONFIG_ZMK_RGB_UNDERGLOW 1
#define CONFIG_LED_PWM 1
#define CONFIG_A320_POLL_INTERVAL_MS 10
#define CONFIG_A320_LOG_LEVEL 0
#define CONFIG_ZMK_LOG_LEVEL 0
#define CONFIG_INPUT_A320_INIT_PRIORITY 90
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define IS_ENABLED(x) (x)
#define ZMK_BEHAVIOR_OPAQUE 0
#define ZMK_EV_EVENT_BUBBLE 0
#define DT_HAS_COMPAT_STATUS_OKAY(x) 1
#define DT_HAS_CHOSEN(x) 1
#define DT_CHOSEN(x) 0
#define DT_NODELABEL(x) 0
#define DT_FOREACH_CHILD(x, fn) fn(0)
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DT_DRV_INST(n) 0
#define DT_PROP_OR(n, p, d) (d)
#define BUILD_ASSERT(x, ...) _Static_assert((x), "build assertion")
#define POST_KERNEL 0
#define APPLICATION 0
#define SYS_INIT(fn, ...) static void *const ref_##fn __attribute__((unused)) = (void *)&fn
#define ZMK_LISTENER(n, fn)
#define ZMK_SUBSCRIPTION(n, event)
#define LOG_MODULE_DECLARE(...)
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
#define LOG_INF(...)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CONTAINER_OF(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
#define K_FOREVER (-1)
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
#define GPIO_INPUT 1
#define GPIO_PULL_UP 2
#define INPUT_REL_X 1
#define INPUT_REL_Y 2
#define INPUT_REL_HWHEEL 3
#define INPUT_REL_WHEEL 4
#define LCTRL 0x0700E0u

struct device { const char *name; const void *config; void *data; };
static struct device fake_device = {.name = "fake"};
#define DEVICE_DT_GET(x) (&fake_device)
#define I2C_DT_SPEC_INST_GET(n) {.bus = &fake_device}
#define GPIO_DT_SPEC_INST_GET_OR(...) {0}
#define DEVICE_DT_INST_DEFINE(n, init, pm, data, config, level, prio, api) \
    static void *const ref_device_##n __attribute__((unused)) = (void *)&init
#define BEHAVIOR_DT_INST_DEFINE(n, init, pm, data, config, level, prio, api) \
    static const struct behavior_driver_api *const ref_behavior_##n = api

struct k_work { int marker; };
struct k_work_delayable { struct k_work work; bool pending; unsigned schedules; int delay; };
static int k_work_reschedule(struct k_work_delayable *w, int d) {
    w->pending = true; w->schedules++; w->delay = d; return 0;
}
static int k_work_schedule(struct k_work_delayable *w, int d) { return k_work_reschedule(w,d); }
static int k_work_cancel_delayable(struct k_work_delayable *w) { w->pending = false; return 0; }
static void k_work_init_delayable(struct k_work_delayable *w, void (*fn)(struct k_work *)) {
    (void)fn; memset(w,0,sizeof(*w));
}
static uint32_t fake_now;
static uint32_t k_uptime_get_32(void) { return fake_now; }
typedef int32_t atomic_t;
static int32_t atomic_get(const atomic_t *v) { return *v; }
static int32_t atomic_set(atomic_t *v,int32_t n) { int32_t old=*v; *v=n; return old; }
static bool device_is_ready(const struct device *d) { return d != NULL; }

struct i2c_dt_spec { const struct device *bus; };
struct gpio_dt_spec { int pin; };
static int fake_gpio_state = 1, fake_gpio_config_error, fake_i2c_error;
static int8_t fake_dx, fake_dy;
static int gpio_pin_get(const struct device *d,int pin) { (void)d;(void)pin;return fake_gpio_state; }
static int gpio_pin_configure(const struct device *d,int pin,int flags) {
    (void)d;(void)pin;(void)flags;return fake_gpio_config_error;
}
static int i2c_write_dt(const struct i2c_dt_spec *s,const void *b,size_t n) {
    (void)s;(void)b;(void)n;return fake_i2c_error;
}
static int i2c_burst_read_dt(const struct i2c_dt_spec *s,int a,uint8_t *b,size_t n) {
    (void)s;(void)a;memset(b,0,n);b[1]=(uint8_t)fake_dy;b[3]=(uint8_t)fake_dx;return fake_i2c_error;
}
static int modifier_refs;
static unsigned report_count;
static int report_values[4];
static int report_modifier_snapshot[4];
static int input_report_rel(const struct device *d,int code,int value,bool sync,int timeout) {
    (void)d;(void)sync;(void)timeout;report_count++;report_values[code-1]=value;
    report_modifier_snapshot[code-1]=modifier_refs;return 0;
}
static int fake_led_level;
static unsigned fake_led_writes;
static int fake_led_error;
static int led_set_brightness(const struct device *d,int index,uint8_t value) {
    (void)d;(void)index;fake_led_writes++;
    if(fake_led_error)return fake_led_error;
    fake_led_level=value;return 0;
}

struct zmk_behavior_binding { const char *behavior_dev; uint32_t param1,param2; };
struct zmk_behavior_binding_event { int layer; uint32_t position; int64_t timestamp; };
struct behavior_parameter_metadata { int dummy; };
static int zmk_behavior_get_empty_param_metadata(const struct device *d,struct behavior_parameter_metadata *m) {
    (void)d;(void)m;return 0;
}
struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *,struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *,struct zmk_behavior_binding_event);
    int (*get_parameter_metadata)(const struct device *,struct behavior_parameter_metadata *);
};
static int key_down_calls, key_up_calls, key_event_error;
static int64_t last_key_timestamp;
static int raise_zmk_keycode_state_changed_from_encoded(uint32_t code,bool state,int64_t t) {
    assert(code==LCTRL);last_key_timestamp=t;
    if(key_event_error)return key_event_error;
    if(state){modifier_refs++;key_down_calls++;}else{assert(modifier_refs>0);modifier_refs--;key_up_calls++;}
    return 0;
}
static int fake_layer, layer_to_calls;
static int zmk_keymap_layer_to(int layer) { fake_layer=layer;layer_to_calls++;return 0; }
static int zmk_keymap_highest_layer_active(void) { return fake_layer; }
struct zmk_position_state_changed { uint32_t position; bool state; int64_t timestamp; };
typedef struct { struct zmk_position_state_changed data; bool valid; } zmk_event_t;
static const struct zmk_position_state_changed *as_zmk_position_state_changed(const zmk_event_t *e) {
    return e->valid?&e->data:NULL;
}
enum zmk_activity_state { ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE };
static enum zmk_activity_state fake_activity=ZMK_ACTIVITY_ACTIVE;
static enum zmk_activity_state zmk_activity_get_state(void) { return fake_activity; }
struct zmk_led_hsb { uint8_t h,s,b; };
static uint8_t fake_brightness=40;
static bool fake_rgb_on=true;
static struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int i) { (void)i;return (struct zmk_led_hsb){.b=fake_brightness}; }
static int zmk_rgb_underglow_get_state(bool *on) { *on=fake_rgb_on;return 0; }
static uint8_t zmk_backlight_get_brt(void) { return fake_brightness; }
enum zmk_transport { ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE };
static enum zmk_transport fake_transport=ZMK_TRANSPORT_BLE;
struct zmk_endpoint_instance { enum zmk_transport transport; };
static struct zmk_endpoint_instance zmk_endpoints_selected(void) {
    return (struct zmk_endpoint_instance){.transport=fake_transport};
}
