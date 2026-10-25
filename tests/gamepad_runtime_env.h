/* Minimal host kernel/route mocks for the ACTUAL behavior_gamepad.c. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_report.h"
#define CONFIG_ZMK_USB 1
#define CONFIG_ZMK_BLE 1
#define CONFIG_ZMK_LOG_LEVEL 3
#define CONFIG_A320_TAP_OUTPUT 1
#define CONFIG_APPLICATION_INIT_PRIORITY 50
#define IS_ENABLED(x) (x)
#define DT_HAS_COMPAT_STATUS_OKAY(x) 0
#define APPLICATION 0
#define SYS_INIT(...)
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...) do{}while(0)
#define LOG_WRN(...) do{}while(0)
#define LOG_ERR(...) do{}while(0)
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_BEHAVIOR_OPAQUE 0
#define K_MSEC(x) (x)
#define K_THREAD_STACK_DEFINE(n,s) unsigned char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
typedef int atomic_t;
static inline int atomic_get(const atomic_t *a){return *a;}
static inline int atomic_inc(atomic_t *a){return (*a)++;}
static inline int atomic_set(atomic_t *a,int v){int old=*a;*a=v;return old;}
static inline bool atomic_cas(atomic_t *a,int old,int value){if(*a!=old)return false;*a=value;return true;}
struct k_spinlock {int unused;};typedef int k_spinlock_key_t;
static unsigned lock_depth;
static inline int k_spin_lock(struct k_spinlock *l){(void)l;assert(!lock_depth);lock_depth++;return 0;}
static inline void k_spin_unlock(struct k_spinlock *l,int k){(void)l;(void)k;assert(lock_depth==1);lock_depth--;}
struct k_work {void(*fn)(struct k_work*);};
struct k_work_delayable {struct k_work work;};
struct k_work_q {int started;};
struct k_work_queue_config {const char *name;};
#define K_WORK_DELAYABLE_DEFINE(n,f) struct k_work_delayable n={.work={.fn=(f)}}
static int64_t now_ms;static unsigned schedules;static uint32_t scheduled_delay;static bool work_pending;
static inline int64_t k_uptime_get(void){return now_ms;}
static inline int k_work_reschedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,int d){assert(q->started);(void)w;assert(!lock_depth);schedules++;scheduled_delay=(uint32_t)d;work_pending=true;return 1;}
static inline int k_work_schedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,int d){assert(q->started);(void)w;assert(!lock_depth);if(work_pending)return 0;schedules++;scheduled_delay=(uint32_t)d;work_pending=true;return 1;}
static inline void k_work_queue_start(struct k_work_q *q,void *s,size_t n,int p,const struct k_work_queue_config *c){(void)s;assert(n==2048&&p==5&&c->name);q->started=1;}
struct device {int unused;};
typedef struct {int unused;} zmk_event_t;
struct zmk_behavior_binding {uint32_t param1;};
struct zmk_behavior_binding_event {uint32_t position;int64_t timestamp;};
enum zmk_transport {ZMK_TRANSPORT_USB,ZMK_TRANSPORT_BLE};
struct zmk_endpoint_instance {enum zmk_transport transport;struct {uint8_t profile_index;} ble;};
static struct zmk_endpoint_instance selected;
/* Fixed SDK contract: IDs are stable, indices are positions in layer order. */
typedef uint8_t zmk_keymap_layer_id_t;
#define ZMK_KEYMAP_LAYER_ID_INVAL UINT8_MAX
typedef uint8_t zmk_keymap_layer_index_t;
static zmk_keymap_layer_index_t highest_layer;
static zmk_keymap_layer_id_t layer_order[] = {0, 1, 2, 3};
static inline void reset_layer_order(void){
    for(size_t i=0;i<sizeof(layer_order)/sizeof(layer_order[0]);i++)layer_order[i]=(uint8_t)i;
}
static bool usb_ready;
static uint32_t usb_epoch;
static inline struct zmk_endpoint_instance zmk_endpoints_selected(void){assert(!lock_depth);return selected;}
static inline zmk_keymap_layer_index_t zmk_keymap_highest_layer_active(void){assert(!lock_depth);return highest_layer;}
static inline zmk_keymap_layer_id_t zmk_keymap_layer_index_to_id(zmk_keymap_layer_index_t index){
    assert(!lock_depth);
    return index<sizeof(layer_order)/sizeof(layer_order[0])?layer_order[index]:ZMK_KEYMAP_LAYER_ID_INVAL;
}
static inline bool zmk_usb_is_hid_ready(void){assert(!lock_depth);return usb_ready;}
struct bt_conn {int id,refs;bool connected,subscribed,secure;};
static struct bt_conn peer_a,peer_b,*active_peer;
static unsigned security_requests;
static inline struct bt_conn *zmk_ble_active_profile_conn(void){assert(!lock_depth);if(!active_peer||!active_peer->connected)return NULL;active_peer->refs++;return active_peer;}
static inline void bt_conn_unref(struct bt_conn *c){assert(!lock_depth&&c&&c->refs>1);c->refs--;}
#define BT_SECURITY_L2 2
static inline int bt_conn_set_security(struct bt_conn *c,int l){assert(!lock_depth&&c&&l==2);security_requests++;return 0;}
typedef uint8_t bt_security_t;
enum bt_security_err {BT_SECURITY_ERR_SUCCESS};
struct bt_conn_cb {void(*connected)(struct bt_conn*,uint8_t);void(*disconnected)(struct bt_conn*,uint8_t);void(*security_changed)(struct bt_conn*,bt_security_t,enum bt_security_err);};
#define BT_CONN_CB_DEFINE(n) struct bt_conn_cb n
struct accepted_frame {uint8_t transport;int peer;uint32_t context;struct bbp_gamepad_report_body body;};
static struct accepted_frame accepted[4096];static unsigned accepted_count;
static int forced_send_error;
static void (*during_send)(void);
