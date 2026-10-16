/* Compile the actual v0.3.0 endpoint TU with the source preinclude in effect.
 * Fault mode below is deliberately a separate MOCK, not the SDK fixture. */
#include "menu_endpoints_mocks/menu_endpoints_test_support.h"
#include <dt-bindings/zmk/outputs.h>
#include "../config/boards/arm/bbp9981/custom_driver/menu_diag_core.h"
static struct md_event observations[64];
static unsigned observation_count,arm_calls;
void bbp_menu_diag_arm(void){assert(irq_depth==0U);arm_calls++;}
void bbp_menu_diag_note(uint8_t kind,int32_t a,int32_t b,int32_t c,int32_t d){
 assert(irq_depth==0U&&observation_count<64U);
 observations[observation_count++]=(struct md_event){.at=fake_now,.kind=kind,.a=a,.b=b,.c=c,.d=d};
}
#include "../config/boards/arm/bbp9981/custom_driver/menu_diag_endpoints.h"
#if defined(MD_TEST_WRAPPER_FAILURE)
/* Nonzero result passthrough cannot be produced by stock v0.3.0 select (always
 * returns zero), so this explicitly labelled stand-in tests only wrapper ABI. */
static unsigned mock_select_calls,mock_toggle_calls;
int bbp9981_stock_zmk_endpoints_select_transport(enum zmk_transport transport){
 (void)transport;mock_select_calls++;return -EIO;
}
int bbp9981_stock_zmk_endpoints_toggle_transport(void){mock_toggle_calls++;return -ENOTSUP;}
#else
#include "sdk_v0_3_0_endpoints.c"
#endif
/* Public tests must call the wrappers, not the internal stock macro aliases. */
#undef zmk_endpoints_select_transport
#undef zmk_endpoints_toggle_transport
static void pair(enum zmk_transport request,int result,int before,int after){
 assert(observation_count==2U);
 struct md_event a=observations[0],b=observations[1];
 assert(a.kind==MD_ROUTE_REQUEST&&a.a==(int32_t)request&&a.b==0&&a.c==0&&a.d==before);
 assert(b.kind==MD_ROUTE_REQUEST&&b.a==(int32_t)request&&b.b==result&&b.c==1&&b.d==after);
 observation_count=0;
}
int main(void){
 assert(ZMK_TRANSPORT_USB==0&&ZMK_TRANSPORT_BLE==1&&OUT_BLE==2);
#if defined(MD_TEST_WRAPPER_FAILURE)
 current_instance.transport=ZMK_TRANSPORT_USB;preferred_transport=ZMK_TRANSPORT_USB;
 assert(zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE)==-EIO);
 pair(ZMK_TRANSPORT_BLE,-EIO,0,0);assert(mock_select_calls==1U);
 assert(zmk_endpoints_toggle_transport()==-ENOTSUP);
 pair(ZMK_TRANSPORT_BLE,-ENOTSUP,0,0);assert(mock_toggle_calls==1U&&mock_select_calls==1U);
 assert(arm_calls==2U&&settings_saves==0U&&work_schedule_calls==0U&&hid_clear_calls==0U&&usb_sends==0U&&ble_sends==0U);
 puts("PASS endpoint wrapper ABI fault MOCK: nonzero results unchanged, stock exactly once, no extra settings/clear/send; not a real SDK fault or radio test");
#else
 /* Initialization comes from stock source, proving no extra route initialization. */
 fake_usb_ready=true;fake_ble_ready=false;fake_ble_index=3;
 assert(zmk_endpoints_init()==0);assert(current_instance.transport==ZMK_TRANSPORT_USB&&preferred_transport==ZMK_TRANSPORT_USB);
 unsigned schedules=work_schedule_calls;
 struct md_endpoint_snapshot s=bbp_menu_diag_endpoints_snapshot();
 assert(s.selected==0&&s.preferred==0&&s.ready_bits==1&&s.active_ble_index==3);
 assert(work_schedule_calls==schedules&&settings_saves==0U&&hid_clear_calls==0U&&observation_count==0U);
 /* Requested BLE isn't ready: preferred changes but true selected stays USB. */
 assert(zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE)==0);pair(ZMK_TRANSPORT_BLE,0,0,0);
 assert(stock_select_calls==1U&&arm_calls==1U&&endpoint_changes==0U&&hid_clear_calls==0U);
 s=bbp_menu_diag_endpoints_snapshot();assert(s.selected==0&&s.preferred==1&&s.ready_bits==1&&s.active_ble_index==3);
#if CONFIG_SETTINGS
 assert(work_schedule_calls==schedules+1U&&endpoints_save_work.schedules==1U&&settings_saves==0U);
 endpoints_save_work.handler(&endpoints_save_work.work);assert(settings_saves==1U&&saved_preference==ZMK_TRANSPORT_BLE);
#else
 assert(work_schedule_calls==schedules&&settings_saves==0U);
#endif
 unsigned saved=settings_saves;
 schedules=work_schedule_calls;
 /* Repeating the same preference is stock no-op, but still two observation phases. */
 assert(zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE)==0);pair(ZMK_TRANSPORT_BLE,0,0,0);
 assert(stock_select_calls==2U&&work_schedule_calls==schedules&&settings_saves==saved&&hid_clear_calls==0U);
 /* Stock endpoint listener, not our polling, changes route once BLE is ready. */
 fake_ble_ready=true;zmk_event_t event={0};assert(endpoint_listener(&event)==0);
 assert(current_instance.transport==ZMK_TRANSPORT_BLE&&current_instance.ble.profile_index==3);
 assert(endpoint_changes==1U&&hid_clear_calls==3U&&usb_sends==2U&&ble_sends==0U);
 unsigned clears=hid_clear_calls,sends=usb_sends+ble_sends,changes=endpoint_changes;
 for(unsigned n=0;n<4U;n++){
  s=bbp_menu_diag_endpoints_snapshot();assert(s.selected==1&&s.preferred==1&&s.ready_bits==3&&s.active_ble_index==3);
 }
 assert(hid_clear_calls==clears&&usb_sends+ble_sends==sends&&endpoint_changes==changes&&work_schedule_calls==schedules&&settings_saves==saved);
 /* Both transports ready: public toggle calls stock toggle/internal stock select
  * only once, observes only one pair, and preserves stock clear/send behavior. */
 assert(zmk_endpoints_toggle_transport()==0);pair(ZMK_TRANSPORT_USB,0,1,0);
 assert(stock_select_calls==3U&&arm_calls==3U&&endpoint_changes==2U&&hid_clear_calls==6U);
 assert(usb_sends==2U&&ble_sends==2U);
#if CONFIG_SETTINGS
 assert(work_schedule_calls==schedules+1U);
#else
 assert(work_schedule_calls==schedules);
#endif
 assert(settings_saves==saved);s=bbp_menu_diag_endpoints_snapshot();assert(s.selected==0&&s.preferred==0&&s.ready_bits==3);
 /* Stock send functions still dispatch on current_instance and return errors. */
 usb_send_result=-EPIPE;assert(zmk_endpoints_send_report(HID_USAGE_KEY)==-EPIPE);
 assert(zmk_endpoints_send_report(HID_USAGE_CONSUMER)==-EPIPE&&zmk_endpoints_send_mouse_report()==-EPIPE);
 assert(zmk_endpoints_send_report(UINT16_MAX)==-ENOTSUP);
 assert(zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE)==0);pair(ZMK_TRANSPORT_BLE,0,0,1);
 ble_send_result=-EIO;assert(zmk_endpoints_send_report(HID_USAGE_KEY)==-EIO);
 assert(zmk_endpoints_send_report(HID_USAGE_CONSUMER)==-EIO&&zmk_endpoints_send_mouse_report()==-EIO);
 /* Same-TU proof: mutate stock TU-local objects directly; snapshot sees both,
  * including a selected route deliberately unlike preferred. No mirror state. */
 current_instance.transport=ZMK_TRANSPORT_USB;preferred_transport=ZMK_TRANSPORT_BLE;
 fake_ble_ready=false;fake_ble_index=4;fake_usb_ready=false;
 clears=hid_clear_calls;sends=usb_sends+ble_sends;schedules=work_schedule_calls;saved=settings_saves;
 s=bbp_menu_diag_endpoints_snapshot();assert(s.selected==0&&s.preferred==1&&s.ready_bits==0&&s.active_ble_index==4);
 assert(hid_clear_calls==clears&&usb_sends+ble_sends==sends&&work_schedule_calls==schedules&&settings_saves==saved);
 assert(usb_ready_reads>0U&&ble_ready_reads>0U&&ble_index_reads>0U&&irq_depth==0U);
 puts("PASS actual pinned endpoints.c + real enum header: same-TU statics, selected vs preferred, readiness/index, stock no-op/settings/toggle/send semantics, snapshots without mutations or IRQ-locked SDK reads; host mocks only");
#endif
 return 0;
}
