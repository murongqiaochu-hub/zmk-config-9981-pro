/* SPDX-License-Identifier: MIT
 * Deterministic host-only Zephyr seams. The ZMK BLE TU/headers are unmodified
 * upstream fixtures; this file is deliberately a mock, not SDK implementation.
 */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifndef CONFIG_SETTINGS
#define CONFIG_SETTINGS 1
#endif
#ifndef CONFIG_ZMK_BLE
#define CONFIG_ZMK_BLE 1
#endif
#ifndef CONFIG_ZMK_USB
#define CONFIG_ZMK_USB 1
#endif
#ifndef CONFIG_ZMK_SPLIT
#define CONFIG_ZMK_SPLIT 0
#endif
#ifndef CONFIG_ZMK_SPLIT_BLE
#define CONFIG_ZMK_SPLIT_BLE 0
#endif
#ifndef CONFIG_ZMK_SPLIT_ROLE_CENTRAL
#define CONFIG_ZMK_SPLIT_ROLE_CENTRAL 0
#endif
#ifndef CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL
#define CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL 0
#endif
#define CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS 1
#define CONFIG_BT_MAX_CONN 8
#define CONFIG_BT_MAX_PAIRED 5
#define CONFIG_BT_DEVICE_NAME "host-only-9981"
#define CONFIG_BT_DEVICE_NAME_MAX 32
#define CONFIG_ZMK_LOG_LEVEL 4
#define CONFIG_BBP9981_BLE_SINGLE_HOST_LOG_LEVEL 4
#define CONFIG_ZMK_BLE_INIT_PRIORITY 50
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE 1000
#define CONFIG_BT_SMP 1
#define CONFIG_BT_SMP_APP_PAIRING_ACCEPT 1
#define CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE 0
#define CONFIG_ZMK_BLE_PASSKEY_ENTRY 0
#define CONFIG_ZMK_BLE_CLEAR_BONDS_ON_START 0

#define T_JOIN_(a,b) a##b
#define T_JOIN(a,b) T_JOIN_(a,b)
#define T_ARG_PLACEHOLDER_1 0,
#define T_SECOND_ARG(_ignored,val,...) val
#define T_IS_ENABLED_2(arg) T_SECOND_ARG(arg 1,0)
#define T_IS_ENABLED_1(value) T_IS_ENABLED_2(T_ARG_PLACEHOLDER_##value)
#define IS_ENABLED(value) T_IS_ENABLED_1(value)
#define T_UNPAREN(...) __VA_ARGS__
#define T_COND_0(yes,no) T_UNPAREN no
#define T_COND_1(yes,no) T_UNPAREN yes
#define COND_CODE_1(flag,yes,no) T_JOIN(T_COND_,flag)(yes,no)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define BIT(n) (1UL<<(n))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ARG_UNUSED(x) (void)(x)
#define CONTAINER_OF(p,type,member) ((type *)((char *)(p)-offsetof(type,member)))
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define BUILD_ASSERT(c,...) _Static_assert(c,#__VA_ARGS__)
#define __ASSERT(c,...) assert(c)
#define __ASSERT_NO_MSG(c) assert(c)
#define APPLICATION 0
#define K_MSEC(ms) ((int64_t)(ms))
#define K_SECONDS(s) K_MSEC((s)*1000)
#define K_NO_WAIT 0
#define K_FOREVER INT64_MAX
#define LOG_LEVEL_INF 3
#define LOG_MODULE_DECLARE(...)
#define LOG_MODULE_REGISTER(...)
void test_log(const char *,...);
#define LOG_DBG(...) test_log(__VA_ARGS__)
#define LOG_INF(...) test_log(__VA_ARGS__)
#define LOG_WRN(...) test_log(__VA_ARGS__)
#define LOG_ERR(...) test_log(__VA_ARGS__)

struct device { unsigned unused; };
typedef uint32_t zmk_key_t;
struct k_work {
    void (*handler)(struct k_work *);
    bool pending;
    uint64_t due, order;
};
struct k_work_delayable { struct k_work work; };
#define K_WORK_DEFINE(name,fn) struct k_work name = {.handler=(fn)}
#define K_WORK_DELAYABLE_DEFINE(name,fn) struct k_work_delayable name = {.work={.handler=(fn)}}
#define K_WORK_SCHEDULED 1
#define K_WORK_RUNNING 2
typedef int64_t k_timeout_t;
int k_work_submit(struct k_work *);
int k_work_schedule(struct k_work_delayable *,k_timeout_t);
int k_work_reschedule(struct k_work_delayable *,k_timeout_t);
int k_work_cancel_delayable(struct k_work_delayable *);
int k_work_cancel(struct k_work *);
bool k_work_is_pending(const struct k_work *);
int k_work_busy_get(const struct k_work *);
void k_work_init(struct k_work *,void (*)(struct k_work *));
void k_work_init_delayable(struct k_work_delayable *,void (*)(struct k_work *));
static inline struct k_work_delayable *k_work_delayable_from_work(struct k_work *w) {
    return CONTAINER_OF(w,struct k_work_delayable,work);
}
int64_t k_uptime_get(void);
uint32_t k_uptime_get_32(void);
void *k_current_get(void);
void k_sched_lock(void);
void k_sched_unlock(void);
unsigned int irq_lock(void);
void irq_unlock(unsigned int);
struct k_spinlock { unsigned held; };
typedef unsigned k_spinlock_key_t;
k_spinlock_key_t k_spin_lock(struct k_spinlock *);
void k_spin_unlock(struct k_spinlock *,k_spinlock_key_t);
typedef int atomic_t;
static inline int atomic_get(const atomic_t *p) { return *p; }
static inline int atomic_set(atomic_t *p,int v) { int old=*p;*p=v;return old; }
static inline int atomic_inc(atomic_t *p) { return (*p)++; }
static inline bool atomic_cas(atomic_t *p,int old,int v) {
    if(*p!=old)return false;
    *p=v;return true;
}

void test_register_init(const char *,int (*)(void),unsigned);
#define SYS_INIT(fn,level,prio) \
    static void T_JOIN(test_register_init_,fn)(void) __attribute__((constructor)); \
    static void T_JOIN(test_register_init_,fn)(void) { test_register_init(#fn,fn,prio); }

typedef struct { uint8_t val[6]; } bt_addr_t;
typedef struct { uint8_t type; bt_addr_t a; } bt_addr_le_t;
#define BT_ADDR_LE_PUBLIC 0
#define BT_ADDR_LE_RANDOM 1
#define BT_ADDR_LE_PUBLIC_ID 2
#define BT_ADDR_LE_RANDOM_ID 3
extern const bt_addr_le_t test_addr_any,test_addr_none;
#define BT_ADDR_LE_ANY (&test_addr_any)
#define BT_ADDR_LE_NONE (&test_addr_none)
#define BT_ADDR_LE_STR_LEN 30
static inline int bt_addr_le_cmp(const bt_addr_le_t *a,const bt_addr_le_t *b) {
    return memcmp(a,b,sizeof(*a));
}
static inline void bt_addr_le_copy(bt_addr_le_t *a,const bt_addr_le_t *b) { *a=*b; }
static inline bool bt_addr_is_rpa(const bt_addr_t *a) { return (a->val[5]&0xc0)==0x40; }
static inline bool bt_addr_le_is_rpa(const bt_addr_le_t *a) {
    return a->type==BT_ADDR_LE_RANDOM && bt_addr_is_rpa(&a->a);
}
int bt_addr_le_to_str(const bt_addr_le_t *,char *,size_t);
#define BT_ID_DEFAULT 0
#define BT_CONN_TYPE_LE 1
#define BT_CONN_TYPE_BR 2
#define BT_CONN_TYPE_ALL 0xff
#define BT_CONN_ROLE_CENTRAL 0
#define BT_CONN_ROLE_PERIPHERAL 1
#define BT_CONN_STATE_DISCONNECTED 0
#define BT_CONN_STATE_CONNECTING 1
#define BT_CONN_STATE_CONNECTED 2
#define BT_CONN_STATE_DISCONNECTING 3
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 0x13
#define BT_SECURITY_L1 1
#define BT_SECURITY_L2 2
#define BT_SECURITY_L3 3
#define BT_SECURITY_L4 4
typedef uint8_t bt_security_t;
enum bt_security_err { BT_SECURITY_ERR_SUCCESS=0,BT_SECURITY_ERR_AUTH_FAIL=1,
    BT_SECURITY_ERR_PAIR_NOT_ALLOWED=2,BT_SECURITY_ERR_UNSPECIFIED=10 };
struct bt_conn_le_info {
    const bt_addr_le_t *src,*dst,*local,*remote;
    uint16_t interval,latency,timeout;
};
struct bt_conn_info {
    uint8_t type,role,id,state;
    struct bt_conn_le_info le;
    struct { bt_security_t level; uint8_t flags,enc_key_size; } security;
};
struct bt_conn {
    bool used,resolved;
    unsigned serial,refs,index;
    uint8_t type,role,id,state;
    bt_addr_le_t dst,rpa,identity;
    int info_error,disconnect_result;
    unsigned disconnect_calls;
};
struct bt_conn_pairing_feat { uint8_t unused; };
struct bt_bond_info { bt_addr_le_t addr; };
struct bt_conn_cb {
    void (*connected)(struct bt_conn *,uint8_t);
    void (*disconnected)(struct bt_conn *,uint8_t);
    void (*identity_resolved)(struct bt_conn *,const bt_addr_le_t *,const bt_addr_le_t *);
    void (*security_changed)(struct bt_conn *,bt_security_t,enum bt_security_err);
    void (*le_param_updated)(struct bt_conn *,uint16_t,uint16_t,uint16_t);
};
struct bt_conn_auth_cb {
    enum bt_security_err (*pairing_accept)(struct bt_conn *,const struct bt_conn_pairing_feat *);
    void (*cancel)(struct bt_conn *);
};
struct bt_conn_auth_info_cb {
    void (*pairing_complete)(struct bt_conn *,bool);
    void (*pairing_failed)(struct bt_conn *,enum bt_security_err);
    struct { void *next; } node;
};
void bt_conn_cb_register(struct bt_conn_cb *);
#define BT_CONN_CB_DEFINE(name) \
    static struct bt_conn_cb name; \
    static void T_JOIN(test_register_conn_,name)(void) __attribute__((constructor)); \
    static void T_JOIN(test_register_conn_,name)(void) { bt_conn_cb_register(&name); } \
    static struct bt_conn_cb name
int bt_conn_auth_cb_register(const struct bt_conn_auth_cb *);
int bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *);
struct bt_conn *bt_conn_lookup_addr_le(uint8_t,const bt_addr_le_t *);
int bt_conn_get_info(const struct bt_conn *,struct bt_conn_info *);
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *);
struct bt_conn *bt_conn_ref(struct bt_conn *);
void bt_conn_unref(struct bt_conn *);
uint8_t bt_conn_index(const struct bt_conn *);
int bt_conn_disconnect(struct bt_conn *,uint8_t);
void bt_conn_foreach(int,void (*)(struct bt_conn *,void *),void *);
void bt_foreach_bond(uint8_t,void (*)(const struct bt_bond_info *,void *),void *);
int bt_enable(void (*)(int));
int bt_unpair(uint8_t,const bt_addr_le_t *);
int bt_set_name(const char *);

#define BT_LE_ADV_OPT_CONNECTABLE BIT(0)
#define BT_LE_ADV_OPT_ONE_TIME BIT(1)
#define BT_LE_ADV_OPT_USE_NAME BIT(2)
#define BT_LE_ADV_OPT_FORCE_NAME_IN_AD BIT(3)
#define BT_LE_ADV_OPT_FILTER_CONN BIT(4)
#define BT_GAP_ADV_FAST_INT_MIN_2 160
#define BT_GAP_ADV_FAST_INT_MAX_2 240
#define BT_DATA_GAP_APPEARANCE 0x19
#define BT_DATA_FLAGS 0x01
#define BT_DATA_UUID16_SOME 0x02
#define BT_LE_AD_GENERAL 0x02
#define BT_LE_AD_NO_BREDR 0x04
struct bt_data { uint8_t type,data_len; const uint8_t *data; };
#define BT_DATA_BYTES(kind,...) {.type=(kind),.data_len=sizeof((uint8_t[]){__VA_ARGS__}),.data=(const uint8_t[]){__VA_ARGS__}}
struct bt_le_adv_param { uint8_t id,sid; uint32_t options;uint32_t interval_min,interval_max;const bt_addr_le_t *peer; };
#define BT_LE_ADV_PARAM(opts,imin,imax,addr) (&(const struct bt_le_adv_param){.options=(opts),.interval_min=(imin),.interval_max=(imax),.peer=(addr)})
#define BT_LE_ADV_CONN_DIR_LOW_DUTY(addr) BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE,160,240,addr)
int bt_le_adv_start(const struct bt_le_adv_param *,const struct bt_data *,size_t,const struct bt_data *,size_t);
int bt_le_adv_stop(void);

typedef int (*settings_read_cb)(void *,void *,size_t);
struct settings_handler {
    const char *name;
    int (*h_set)(const char *,size_t,settings_read_cb,void *);
    int (*h_commit)(void);
};
int settings_register(struct settings_handler *);
int settings_name_steq(const char *,const char *,const char **);
int settings_save_one(const char *,const void *,size_t);
int settings_delete(const char *);

typedef struct { const char *type; const void *data; } zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_EV_EVENT_HANDLED 1
int test_raise_event(const char *,const void *);
void test_register_listener(const char *,int (*)(const zmk_event_t *));
#define ZMK_EVENT_DECLARE(name) \
    static inline struct name *T_JOIN(as_,name)(const zmk_event_t *e) { \
        return !strcmp(e->type,#name)?(struct name *)e->data:NULL; \
    } \
    static inline int T_JOIN(raise_,name)(struct name e) { return test_raise_event(#name,&e); }
#define ZMK_LISTENER(name,callback) \
    static void T_JOIN(test_register_listener_,name)(void) __attribute__((constructor)); \
    static void T_JOIN(test_register_listener_,name)(void) { test_register_listener(#name,callback); }
#define ZMK_SUBSCRIPTION(...)

/* Test driver API; none of these symbols occur in production source. */
struct test_metrics {
    unsigned disconnects,adv_starts,adv_stops,settings_saves,settings_deletes;
    unsigned unpairs,events,work_schedules,work_runs,conn_refs,conn_unrefs,logs;
    unsigned usb_mutations,sched_locks,sched_unlocks,sched_max_depth;
};
extern struct test_metrics test_metrics;
extern struct bt_conn test_connections[CONFIG_BT_MAX_CONN];
extern unsigned test_conn_callback_count,test_init_count,test_listener_count;
extern int test_adv_start_result,test_adv_stop_result,test_work_result;
extern unsigned test_irq_depth,test_sched_depth;
extern int test_select_on_sched_lock,test_select_on_settings_block;
extern int test_update_profile_on_sched_lock;
extern bt_addr_le_t test_update_profile_addr;
void test_run_inits(void);
void test_commit_settings(void);
void test_load_profile(uint8_t,const bt_addr_le_t *);
void test_load_active(uint8_t);
void test_run_ready(void);
void test_advance(uint64_t);
unsigned test_pending_work(void);
void test_reset_metrics(void);
bt_addr_le_t test_address(unsigned,uint8_t);
struct bt_conn *test_seed_conn(unsigned,const bt_addr_le_t *,uint8_t,uint8_t);
void test_emit_connected(struct bt_conn *,uint8_t);
void test_emit_disconnected(struct bt_conn *,uint8_t);
void test_emit_identity(struct bt_conn *,const bt_addr_le_t *);
void test_emit_security(struct bt_conn *,bt_security_t,enum bt_security_err);
enum bt_security_err test_pair_accept(struct bt_conn *);
void test_pair_complete(struct bt_conn *,bool);
void test_pair_cancel(struct bt_conn *);
void test_pair_failed(struct bt_conn *,enum bt_security_err);
void test_add_bond(const bt_addr_le_t *);
void test_assert_balanced(void);
