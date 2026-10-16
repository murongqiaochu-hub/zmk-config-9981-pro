/* Actual diagnostic worker + isolated host mocks; no shared test_env changes. */
#include "menu_endpoints_mocks/menu_endpoints_test_support.h"
#include <zmk/endpoints.h>
#include "../config/boards/arm/bbp9981/custom_driver/menu_click_diag.h"
static unsigned snapshot_calls;
static int matrix_ready=1;
static struct zmk_endpoint_instance fake_selected={.transport=ZMK_TRANSPORT_USB};
int bbp_menu_diag_matrix_ready(void){return matrix_ready;}
struct zmk_endpoint_instance zmk_endpoints_selected(void){assert(irq_depth==0U);return fake_selected;}
#if !defined(MD_TEST_NO_ENDPOINT_SNAPSHOT)
struct md_endpoint_snapshot bbp_menu_diag_endpoints_snapshot(void){
 assert(irq_depth==0U);snapshot_calls++;
 return (struct md_endpoint_snapshot){fake_selected.transport,ZMK_TRANSPORT_BLE,1,3};
}
#endif
#include "../config/boards/arm/bbp9981/custom_driver/menu_click_diag.c"

static struct md_event take(void){struct md_event e;assert(md_pop(&ring,&e));return e;}
static void empty_ring(void){ring=(struct md_ring){0};}
static void position(uint32_t p,bool down){
 zmk_event_t e={.type=MD_TEST_POSITION,.position={.position=p,.state=down,.timestamp=fake_now}};
 assert(position_event(&e)==ZMK_EV_EVENT_BUBBLE);
}
static struct md_event wire(unsigned index){
 assert(index<md_log_count);struct md_event e={0};char build[64];uint32_t crc;unsigned kind;
 int end=0;
 assert(sscanf(md_logs[index],"MDIAG v=2 build=%63s t=%" SCNu32 " seq=%" SCNu32
               " k=%u a=%" SCNd32 " b=%" SCNd32 " c=%" SCNd32 " d=%" SCNd32 " crc=%8" SCNx32 "%n",
               build,&e.at,&e.seq,&kind,&e.a,&e.b,&e.c,&e.d,&crc,&end)==9);
 assert(strcmp(build,MD_BUILD_ID)==0&&kind<=UINT8_MAX&&md_logs[index][end]=='\0');
 assert(strlen(strstr(md_logs[index]," crc=")+5)==8U);
 e.kind=(uint8_t)kind;assert(crc==md_crc(&e));return e;
}
static void q_candidate(int expected_layer,int expected_ble,int expected_param){
 empty_ring();position(7,true);assert(ring.count==2U);
 struct md_event p=take(),q=take();assert(p.kind==MD_POS&&p.a==7&&p.b==1);
 assert(q.kind==MD_Q_BINDING&&q.a==7&&q.b==expected_layer&&q.c==expected_ble&&q.d==expected_param);
}
int main(void){
 /* Published vectors were computed independently from LE bytes, not struct layout. */
 struct md_event e={0};assert(md_crc(&e)==UINT32_C(0xf5cbf9f5));
 e=(struct md_event){.at=UINT32_C(0x12345678),.seq=UINT32_C(0x9abcdef0),.kind=13,.a=7,.b=2,.c=1,.d=-1};
 assert(md_crc(&e)==UINT32_C(0xa4c3a518));
 e=(struct md_event){.at=UINT32_MAX,.seq=UINT32_MAX,.kind=15,.a=-1,.b=INT32_MIN,.c=INT32_MAX,.d=2};
 assert(md_crc(&e)==UINT32_C(0xb5fc3d08));
 assert(MD_RAW==1&&MD_BEGIN==10&&MD_ROUTE==11&&MD_ROUTE_REQUEST==12&&MD_Q_BINDING==13&&MD_ENDPOINT==14&&MD_QUALITY==15);
 matrix_ready=0;assert(diag_init()==-ENODEV);matrix_ready=1;
 assert(diag_init()==0&&bbp_menu_diag_active()&&drain_work.delay==25);
 drain(&drain_work.work);assert(md_log_count==2U&&snapshot_calls==1U&&ring.count==0U);
 struct md_event a=wire(0),b=wire(1);
 assert(a.kind==MD_ROUTE&&a.at==0U&&a.seq==1U&&a.a==0&&a.b==1&&a.c==1&&a.d==3);
 assert(b.kind==MD_QUALITY&&b.at==a.at&&b.seq==a.seq+1U&&b.a==0&&b.b==0&&b.c==1&&b.d==2);
 /* Independent, CRC-protected heartbeat continues without BLE, input or active gate. */
 fake_now=31000;assert(!bbp_menu_diag_active());drain(&drain_work.work);
 assert(md_log_count==4U&&snapshot_calls==2U&&drain_work.delay==250);
 a=wire(2);b=wire(3);assert(a.kind==11&&b.kind==15&&a.at==31000U&&b.at==a.at&&b.seq==a.seq+1U&&b.c==0);
 fake_now=32999;drain(&drain_work.work);assert(md_log_count==4U);
 fake_now=33000;drain(&drain_work.work);assert(md_log_count==6U);
 assert(wire(4).at==33000U&&wire(5).kind==15);
 /* No more than four log lines per 25ms active tick, including a full FIFO. */
 fake_now=40000;bbp_menu_diag_arm();empty_ring();md_log_count=0;
 for(unsigned i=0;i<70U;i++)bbp_menu_diag_note(MD_HID,1,0,1,8<<16);
 assert(ring.count==64U&&ring.dropped==6U);
 for(unsigned i=0;i<16U;i++){
  unsigned before=md_log_count;drain(&drain_work.work);assert(md_log_count-before==4U&&drain_work.delay==25);
 }
 assert(ring.count==0U&&md_log_count==64U);
 for(unsigned i=0;i<md_log_count;i++)assert(wire(i).kind==MD_HID);
 fake_now=41000;drain(&drain_work.work);assert(md_log_count==66U);
 a=wire(64);b=wire(65);assert(a.kind==11&&b.kind==15&&b.at==a.at&&b.seq==a.seq+1U&&b.a>0);
 /* USB selection never suppresses input. Ordinary typing Q and other positions stay private. */
 fake_now=50000;empty_ring();position(7,true);position(7,false);position(9,true);assert(ring.count==0U);
 for(unsigned i=0;i<3U;i++){uint32_t p=(uint32_t[]){0,3,4}[i];position(p,true);e=take();assert(e.kind==MD_POS&&e.a==(int32_t)p&&e.c==0);}
 fake_layer=2;position(41,true);e=take();assert(e.kind==MD_POS&&e.a==41);
 q_candidate(2,1,OUT_BLE);position(7,false);assert(take().a==7);
 position(41,false);(void)take();position(7,true);assert(ring.count==0U);
 fake_now=51000;position(41,true);(void)take();
 fake_now=55999;q_candidate(2,1,OUT_BLE);
 fake_now=56000;position(7,true);assert(ring.count==0U);
 position(41,true);(void)take();position(7,true);assert(ring.count==0U); /* Duplicate down doesn't renew window. */
 position(41,false);(void)take();fake_now=57000;position(41,true);(void)take();
 fake_binding_valid=false;q_candidate(2,-1,-1);fake_binding_valid=true;
 fake_behavior_available=false;q_candidate(2,-1,-1);fake_behavior_available=true;
 fake_binding.behavior_dev="trans";q_candidate(2,-1,-1);
 fake_binding.behavior_dev="kp";q_candidate(2,0,-1);
 fake_binding.behavior_dev="out";fake_binding.param1=OUT_USB;q_candidate(2,0,OUT_USB);
 fake_binding.param1=OUT_BLE;fake_layer=1;fake_layer_ids[1]=2;q_candidate(2,1,OUT_BLE);
 fake_layer=UINT8_MAX;q_candidate(-1,-1,-1);
 /* Endpoint change contains only route/profile, no Bluetooth addresses. */
 empty_ring();previous_route=ZMK_TRANSPORT_USB;previous_profile=-1;
 zmk_event_t changed={.type=MD_TEST_ENDPOINT,.endpoint={.endpoint={.transport=ZMK_TRANSPORT_BLE,.ble={.profile_index=4}}}};
 assert(endpoint_event(&changed)==0);e=take();assert(e.kind==14&&e.a==1&&e.b==4&&e.c==0&&e.d==-1);
 changed.endpoint.endpoint.transport=ZMK_TRANSPORT_USB;endpoint_event(&changed);e=take();assert(e.a==0&&e.b==-1&&e.c==1&&e.d==4);
 /* Existing result/recovery semantics and wrap-safe active/Q windows remain. */
 bbp_menu_diag_tx(MD_NOTIFY,1,-EIO,2);bbp_menu_diag_tx(MD_NOTIFY,1,0,1);assert(ring.count==2U);
 bbp_menu_diag_tx(MD_NOTIFY,1,0,1);assert(ring.count==2U);empty_ring();
 position(41,false);empty_ring();fake_now=UINT32_MAX-20U;bbp_menu_diag_arm();position(41,true);empty_ring();
 fake_layer=2;fake_now=10;assert(bbp_menu_diag_active());q_candidate(2,1,OUT_BLE);
 fake_now=30011;assert(!bbp_menu_diag_active());bbp_menu_diag_note(MD_HID,1,0,0,0);assert(ring.count==0U);
 /* FIFO128 / raw32/s / critical64/s, all-or-none pair and modulo sequence. */
 struct md_ring r={0};struct md_event out;
 for(unsigned n=0;n<2000U;n++){
  assert(md_push(&r,n*1000U,MD_RAW,(int32_t)n,0,0,0));
  assert(md_pop(&r,&out)&&out.a==(int32_t)n&&!md_pop(&r,&out));
 }
 r=(struct md_ring){0};for(unsigned n=0;n<100U;n++)(void)md_push(&r,0,MD_RAW,(int32_t)n,0,0,0);
 assert(r.count==32U&&r.dropped==68U);
 for(unsigned n=0;n<64U;n++)assert(md_push(&r,0,MD_HID,(int32_t)n,0,0,0));
 assert(r.count==96U);
 struct md_endpoint_snapshot ep={0,1,3,4};uint32_t seq=r.seq;
 assert(!md_push_heartbeats(&r,0,ep,false)&&r.count==96U&&r.seq==seq&&r.dropped==70U);
 r=(struct md_ring){0};for(unsigned n=0;n<128U;n++)assert(md_push(&r,n*1000U,MD_RAW,(int32_t)n,0,0,0));
 seq=r.seq;assert(!md_push_heartbeats(&r,129000,ep,true)&&r.count==128U&&r.seq==seq&&r.dropped==2U);
 for(unsigned n=0;n<128U;n++)assert(md_pop(&r,&out)&&out.a==(int32_t)n);
 r=(struct md_ring){.seq=UINT32_MAX-1U};assert(md_push_heartbeats(&r,0,ep,false));
 assert(md_pop(&r,&a)&&md_pop(&r,&b));assert(a.seq==UINT32_MAX&&b.seq==0U&&a.at==b.at&&b.b==0&&b.c==0&&b.d==2);
 assert(irq_depth==0U&&settings_saves==0U&&work_schedule_calls>0U&&lock_calls>0U);
 puts("PASS MDIAG v2: metadata/CRC fixed vectors, atomic heartbeat pair, FIFO/rates, four-line drain, USB input, bounded physical-aA Q privacy, runtime candidate unknowns, lock-external SDK/logging; host mocks only");
 return 0;
}
