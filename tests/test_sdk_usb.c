/* Compile the unmodified pinned SDK usb.c AND usb_hid.c with real production extensions. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define CONFIG_ZMK_POINTING 1
#define CONFIG_ZMK_USB_BOOT 1
#define CONFIG_ZMK_POINTING_SMOOTH_SCROLLING 0
#define CONFIG_ZMK_HID_INDICATORS 0
#define IS_ENABLED(x) (x)
#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
#define SYS_INIT(...)
#define K_MSEC(x) (x)
#define ATOMIC_INIT(x) (x)
typedef int atomic_t;
static int atomic_get(atomic_t *v){return *v;}
static int atomic_inc(atomic_t *v){return (*v)++;}
struct device {int id;};static struct device device;
struct k_sem {int value;};
#define K_SEM_DEFINE(name,count,max) struct k_sem name={count}
struct k_work {void(*handler)(struct k_work*);};
#define K_WORK_DEFINE(name,handler_) struct k_work name={handler_}
static int k_work_submit(struct k_work *w){(void)w;return 0;}
enum usb_dc_status_code {USB_DC_UNKNOWN,USB_DC_ERROR,USB_DC_RESET,USB_DC_DISCONNECTED,USB_DC_CONFIGURED,USB_DC_SUSPEND,USB_DC_RESUME,USB_DC_CLEAR_HALT,USB_DC_SOF};
typedef void(*usb_dc_status_callback)(enum usb_dc_status_code,const uint8_t*);
static usb_dc_status_callback actual_cb;static unsigned enable_calls;
static int usb_enable(usb_dc_status_callback cb){enable_calls++;actual_cb=cb;return 0;}
static int usb_wakeup_request(void){assert(0);return 0;}
enum zmk_usb_conn_state {ZMK_USB_CONN_NONE,ZMK_USB_CONN_POWERED,ZMK_USB_CONN_HID};
enum usb_dc_status_code zmk_usb_get_status(void);
enum zmk_usb_conn_state zmk_usb_get_conn_state(void);
void zmk_usb_hid_set_protocol(uint8_t protocol);
struct zmk_usb_conn_state_changed {enum zmk_usb_conn_state conn_state;};
static int raise_zmk_usb_conn_state_changed(struct zmk_usb_conn_state_changed s){(void)s;return 0;}
struct usb_setup_packet {uint16_t wValue;};
struct hid_ops {void(*protocol_change)(const struct device*,uint8_t);void(*int_in_ready)(const struct device*);int(*get_report)(const struct device*,struct usb_setup_packet*,int32_t*,uint8_t**);int(*set_report)(const struct device*,struct usb_setup_packet*,int32_t*,uint8_t**);};
#define HID_PROTOCOL_REPORT 1
#define HID_PROTOCOL_BOOT 0
#define HID_BOOT_IFACE_CODE_KEYBOARD 1
#define ZMK_HID_REPORT_ID_KEYBOARD 1
#define ZMK_HID_REPORT_ID_CONSUMER 2
#define ZMK_HID_REPORT_ID_MOUSE 3
struct zmk_hid_mouse_report_body {uint8_t buttons;int16_t d_x,d_y,d_scroll_x,d_scroll_y;};
struct zmk_hid_mouse_report {uint8_t report_id;struct zmk_hid_mouse_report_body body;};
struct zmk_hid_keyboard_report {uint8_t report_id;uint8_t body[8];};
struct zmk_hid_consumer_report {uint8_t report_id;uint8_t body[8];};
typedef struct {uint8_t body[8];} zmk_hid_boot_report_t;
static struct zmk_hid_mouse_report global_mouse;
static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static zmk_hid_boot_report_t boot;
static uint8_t zmk_hid_report_desc[1];
static struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void){return &global_mouse;}
static struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void){return &keyboard;}
static struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void){return &consumer;}
static zmk_hid_boot_report_t *zmk_hid_get_boot_report(void){return &boot;}
static const struct device *device_get_binding(const char *name){(void)name;return &device;}
static void usb_hid_register_device(const struct device*d,const uint8_t*r,size_t n,const struct hid_ops*o){(void)d;(void)r;(void)n;(void)o;}
static void usb_hid_set_proto_code(const struct device*d,int p){(void)d;(void)p;}
static int usb_hid_init(const struct device*d){(void)d;return 0;}
static int take_error,write_error;static unsigned takes,gives,writes;static void(*during_wait)(void);
static struct zmk_hid_mouse_report last_wire;
static int k_sem_take(struct k_sem*s,int ms){assert(ms==30);takes++;if(during_wait)during_wait();if(!take_error){assert(s->value);s->value--;}return take_error;}
static void k_sem_give(struct k_sem*s){s->value++;gives++;}
static int hid_int_ep_write(const struct device*d,const uint8_t*p,size_t n,void*out){(void)out;assert(d==&device&&n==sizeof(last_wire));writes++;memcpy(&last_wire,p,n);return write_error;}
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_usb_epoch.h"
#include "sdk_v0_3_0_usb.c"
#undef usb_enable
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_usb_snapshot.h"
#include "sdk_v0_3_0_usb_hid.c"
static void reconnect(void){actual_cb(USB_DC_RESET,0);actual_cb(USB_DC_CONFIGURED,0);}
static void foreign_right(void){global_mouse.body.buttons|=2;}
int main(void){
 assert(zmk_usb_init()==0&&enable_calls==1);assert(zmk_usb_hid_init()==0);actual_cb(USB_DC_CONFIGURED,0);
 global_mouse.body=(struct zmk_hid_mouse_report_body){.buttons=1,.d_x=91,.d_y=-47,.d_scroll_x=4,.d_scroll_y=5};
 struct zmk_hid_mouse_report_body source=global_mouse.body;uint32_t epoch=bbp9981_usb_hid_connection_epoch();
 assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==0);
 assert(last_wire.report_id==ZMK_HID_REPORT_ID_MOUSE&&last_wire.body.buttons==1&&last_wire.body.d_x==0&&last_wire.body.d_y==0&&last_wire.body.d_scroll_x==0&&last_wire.body.d_scroll_y==0);
 assert(global_mouse.body.d_x==91&&source.d_x==91);in_ready_cb(&device);
 during_wait=foreign_right;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==0&&last_wire.body.buttons==3);in_ready_cb(&device);during_wait=0;
 take_error=-EAGAIN;unsigned before=writes;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-EAGAIN&&writes==before);take_error=0;
 during_wait=reconnect;before=writes;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-ESTALE&&writes==before&&hid_sem.value==1);during_wait=0;
 assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-ESTALE);
 epoch=bbp9981_usb_hid_connection_epoch();actual_cb(USB_DC_SUSPEND,0);before=takes;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-EAGAIN&&takes==before);
 actual_cb(USB_DC_CONFIGURED,0);write_error=-EIO;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-EIO&&hid_sem.value==1);write_error=0;
 zmk_usb_hid_set_protocol(HID_PROTOCOL_BOOT);before=writes;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-ENOTSUP&&writes==before);zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
 assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(0,epoch)==-EINVAL);
 assert(zmk_usb_is_hid_ready());actual_cb(USB_DC_DISCONNECTED,0);assert(!zmk_usb_is_hid_ready());
 puts("PASS Actual SDK USB source: same semaphore, zero-motion snapshot, foreign owners refreshed after wait, timeout/write failures, reset generation fence, suspend not ACK, boot guard, original callback/init preserved");
 return 0;
}
