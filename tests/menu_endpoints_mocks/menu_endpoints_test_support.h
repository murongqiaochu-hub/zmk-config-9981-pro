/* Host-only deterministic SDK mocks; never electrical/radio/device evidence. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifndef CONFIG_SETTINGS
#define CONFIG_SETTINGS 1
#endif
#ifndef CONFIG_ZMK_USB
#define CONFIG_ZMK_USB 1
#endif
#ifndef CONFIG_ZMK_BLE
#define CONFIG_ZMK_BLE 1
#endif
#ifndef CONFIG_ZMK_POINTING
#define CONFIG_ZMK_POINTING 1
#endif
#define CONFIG_BBP9981_MENU_DIAG 1
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define CONFIG_ZMK_LOG_LEVEL 4
#define CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE 100
#define CONFIG_BT_MAX_PAIRED 5
#define ZMK_BLE_PROFILE_COUNT 5
#define IS_ENABLED(x) x
#define MD_UNPAREN(...) __VA_ARGS__
#define MD_COND_0(yes,no) MD_UNPAREN no
#define MD_COND_1(yes,no) MD_UNPAREN yes
#define MD_CONCAT_INNER(a,b) a##b
#define MD_CONCAT(a,b) MD_CONCAT_INNER(a,b)
#define COND_CODE_1(flag,yes,no) MD_CONCAT(MD_COND_,flag)(yes,no)
#define K_MSEC(n) (n)
#define APPLICATION 0
#define SYS_INIT(fn,...) static void *const ref_##fn __attribute__((unused))=(void *)&fn
#define LOG_LEVEL_INF 3
#define LOG_MODULE_REGISTER(...)
#define LOG_MODULE_DECLARE(...)
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
#define DT_NODELABEL(n) n
#define DT_NODE_EXISTS(n) 1
#define MD_STRING_INNER(x) #x
#define MD_STRING(x) MD_STRING_INNER(x)
#define DEVICE_DT_NAME(n) MD_STRING(n)

static uint32_t fake_now;
static unsigned irq_depth,lock_calls;
static unsigned irq_lock(void){lock_calls++;return irq_depth++;}
static void irq_unlock(unsigned key){assert(irq_depth==key+1U);irq_depth--;}
static uint32_t k_uptime_get_32(void){return fake_now;}
struct k_work {unsigned marker;};
struct k_work_delayable {struct k_work work;bool pending;unsigned schedules;int delay;void (*handler)(struct k_work *);};
static unsigned work_schedule_calls;
static int work_schedule_result;
static int k_work_reschedule(struct k_work_delayable *w,int delay){
 assert(irq_depth==0U);w->pending=true;w->schedules++;w->delay=delay;work_schedule_calls++;return work_schedule_result;
}
static void k_work_init_delayable(struct k_work_delayable *w,void (*fn)(struct k_work *)){
 assert(irq_depth==0U);memset(w,0,sizeof(*w));w->handler=fn;
}
static char md_logs[1024][256];
static unsigned md_log_count,sdk_info_count,stock_select_calls;
static void md_test_log(const char *fmt,...){
 assert(irq_depth==0U);
 if(strncmp(fmt,"MDIAG ",6)!=0){sdk_info_count++;return;}
 assert(md_log_count<1024U);va_list ap;va_start(ap,fmt);
 int n=vsnprintf(md_logs[md_log_count],sizeof(md_logs[0]),fmt,ap);va_end(ap);
 assert(n>0&&(size_t)n<sizeof(md_logs[0]));md_log_count++;
}
static void md_test_debug(const char *fmt,...){
 assert(irq_depth==0U);
 if(strcmp(fmt,"Selected endpoint transport %d")==0)stock_select_calls++;
}
#define LOG_INF(...) md_test_log(__VA_ARGS__)
#define LOG_DBG(...) md_test_debug(__VA_ARGS__)
#define LOG_ERR(...) do{assert(irq_depth==0U);}while(0)

/* endpoints_types.h and endpoints.h in this directory are unmodified upstream
 * v0.3.0 fixtures. In particular USB=0/BLE=1 comes from the real header. */
#include <zmk/endpoints_types.h>
static bool fake_usb_ready,fake_ble_ready;
static int fake_ble_index;
static unsigned usb_ready_reads,ble_ready_reads,ble_index_reads;
static bool zmk_usb_is_hid_ready(void){assert(irq_depth==0U);usb_ready_reads++;return fake_usb_ready;}
static bool zmk_ble_active_profile_is_connected(void){assert(irq_depth==0U);ble_ready_reads++;return fake_ble_ready;}
static int zmk_ble_active_profile_index(void){assert(irq_depth==0U);ble_index_reads++;return fake_ble_index;}

struct zmk_position_state_changed {uint32_t position;bool state;int64_t timestamp;uint8_t source;};
struct zmk_endpoint_changed {struct zmk_endpoint_instance endpoint;};
struct zmk_ble_active_profile_changed {int index;};
struct zmk_usb_conn_state_changed {int state;};
typedef struct {unsigned type;struct zmk_position_state_changed position;struct zmk_endpoint_changed endpoint;} zmk_event_t;
#define MD_TEST_POSITION 1U
#define MD_TEST_ENDPOINT 2U
static const struct zmk_position_state_changed *as_zmk_position_state_changed(const zmk_event_t *e){
 return e->type==MD_TEST_POSITION?&e->position:NULL;
}
static const struct zmk_endpoint_changed *as_zmk_endpoint_changed(const zmk_event_t *e){
 return e->type==MD_TEST_ENDPOINT?&e->endpoint:NULL;
}
static unsigned endpoint_changes;
static struct zmk_endpoint_instance last_endpoint;
static int raise_zmk_endpoint_changed(struct zmk_endpoint_changed e){
 assert(irq_depth==0U);endpoint_changes++;last_endpoint=e.endpoint;return 0;
}

struct device {int unused;};
struct zmk_behavior_binding {const char *behavior_dev;uint32_t param1,param2;};
typedef uint8_t zmk_keymap_layer_id_t;
typedef uint8_t zmk_keymap_layer_index_t;
#define ZMK_KEYMAP_LAYER_ID_INVAL UINT8_MAX
static zmk_keymap_layer_index_t fake_layer;
static zmk_keymap_layer_id_t fake_layer_ids[4]={0,1,2,3};
static struct zmk_behavior_binding fake_binding={"out",2,0};
static bool fake_binding_valid=true,fake_behavior_available=true;
static unsigned binding_reads;
static zmk_keymap_layer_index_t zmk_keymap_highest_layer_active(void){assert(irq_depth==0U);return fake_layer;}
static zmk_keymap_layer_id_t zmk_keymap_layer_index_to_id(zmk_keymap_layer_index_t index){
 assert(irq_depth==0U);return index<4U?fake_layer_ids[index]:ZMK_KEYMAP_LAYER_ID_INVAL;
}
static const struct zmk_behavior_binding *zmk_keymap_get_layer_binding_at_idx(zmk_keymap_layer_id_t layer,uint8_t idx){
 assert(irq_depth==0U);assert(layer<4U&&idx==7U);binding_reads++;return fake_binding_valid?&fake_binding:NULL;
}
static const struct device *zmk_behavior_get_binding(const char *name){
 static const struct device d={0};assert(irq_depth==0U);assert(name);return fake_behavior_available?&d:NULL;
}

#define HID_USAGE_KEY 0x07
#define HID_USAGE_CONSUMER 0x0c
struct md_mock_report_body {uint8_t unused;};
struct zmk_hid_keyboard_report {struct md_mock_report_body body;};
struct zmk_hid_consumer_report {struct md_mock_report_body body;};
struct zmk_hid_mouse_report {struct md_mock_report_body body;};
static struct zmk_hid_keyboard_report mock_keyboard;
static struct zmk_hid_consumer_report mock_consumer;
static struct zmk_hid_mouse_report mock_mouse;
static unsigned hid_clear_calls,usb_sends,ble_sends;
static int usb_send_result,ble_send_result;
static struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void){return &mock_keyboard;}
static struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void){return &mock_consumer;}
static struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void){return &mock_mouse;}
static void zmk_hid_keyboard_clear(void){assert(irq_depth==0U);hid_clear_calls++;}
static void zmk_hid_consumer_clear(void){assert(irq_depth==0U);hid_clear_calls++;}
static void zmk_hid_mouse_clear(void){assert(irq_depth==0U);hid_clear_calls++;}
static int zmk_usb_hid_send_keyboard_report(void){assert(irq_depth==0U);usb_sends++;return usb_send_result;}
static int zmk_usb_hid_send_consumer_report(void){assert(irq_depth==0U);usb_sends++;return usb_send_result;}
static int zmk_usb_hid_send_mouse_report(void){assert(irq_depth==0U);usb_sends++;return usb_send_result;}
static int zmk_hog_send_keyboard_report(const void *p){(void)p;assert(irq_depth==0U);ble_sends++;return ble_send_result;}
static int zmk_hog_send_consumer_report(const void *p){(void)p;assert(irq_depth==0U);ble_sends++;return ble_send_result;}
static int zmk_hog_send_mouse_report(const void *p){(void)p;assert(irq_depth==0U);ble_sends++;return ble_send_result;}

typedef int (*settings_read_cb)(void *,void *,size_t);
static unsigned settings_saves;
static enum zmk_transport saved_preference;
static int settings_save_one(const char *name,const void *value,size_t len){
 assert(irq_depth==0U);assert(strcmp(name,"endpoints/preferred")==0&&len==sizeof(saved_preference));
 memcpy(&saved_preference,value,len);settings_saves++;return 0;
}
static bool settings_name_steq(const char *a,const char *b,const char **next){
 (void)next;return strcmp(a,b)==0;
}
#define SETTINGS_STATIC_HANDLER_DEFINE(...)
