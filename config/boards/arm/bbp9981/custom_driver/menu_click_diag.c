/* SPDX-License-Identifier: MIT -- event snapshots only; never log inside IRQ lock. */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include "menu_click_diag.h"
#include "menu_diag_core.h"
LOG_MODULE_REGISTER(menu_diag, LOG_LEVEL_INF);
static struct md_ring ring;
static uint32_t deadline;
static bool armed;
static struct k_work_delayable drain_work;
bool bbp_menu_diag_active(void) {
 uint32_t now=k_uptime_get_32();unsigned key=irq_lock();
 bool active=armed&&(int32_t)(deadline-now)>0;irq_unlock(key);return active;
}
void bbp_menu_diag_arm(void) {
 unsigned key=irq_lock();deadline=k_uptime_get_32()+30000U;armed=true;irq_unlock(key);
}
void bbp_menu_diag_note(uint8_t kind,int32_t a,int32_t b,int32_t c,int32_t d) {
 if(!bbp_menu_diag_active())return;
 uint32_t now=k_uptime_get_32();unsigned key=irq_lock();
 (void)md_push(&ring,now,kind,a,b,c,d);irq_unlock(key);
}
void bbp_menu_diag_tx(uint8_t kind,uint8_t buttons,int result,unsigned outstanding) {
 static uint8_t previous[16];static int previous_result[16];static bool valid[16];static uint32_t sample[16];
 if(kind>=16U||!bbp_menu_diag_active())return;
 uint32_t now=k_uptime_get_32();unsigned key=irq_lock();
 bool record=!valid[kind]||previous[kind]!=buttons||previous_result[kind]!=result||result!=0||now-sample[kind]>=1000U;
 if(record){valid[kind]=true;previous[kind]=buttons;previous_result[kind]=result;sample[kind]=now;
  (void)md_push(&ring,now,kind,buttons,result,(int32_t)outstanding,0);}
 irq_unlock(key);
}
static int position_event(const zmk_event_t *eh) {
 const struct zmk_position_state_changed *e=as_zmk_position_state_changed(eh);
 if(!e||(e->position!=0U&&e->position!=3U&&e->position!=4U))return ZMK_EV_EVENT_BUBBLE;
 bbp_menu_diag_arm();
 bbp_menu_diag_note(MD_POS,(int32_t)e->position,e->state,
                    (int32_t)zmk_endpoints_selected().transport,zmk_keymap_highest_layer_active());
 return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(menu_diag_position,position_event);
ZMK_SUBSCRIPTION(menu_diag_position,zmk_position_state_changed);
static void drain(struct k_work *work) {
 (void)work;struct md_event e;
 for(unsigned i=0;i<16U;i++) {
  unsigned key=irq_lock();bool have=md_pop(&ring,&e);irq_unlock(key);if(!have)break;
  LOG_INF("MDIAG v=1 t=%u k=%u a=%d b=%d c=%d d=%d seq=%u",e.at,e.kind,e.a,e.b,e.c,e.d,e.seq);
 }
 static uint32_t last_status;uint32_t now=k_uptime_get_32();
 if(now-last_status>=2000U){
  last_status=now;unsigned key=irq_lock();uint32_t drops=ring.dropped;unsigned queued=ring.count;irq_unlock(key);
  enum zmk_transport route=zmk_endpoints_selected().transport;
  LOG_INF("MDIAG status build=tp2_mem_20261016 t=%u transport=%s route=%d drop=%u queued=%u",now,
   route==ZMK_TRANSPORT_BLE?"BLE":"USB",(int)route,drops,queued);
 }
 k_work_reschedule(&drain_work,K_MSEC(bbp_menu_diag_active()?100:500));
}
static int diag_init(void) {
 if(bbp_menu_diag_matrix_ready()!=1)return -ENODEV;
 bbp_menu_diag_arm();k_work_init_delayable(&drain_work,drain);
 k_work_reschedule(&drain_work,K_MSEC(100));return 0;
}
SYS_INIT(diag_init,APPLICATION,CONFIG_APPLICATION_INIT_PRIORITY);
