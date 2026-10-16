#define CONFIG_BBP9981_MENU_DIAG 1
#include "test_env.h"
static unsigned irq_depth,log_calls;
static unsigned irq_lock(void){return irq_depth++;}
static void irq_unlock(unsigned key){assert(irq_depth==key+1U);irq_depth--;}
static void audit_log(const char *fmt,...){(void)fmt;assert(irq_depth==0U);log_calls++;}
#undef LOG_INF
#define LOG_INF(...) audit_log(__VA_ARGS__)
#include "../config/boards/arm/bbp9981/custom_driver/menu_click_diag.c"
int main(void){
 assert(diag_init()==0&&bbp_menu_diag_active());
 for(unsigned i=0;i<70U;i++)bbp_menu_diag_note(MD_HID,1,0,1,8<<16);
 assert(ring.count==64U&&ring.dropped==6U);
 for(unsigned i=0;i<4U;i++)drain(&drain_work.work);
 assert(ring.count==0U&&log_calls==64U);
 fake_now=31000;assert(!bbp_menu_diag_active());
 bbp_menu_diag_note(MD_HID,1,0,1,0);assert(ring.count==0U);
 zmk_event_t e={.valid=true,.data={.position=3,.state=true}};
 assert(position_event(&e)==0&&bbp_menu_diag_active()&&ring.count==1U);
 e.data.position=7;position_event(&e);assert(ring.count==1U);
 bbp_menu_diag_tx(MD_NOTIFY,1,-EIO,2);bbp_menu_diag_tx(MD_NOTIFY,1,0,1);
 assert(ring.count==3U); /* A recovered success is not deduplicated after an error. */
 unsigned before=ring.count;bbp_menu_diag_tx(MD_NOTIFY,1,0,1);assert(ring.count==before);
 fake_now=UINT32_MAX-20U;bbp_menu_diag_arm();assert(bbp_menu_diag_active());
 fake_now=10;assert(bbp_menu_diag_active());fake_now=30000;assert(!bbp_menu_diag_active());
 struct md_ring r={0};struct md_event out;
 for(unsigned n=0;n<2000U;n++){
  assert(md_push(&r,n*1000U,MD_RAW,(int32_t)n,0,0,0));
  assert(md_pop(&r,&out)&&out.a==(int32_t)n&&!md_pop(&r,&out));
 }
 r=(struct md_ring){0};
 for(unsigned n=0;n<100U;n++)(void)md_push(&r,0,MD_RAW,(int32_t)n,0,0,0);
 assert(r.count==32U&&r.dropped==68U);
 for(unsigned n=0;n<64U;n++)assert(md_push(&r,0,MD_HID,(int32_t)n,0,0,0));
 assert(r.count==96U); /* Noisy raw scans cannot consume the 64 critical-event credits. */
 r=(struct md_ring){0};for(unsigned n=0;n<128U;n++)assert(md_push(&r,n*1000U,MD_RAW,(int32_t)n,0,0,0));
 assert(!md_push(&r,129000,MD_RAW,0,0,0,0)&&r.dropped==1U);
 for(unsigned n=0;n<128U;n++)assert(md_pop(&r,&out)&&out.a==(int32_t)n);
 puts("PASS diagnostic bounds: FIFO128, 64/sec rate/drop, IRQ-safe no logging in critical section, position privacy filter, recovery result, 30s gate/wrap; no transport or settings mutation");
 return 0;
}
