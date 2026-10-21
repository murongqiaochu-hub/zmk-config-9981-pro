/* Compile the unmodified pinned SDK usb.c AND usb_hid.c with real production extensions.
 * The driver retains source until completion: it NEVER copies at submission.
 *
 * cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter
 *    -Wno-unused-function -Wno-unused-variable -fsanitize=undefined
 *    -fno-sanitize-recover=all -Itests/sdk_snapshot_mocks
 *    tests/test_sdk_usb.c -o /tmp/test_sdk_usb
 * Cases: normal (all original assertions), lifetime, protocol-wait.
 * -DBBP_USB_SNAPSHOT_SOURCE='"/absolute/path/to/old_header.h"' reuses the
 * SAME public API tests for behavior negatives; no new private state is used.
 * Host model only: not real enumeration, nrfx IRQ/DMA or device validation. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <nrfx_usbd.h>
#define CONFIG_ZMK_POINTING 1
#ifndef CONFIG_ZMK_USB_BOOT
#define CONFIG_ZMK_USB_BOOT 1
#endif
#ifndef CONFIG_SOC_NRF52840_QIAA
#define CONFIG_SOC_NRF52840_QIAA 1
#endif
#ifndef CONFIG_USB_NRFX
#define CONFIG_USB_NRFX 1
#endif
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
struct device {int id;const void *config;};
struct k_sem {int value;unsigned int limit;};
#define K_SEM_DEFINE(name,count,max) struct k_sem name={count,max}
struct k_work {void(*handler)(struct k_work*);};
#define K_WORK_DEFINE(name,handler_) struct k_work name={handler_}
static int k_work_submit(struct k_work *w){(void)w;return 0;}
enum usb_dc_status_code {USB_DC_UNKNOWN,USB_DC_ERROR,USB_DC_RESET,USB_DC_DISCONNECTED,USB_DC_CONFIGURED,USB_DC_SUSPEND,USB_DC_RESUME,USB_DC_CLEAR_HALT,USB_DC_SOF};
typedef void(*usb_dc_status_callback)(enum usb_dc_status_code,const uint8_t*);
/* Public cfg/endpoint field order matches the pinned Zephyr API, as in the
 * standalone Game Pad harness. HID_INT_IN_EP_IDX is endpoint[0]. */
struct usb_setup_packet {uint8_t bmRequestType,bRequest;uint16_t wValue,wIndex,wLength;} __attribute__((packed));
struct usb_desc_header {uint8_t bLength,bDescriptorType;} __attribute__((packed));
enum usb_dc_ep_cb_status_code {USB_DC_EP_SETUP,USB_DC_EP_DATA_OUT,USB_DC_EP_DATA_IN};
typedef void(*usb_ep_callback)(uint8_t,enum usb_dc_ep_cb_status_code);
typedef int(*usb_request_handler)(struct usb_setup_packet*,int32_t*,uint8_t**);
typedef void(*usb_interface_config)(struct usb_desc_header*,uint8_t);
struct usb_ep_cfg_data {usb_ep_callback ep_cb;uint8_t ep_addr;};
struct usb_interface_cfg_data {usb_request_handler class_handler,vendor_handler,custom_handler;};
struct usb_cfg_data {
 const uint8_t *usb_device_description;
 void *interface_descriptor;
 usb_interface_config interface_config;
 void(*cb_usb_status)(struct usb_cfg_data*,enum usb_dc_status_code,const uint8_t*);
 struct usb_interface_cfg_data interface;
 uint8_t num_endpoints;
 struct usb_ep_cfg_data *endpoint;
};
static struct usb_ep_cfg_data actual_eps[]={{.ep_addr=0x85},{.ep_addr=0x02}};
static struct usb_cfg_data actual_cfg={.num_endpoints=2,.endpoint=actual_eps};
static struct device device={.config=&actual_cfg};
static usb_dc_status_callback actual_cb;static unsigned enable_calls;
static int usb_enable(usb_dc_status_callback cb){enable_calls++;actual_cb=cb;return 0;}
static int usb_wakeup_request(void){assert(0);return 0;}
enum zmk_usb_conn_state {ZMK_USB_CONN_NONE,ZMK_USB_CONN_POWERED,ZMK_USB_CONN_HID};
enum usb_dc_status_code zmk_usb_get_status(void);
enum zmk_usb_conn_state zmk_usb_get_conn_state(void);
void zmk_usb_hid_set_protocol(uint8_t protocol);
struct zmk_usb_conn_state_changed {enum zmk_usb_conn_state conn_state;};
static int raise_zmk_usb_conn_state_changed(struct zmk_usb_conn_state_changed s){(void)s;return 0;}
struct hid_ops {void(*protocol_change)(const struct device*,uint8_t);void(*int_in_ready)(const struct device*);int(*get_report)(const struct device*,struct usb_setup_packet*,int32_t*,uint8_t**);int(*set_report)(const struct device*,struct usb_setup_packet*,int32_t*,uint8_t**);};
#define HID_PROTOCOL_REPORT 1
#define HID_PROTOCOL_BOOT 0
#define HID_BOOT_IFACE_CODE_KEYBOARD 1
#define ZMK_HID_REPORT_ID_KEYBOARD 1
#define ZMK_HID_REPORT_ID_CONSUMER 2
#define ZMK_HID_REPORT_ID_MOUSE 3
/* Exact mouse layout from the pinned hid.h: body9, wire10, union staging12. */
struct zmk_hid_mouse_report_body {uint8_t buttons;int16_t d_x,d_y,d_scroll_y,d_scroll_x;} __attribute__((packed));
struct zmk_hid_mouse_report {uint8_t report_id;struct zmk_hid_mouse_report_body body;} __attribute__((packed));
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
static const struct hid_ops *registered_ops;
static void usb_hid_register_device(const struct device*d,const uint8_t*r,size_t n,const struct hid_ops*o){assert(d==&device);(void)r;(void)n;registered_ops=o;}
static void usb_hid_set_proto_code(const struct device*d,int p){(void)d;(void)p;}
static int usb_hid_init(const struct device*d){(void)d;return 0;}
static int take_error,write_error,after_accept_error;static unsigned takes,gives,writes;
static void(*during_wait)(void),(*before_driver_accept)(void),(*during_driver)(void),(*before_irq_lock)(void);
static unsigned int irq_depth,busy_queries;
static bool mock_in_isr,nrf_busy[256];
static nrfx_usbd_ep_t last_busy_ep;
static struct zmk_hid_mouse_report last_wire;
static struct {
 const uint8_t *source;
 uint32_t len;
 uint8_t ep;
 bool pending;
 uint8_t completed[64];
 uint32_t completed_len;
} dma;
static bool k_is_in_isr(void){return mock_in_isr;}
static unsigned int irq_lock(void){
 if(!irq_depth&&before_irq_lock){void(*cb)(void)=before_irq_lock;before_irq_lock=NULL;cb();}
 return irq_depth++;
}
static void irq_unlock(unsigned int key){assert(irq_depth==key+1U);irq_depth=key;}
bool nrfx_usbd_ep_is_busy(nrfx_usbd_ep_t ep){
 assert(irq_depth&&!mock_in_isr&&(unsigned int)ep>=0x81U&&(unsigned int)ep<=0x87U);
 busy_queries++;last_busy_ep=ep;return nrf_busy[(unsigned int)ep];
}
static int k_sem_take(struct k_sem*s,int ms){
 assert(!irq_depth&&ms==30);takes++;
 if(during_wait){void(*cb)(void)=during_wait;during_wait=NULL;cb();}
 if(take_error){return take_error;}
 if(!s->value){return -EAGAIN;}
 s->value--;return 0;
}
static void k_sem_give(struct k_sem*s){if(s->value<(int)s->limit){s->value++;}gives++;}
static int hid_int_ep_write(const struct device*d,const uint8_t*p,uint32_t n,uint32_t*out){
 assert(!irq_depth&&d==&device&&p&&n<=sizeof(dma.completed));writes++;
 if(before_driver_accept){void(*cb)(void)=before_driver_accept;before_driver_accept=NULL;cb();}
 const struct usb_cfg_data *cfg=d->config;assert(cfg&&cfg->endpoint&&cfg->num_endpoints);
 uint8_t ep=cfg->endpoint[0].ep_addr;
 int err=(dma.pending||nrf_busy[ep])?-EAGAIN:write_error;
 if(!err){
  dma.source=p;dma.len=n;dma.ep=ep;dma.pending=true;nrf_busy[ep]=true;
  /* No memcpy here: the pinned nrfx driver stores this pointer for its IRQ. */
 }
 if(during_driver){void(*cb)(void)=during_driver;during_driver=NULL;cb();}
 if(!err&&after_accept_error){err=after_accept_error;} /* Fault: error with RAM still owned. */
 if(out){*out=err?0U:n;}
 return err;
}
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_usb_epoch.h"
#include "sdk_v0_3_0_usb.c"
#undef usb_enable
#ifndef BBP_USB_SNAPSHOT_SOURCE
#define BBP_USB_SNAPSHOT_SOURCE "../config/boards/arm/bbp9981/custom_driver/trackpad_usb_snapshot.h"
#endif
#include BBP_USB_SNAPSHOT_SOURCE
#include "sdk_v0_3_0_usb_hid.c"

/* Consume at a distinct completion point, AFTER the sender has returned. */
static void consume_dma_no_ready(void){
 assert(dma.pending&&dma.len<=sizeof(dma.completed));
 memset(dma.completed,0xA5,sizeof(dma.completed));
 memcpy(dma.completed,dma.source,dma.len);dma.completed_len=dma.len;
 if(dma.len==sizeof(last_wire)){memcpy(&last_wire,dma.completed,sizeof(last_wire));}
 nrf_busy[dma.ep]=false;dma.pending=false;
}
static void complete_dma(void){consume_dma_no_ready();in_ready_cb(&device);}
static void abort_dma_no_ready(void){assert(dma.pending);nrf_busy[dma.ep]=false;dma.pending=false;}
static void reconnect(void){actual_cb(USB_DC_RESET,0);actual_cb(USB_DC_CONFIGURED,0);}
static void foreign_right(void){global_mouse.body.buttons|=2;}
static void fixture_init(void){
 assert(zmk_usb_init()==0&&enable_calls==1);assert(zmk_usb_hid_init()==0);actual_cb(USB_DC_CONFIGURED,0);
 assert(registered_ops==&ops&&registered_ops->int_in_ready==in_ready_cb);
 global_mouse.body=(struct zmk_hid_mouse_report_body){.buttons=1,.d_x=91,.d_y=-47,.d_scroll_x=4,.d_scroll_y=5};
}
static void assert_snapshot_wire(uint8_t buttons){
 assert(dma.completed_len==sizeof(struct zmk_hid_mouse_report));
 assert(last_wire.report_id==ZMK_HID_REPORT_ID_MOUSE&&last_wire.body.buttons==buttons&&
        last_wire.body.d_x==0&&last_wire.body.d_y==0&&last_wire.body.d_scroll_x==0&&last_wire.body.d_scroll_y==0);
 assert(dma.completed[sizeof(last_wire)]==0xA5&&dma.completed[sizeof(last_wire)+1U]==0xA5);
}
static void test_sdk_existing_contract(void){
 fixture_init();
 struct zmk_hid_mouse_report_body source=global_mouse.body;uint32_t epoch=bbp9981_usb_hid_connection_epoch();
 assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==0);
 consume_dma_no_ready();
 assert(last_wire.report_id==ZMK_HID_REPORT_ID_MOUSE&&last_wire.body.buttons==1&&last_wire.body.d_x==0&&last_wire.body.d_y==0&&last_wire.body.d_scroll_x==0&&last_wire.body.d_scroll_y==0);
 assert(global_mouse.body.d_x==91&&source.d_x==91);in_ready_cb(&device);
 during_wait=foreign_right;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==0);
 consume_dma_no_ready();assert(last_wire.body.buttons==3);in_ready_cb(&device);during_wait=0;
 take_error=-EAGAIN;unsigned before=writes;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-EAGAIN&&writes==before);take_error=0;
 during_wait=reconnect;before=writes;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-ESTALE&&writes==before&&hid_sem.value==1);during_wait=0;
 assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-ESTALE);
 epoch=bbp9981_usb_hid_connection_epoch();actual_cb(USB_DC_SUSPEND,0);before=takes;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-EAGAIN&&takes==before);
 actual_cb(USB_DC_CONFIGURED,0);write_error=-EIO;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-EIO&&hid_sem.value==1);write_error=0;
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
 zmk_usb_hid_set_protocol(HID_PROTOCOL_BOOT);before=writes;assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch)==-ENOTSUP&&writes==before);zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
#endif
 assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(0,epoch)==-EINVAL);
 assert(zmk_usb_is_hid_ready());actual_cb(USB_DC_DISCONNECTED,0);assert(!zmk_usb_is_hid_ready());
 puts("PASS Actual SDK USB source: same semaphore, zero-motion snapshot, foreign owners refreshed after wait, timeout/write failures, reset generation fence, suspend not ACK, boot guard, original callback/init preserved");
}
#if defined(__GNUC__)
#define SNAPSHOT_NOINLINE __attribute__((noinline))
#else
#define SNAPSHOT_NOINLINE
#endif
static SNAPSHOT_NOINLINE void clobber_stack(void){
 volatile uint8_t bytes[8192];
 for(size_t i=0;i<sizeof(bytes);i++){bytes[i]=(uint8_t)(0xCCU^i);}
 assert(bytes[17]==(uint8_t)(0xCCU^17U));
}
static SNAPSHOT_NOINLINE int send_caller_stack(uint32_t epoch){
 struct zmk_hid_mouse_report_body source=global_mouse.body;
 int err=bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,epoch);
 volatile uint8_t *overwrite=(volatile uint8_t *)&source;
 for(size_t i=0;i<sizeof(source);i++){overwrite[i]=0xEEU;}
 clobber_stack();return err;
}
static void test_snapshot_lifetime(void){
 fixture_init();
 global_mouse.body.buttons=0x15;
 assert(send_caller_stack(bbp9981_usb_hid_connection_epoch())==0&&dma.pending);
 assert(((uintptr_t)dma.source%sizeof(uint32_t))==0U);
 /* Neither the returned caller frame, global report nor another stack frame
  * owns the accepted interrupt-IN source. Only completion consumes it. */
 memset(&global_mouse,0x7B,sizeof(global_mouse));clobber_stack();
 complete_dma();assert_snapshot_wire(0x15);
 assert(hid_sem.value==1);
 puts("PASS snapshot lifetime: caller overwrite/return + global overwrite + stack clobber before deferred consumption");
}
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
static void switch_stock_to_boot(void){zmk_usb_hid_set_protocol(HID_PROTOCOL_BOOT);}
#endif
static void test_snapshot_protocol_wait(void){
 fixture_init();
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
 struct zmk_hid_mouse_report_body source=global_mouse.body;
 unsigned before_writes=writes,before_takes=takes,before_gives=gives;
 during_wait=switch_stock_to_boot;
 assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,bbp9981_usb_hid_connection_epoch())==-ENOTSUP);
 assert(writes==before_writes&&!dma.pending&&takes==before_takes+1U&&gives==before_gives+1U&&hid_sem.value==1);
 zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
 assert(bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(&source,bbp9981_usb_hid_connection_epoch())==0);
 complete_dma();assert_snapshot_wire(1);
 puts("PASS snapshot protocol wait: REPORT -> BOOT rejects ID3, no write, exactly our permit returned");
#else
 puts("PASS snapshot protocol wait: BOOT disabled at compile time (no transition exists)");
#endif
}
#ifndef BBP_USB_SNAPSHOT_NO_MAIN
int main(int argc,char **argv){
 const char *mode=argc>1?argv[1]:"normal";
 if(!strcmp(mode,"normal")){test_sdk_existing_contract();}
 else if(!strcmp(mode,"lifetime")){test_snapshot_lifetime();}
 else if(!strcmp(mode,"protocol-wait")){test_snapshot_protocol_wait();}
 else {assert(!"Unknown SDK snapshot case");}
 assert(!irq_depth);return 0;
}
#endif
