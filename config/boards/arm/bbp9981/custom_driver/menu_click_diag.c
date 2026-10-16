/* SPDX-License-Identifier: MIT -- event snapshots only; never log inside IRQ lock. */
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <dt-bindings/zmk/outputs.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include "menu_click_diag.h"
#include "menu_diag_core.h"
LOG_MODULE_REGISTER(menu_diag, LOG_LEVEL_INF);
static struct md_ring ring;
static uint32_t deadline;
static bool armed;
static bool aa_down;
static uint32_t aa_since;
static int32_t previous_route=-1,previous_profile=-1;
static struct k_work_delayable drain_work;

bool bbp_menu_diag_active(void) {
 uint32_t now=k_uptime_get_32();unsigned key=irq_lock();
 bool active=armed&&(int32_t)(deadline-now)>0;irq_unlock(key);return active;
}
void bbp_menu_diag_arm(void) {
 uint32_t now=k_uptime_get_32();unsigned key=irq_lock();
 deadline=now+30000U;armed=true;irq_unlock(key);
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

/* v0.3.0 exposes the current mapped layer binding, NOT the keymap listener's
 * eventual dispatch/layer captured for release. Sample only on a gated Q down.
 * This is an available active-layer candidate; hold-tap/event-listener ordering
 * cannot be proven with this API. Unknown/missing/transparent stays -1 rather
 * than inventing a successful OUT_BLE invocation. kind12 independently shows
 * whether stock select/toggle was actually called. No behavior is invoked. */
static void q_binding_sample(void) {
 zmk_keymap_layer_index_t index=zmk_keymap_highest_layer_active();
 zmk_keymap_layer_id_t layer=zmk_keymap_layer_index_to_id(index);
 int32_t is_ble=-1,out=-1;
 if(layer!=ZMK_KEYMAP_LAYER_ID_INVAL){
  const struct zmk_behavior_binding *binding=zmk_keymap_get_layer_binding_at_idx(layer,7);
  if(binding&&binding->behavior_dev&&zmk_behavior_get_binding(binding->behavior_dev)){
#if DT_NODE_EXISTS(DT_NODELABEL(out))
   if(strcmp(binding->behavior_dev,DEVICE_DT_NAME(DT_NODELABEL(out)))==0){
    out=(int32_t)binding->param1;is_ble=binding->param1==OUT_BLE?1:0;
   }else
#endif
   {
#if DT_NODE_EXISTS(DT_NODELABEL(trans))
    if(strcmp(binding->behavior_dev,DEVICE_DT_NAME(DT_NODELABEL(trans)))!=0)
#endif
     is_ble=0;
   }
  }
 }
 bbp_menu_diag_note(MD_Q_BINDING,7,layer==ZMK_KEYMAP_LAYER_ID_INVAL?-1:(int32_t)layer,is_ble,out);
}
static int position_event(const zmk_event_t *eh) {
 const struct zmk_position_state_changed *e=as_zmk_position_state_changed(eh);
 if(!e)return ZMK_EV_EVENT_BUBBLE;
 uint32_t now=k_uptime_get_32();
 unsigned key=irq_lock();
 if(e->position==41U){
  if(e->state&&!aa_down)aa_since=now;
  aa_down=e->state;
 }
 bool gated_q=e->position==7U&&aa_down&&now-aa_since<MD_Q_WINDOW_MS;
 irq_unlock(key);
 if(e->position!=0U&&e->position!=3U&&e->position!=4U&&e->position!=41U&&!gated_q)
  return ZMK_EV_EVENT_BUBBLE;
 bbp_menu_diag_arm();
 /* SDK reads are outside the ring lock, even while the selected route is USB. */
 bbp_menu_diag_note(MD_POS,(int32_t)e->position,e->state,
                    (int32_t)zmk_endpoints_selected().transport,zmk_keymap_highest_layer_active());
 if(gated_q&&e->state)q_binding_sample();
 return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(menu_diag_position,position_event);
ZMK_SUBSCRIPTION(menu_diag_position,zmk_position_state_changed);

static int endpoint_event(const zmk_event_t *eh) {
 const struct zmk_endpoint_changed *e=as_zmk_endpoint_changed(eh);
 if(!e)return ZMK_EV_EVENT_BUBBLE;
 int32_t route=(int32_t)e->endpoint.transport;
 int32_t profile=route==ZMK_TRANSPORT_BLE?e->endpoint.ble.profile_index:-1;
 uint32_t now=k_uptime_get_32();unsigned key=irq_lock();
 (void)md_push(&ring,now,MD_ENDPOINT,route,profile,previous_route,previous_profile);
 previous_route=route;previous_profile=profile;irq_unlock(key);
 return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(menu_diag_endpoint,endpoint_event);
ZMK_SUBSCRIPTION(menu_diag_endpoint,zmk_endpoint_changed);

static void drain(struct k_work *work) {
 (void)work;
 static uint32_t last_heartbeat;
 static bool have_heartbeat;
 uint32_t now=k_uptime_get_32();
 if(!have_heartbeat||now-last_heartbeat>=2000U){
  /* This mandatory external symbol is the endpoints preinclude link guard.
   * Snapshot can query BLE/USB SDK state: NEVER take the ring lock around it. */
  struct md_endpoint_snapshot snapshot=bbp_menu_diag_endpoints_snapshot();
  bool active=bbp_menu_diag_active();unsigned key=irq_lock();
  bool pushed=md_push_heartbeats(&ring,now,snapshot,active);irq_unlock(key);
  if(pushed){last_heartbeat=now;have_heartbeat=true;}
 }
 struct md_event e;
 for(unsigned i=0;i<4U;i++) {
  unsigned key=irq_lock();bool have=md_pop(&ring,&e);irq_unlock(key);if(!have)break;
  LOG_INF("MDIAG v=2 build=" MD_BUILD_ID " t=%" PRIu32 " seq=%" PRIu32
          " k=%u a=%" PRId32 " b=%" PRId32 " c=%" PRId32 " d=%" PRId32 " crc=%08" PRIx32,
          e.at,e.seq,(unsigned)e.kind,e.a,e.b,e.c,e.d,md_crc(&e));
 }
 k_work_reschedule(&drain_work,K_MSEC(bbp_menu_diag_active()?25:250));
}
static int diag_init(void) {
 if(bbp_menu_diag_matrix_ready()!=1)return -ENODEV;
 /* Query outside lock; no route selection, clearing or settings activity. */
 struct zmk_endpoint_instance endpoint=zmk_endpoints_selected();
 previous_route=(int32_t)endpoint.transport;
 previous_profile=endpoint.transport==ZMK_TRANSPORT_BLE?endpoint.ble.profile_index:-1;
 bbp_menu_diag_arm();k_work_init_delayable(&drain_work,drain);
 k_work_reschedule(&drain_work,K_MSEC(25));return 0;
}
SYS_INIT(diag_init,APPLICATION,CONFIG_APPLICATION_INIT_PRIORITY);
