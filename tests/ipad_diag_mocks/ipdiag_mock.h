#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_BT_MAX_CONN 2
#define CONFIG_BBP9981_IPAD_DIAG 1
#ifndef CONFIG_LOG_MODE_DEFERRED
#define CONFIG_LOG_MODE_DEFERRED 1
#endif
#define IPDIAG_CONFIG_REV "0123456789012345678901234567890123456789"
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define LOG_LEVEL_INF 3
#define LOG_MODULE_REGISTER(...)
void test_log(const char *, ...);
#define LOG_INF(...) test_log(__VA_ARGS__)
#define K_MSEC(x) (x)
#define APPLICATION 0
#define SYS_INIT(fn,level,priority) static void *used_init __attribute__((unused)) = (void *)&fn
struct k_work { int ignored; };
struct k_work_delayable { void (*fn)(struct k_work *); };
extern uint32_t test_now;
static inline uint32_t k_uptime_get_32(void) { return test_now; }
static inline unsigned irq_lock(void) { return 0; }
static inline void irq_unlock(unsigned x) { (void)x; }
static inline void k_work_init_delayable(struct k_work_delayable *w, void (*f)(struct k_work *)) { w->fn=f; }
static inline int k_work_reschedule(struct k_work_delayable *w, int ms) { (void)w;(void)ms;return 0; }
struct device { int dummy; };
extern struct device test_console;
#define DT_CHOSEN(x) 0
#define DEVICE_DT_GET(x) (&test_console)
#define UART_LINE_CTRL_DTR 1
extern bool test_dtr;
static inline bool device_is_ready(const struct device *d) { (void)d;return true; }
static inline bool zmk_usb_is_hid_ready(void) { return true; }
static inline int uart_line_ctrl_get(const struct device *d, uint32_t c, uint32_t *v) { (void)d;(void)c;*v=test_dtr;return 0; }
struct bt_uuid { int id; };
extern const struct bt_uuid uuid_report,uuid_ref;
#define BT_UUID_HIDS_REPORT (&uuid_report)
#define BT_UUID_HIDS_REPORT_REF (&uuid_ref)
#define BT_GATT_CCC_NOTIFY 1
static inline int bt_uuid_cmp(const struct bt_uuid *a,const struct bt_uuid *b) { return a->id-b->id; }
struct bt_gatt_attr { const struct bt_uuid *uuid; uint16_t handle; };
struct bt_gatt_service_static { const struct bt_gatt_attr *attrs; size_t attr_count; };
struct bt_conn { unsigned index; bool connected; unsigned security, mtu, mask; };
struct bt_gatt_notify_params { const struct bt_gatt_attr *attr; const void *data; uint16_t len; void (*func)(struct bt_conn *,void *); void *user_data; };
typedef unsigned bt_security_t;
enum bt_security_err { BT_SECURITY_ERR_SUCCESS, BT_SECURITY_ERR_UNSPECIFIED };
enum bt_conn_type { BT_CONN_TYPE_LE=1 };
#define BT_CONN_STATE_CONNECTED 2
struct bt_conn_info { int state; };
struct bt_conn_cb { void (*connected)(struct bt_conn *,uint8_t);void (*disconnected)(struct bt_conn *,uint8_t);void (*security_changed)(struct bt_conn *,bt_security_t,enum bt_security_err); };
#define BT_CONN_CB_DEFINE(n) static const struct bt_conn_cb test_cb __attribute__((unused))
extern struct bt_conn test_connections[2];
extern unsigned test_notify_calls,test_unref_calls;
extern int test_notify_result;
extern struct bt_gatt_notify_params *test_seen_params;
extern void (*test_notify_hook)(void);
static inline unsigned bt_conn_index(const struct bt_conn *c) { return c->index; }
static inline unsigned bt_conn_get_security(const struct bt_conn *c) { return c->security; }
static inline int bt_conn_get_info(const struct bt_conn *c, struct bt_conn_info *i) { i->state=c->connected?2:0;return 0; }
static inline void bt_conn_foreach(enum bt_conn_type type,void (*fn)(struct bt_conn *,void *),void *d) { (void)type;for(unsigned i=0;i<2;i++)fn(&test_connections[i],d); }
static inline uint16_t bt_gatt_get_mtu(struct bt_conn *c) { return c->mtu; }
static inline uint16_t bt_gatt_attr_get_handle(const struct bt_gatt_attr *a) { return a->handle; }
static inline bool bt_gatt_is_subscribed(struct bt_conn *c,const struct bt_gatt_attr *a,uint16_t type) { (void)type;unsigned id=a->handle==106?1:a->handle==110?2:a->handle==114?3:a->handle==123?4:0;return id && (c->mask&(1U<<(id-1))); }
static inline int bt_gatt_notify_cb(struct bt_conn *c,struct bt_gatt_notify_params *p) { (void)c;test_notify_calls++;test_seen_params=p;if(test_notify_hook)test_notify_hook();return test_notify_result; }
static inline struct bt_conn *zmk_ble_active_profile_conn(void) { return &test_connections[0]; }
static inline void bt_conn_unref(struct bt_conn *c) { (void)c;test_unref_calls++; }
static inline int zmk_ble_active_profile_index(void) { return 1; }
struct zmk_endpoint_instance { int transport; };
static inline struct zmk_endpoint_instance zmk_endpoints_selected(void) { return (struct zmk_endpoint_instance){1}; }
typedef int zmk_event_t;
static inline const void *as_zmk_position_state_changed(const zmk_event_t *e) { return e; }
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_LISTENER(n,fn) static void *used_listener __attribute__((unused))=(void *)&fn
#define ZMK_SUBSCRIPTION(...)
