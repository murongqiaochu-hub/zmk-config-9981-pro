/* SPDX-License-Identifier: Apache-2.0 -- actual policy + independent conservation ledger.
 * Host logic only: neither physical contact accuracy nor host HID acceptance. */
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap.h"
static uint64_t checks,steps;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d %s\n",__func__,__LINE__,#x);exit(1);}}while(0)
struct fixture {
 struct tp_microtap_state s;struct tp_microtap_frame ledger[16];unsigned n;
 uint64_t input,replayed,discarded,clicks;int64_t ix,iy,rx,ry,tx,ty;
 bool have_click;uint32_t last_click;
};
static void init(struct fixture *f){*f=(struct fixture){0};tp_microtap_reset(&f->s);}
static struct tp_microtap_result feed(struct fixture *f,uint32_t t,int16_t x,int16_t y,bool valid,bool enabled,bool blocked){
 struct tp_microtap_state before=f->s;
 bool bad=before.have_time&&(uint32_t)(t-before.last_poll_ms)>100U;
 struct tp_microtap_result r=tp_microtap_step(&f->s,t,x,y,valid,enabled,blocked);
 steps++;CHECK(r.count<=4U&&f->s.count<=3U);
 if(!valid||!enabled||blocked||bad)CHECK(!r.click);
 if(bad&&(uint32_t)(t-before.last_poll_ms)>=UINT32_C(0x80000000))f->have_click=false;
 if(r.click){
  CHECK(before.count>=1U&&before.count<=3U&&f->n==before.count);
  CHECK(before.path>=3U&&before.path<=512U&&before.max_offset<=256U);
  CHECK((uint32_t)(t-before.last_motion_ms)>=60U&&(uint32_t)(t-before.first_ms)<=120U);
  CHECK((uint32_t)(before.last_motion_ms-before.first_ms)<=40U);
  if(f->have_click)CHECK((uint32_t)(t-f->last_click)>=200U);
  f->have_click=true;f->last_click=t;
  for(unsigned j=0;j<f->n;j++){f->tx+=f->ledger[j].dx;f->ty+=f->ledger[j].dy;}
  f->discarded+=f->n;f->clicks++;f->n=0;
 }
 if(valid&&(x!=0||y!=0)){
  CHECK(f->n<16U);f->ledger[f->n++]=(struct tp_microtap_frame){t,x,y};
  f->input++;f->ix+=x;f->iy+=y;
 }
 for(unsigned i=0;i<r.count;i++){
  CHECK(f->n>0U&&r.frames[i].at_ms==f->ledger[0].at_ms);
  CHECK(r.frames[i].dx==f->ledger[0].dx&&r.frames[i].dy==f->ledger[0].dy);
  f->rx+=r.frames[i].dx;f->ry+=r.frames[i].dy;f->replayed++;f->n--;
  for(unsigned j=0;j<f->n;j++)f->ledger[j]=f->ledger[j+1U];
 }
 CHECK(f->n==f->s.count&&tp_microtap_pending(&f->s)==(f->n!=0U));
 int64_t px=0,py=0;for(unsigned j=0;j<f->n;j++){px+=f->ledger[j].dx;py+=f->ledger[j].dy;}
 CHECK(f->input==f->replayed+f->discarded+f->n);
 CHECK(f->ix==f->rx+f->tx+px&&f->iy==f->ry+f->ty+py);
 return r;
}
static struct tp_microtap_result packet(struct fixture *f,uint32_t t,int16_t x,int16_t y){return feed(f,t,x,y,true,true,false);}
static void zeros(struct fixture *f,uint32_t base,unsigned n){for(unsigned i=1;i<=n;i++)(void)packet(f,base+i*10U,0,0);}
static void shifted_range_and_first_latency(void){
 struct fixture f;init(&f);
 (void)packet(&f,0,-1,-1);zeros(&f,0,6);CHECK(f.clicks==0U&&f.replayed==1U);
 init(&f);(void)packet(&f,0,3,0);zeros(&f,0,5);CHECK(f.clicks==0U);
 (void)packet(&f,60,0,0);CHECK(f.clicks==1U&&f.discarded==1U); /* Not 260ms. */
 init(&f);(void)packet(&f,0,-128,-128);(void)packet(&f,10,-128,-128);zeros(&f,10,6);
 CHECK(f.clicks==1U&&f.discarded==2U); /* Exact path512/offset256, beyond old caps. */
 init(&f);(void)packet(&f,0,-128,-128);(void)packet(&f,10,127,127);(void)packet(&f,20,2,1);
 CHECK(f.replayed==3U);zeros(&f,20,6);CHECK(f.clicks==0U); /* path513 */
 init(&f);(void)packet(&f,0,-128,0);(void)packet(&f,10,-128,0);(void)packet(&f,20,-1,0);
 CHECK(f.replayed==3U);zeros(&f,20,6);CHECK(f.clicks==0U); /* offset257 */
 puts("PASS shifted path3..512/offset256, reject weak paths1/2, exact widened caps, first click remains60ms");
}
static void frame_span_and_navigation(void){
 struct fixture f;
 for(uint32_t span=0;span<=45U;span++){
  init(&f);(void)packet(&f,0,1,0);(void)packet(&f,span,2,0);zeros(&f,span,6);
  CHECK(f.clicks==(span<=40U?1U:0U));CHECK(f.discarded==(span<=40U?2U:0U));
 }
 init(&f);(void)packet(&f,0,1,0);(void)packet(&f,20,1,0);(void)packet(&f,40,1,0);zeros(&f,40,6);
 CHECK(f.clicks==1U&&f.discarded==3U);
 init(&f);(void)packet(&f,0,1,0);(void)packet(&f,10,1,0);(void)packet(&f,20,1,0);
 struct tp_microtap_result r=packet(&f,30,1,0);CHECK(r.count==4U&&f.replayed==4U);
 for(unsigned i=0;i<4U;i++)CHECK(r.frames[i].at_ms==i*10U);
 (void)packet(&f,100,3,0);zeros(&f,100,19);(void)packet(&f,290,3,0);zeros(&f,290,20);
 CHECK(f.clicks==0U); /* Rejected navigation tails never rearm early. */
 (void)packet(&f,500,3,0);zeros(&f,500,6);CHECK(f.clicks==1U);
 init(&f);(void)packet(&f,0,1,0);(void)packet(&f,40,2,0);r=packet(&f,120,0,0);CHECK(r.click);
 init(&f);(void)packet(&f,0,1,0);(void)packet(&f,40,2,0);r=packet(&f,121,0,0);CHECK(!r.click&&r.count==2U);
 puts("PASS 1..3 frames/span40 inclusive, fourth/span41 reject, ordered4-frame replay, 200ms navigation quiet, age120 inclusive");
}
static void repeat_guard_and_no_deferred_click(void){
 struct fixture f;init(&f);
 for(uint32_t i=0;i<1000U;i++){
  (void)packet(&f,i*240U,5,-12);zeros(&f,i*240U,23);
 }
 CHECK(f.clicks==1000U&&f.discarded==1000U&&f.replayed==0U);
 for(uint32_t delta=199;delta<=200;delta++){
  init(&f);(void)packet(&f,0,3,0);zeros(&f,0,19);(void)packet(&f,delta,3,0);zeros(&f,delta,6);
  CHECK(f.clicks==(delta==200U?2U:1U));
 }
 init(&f);
 for(uint32_t i=0;i<1000U;i++){(void)packet(&f,i*70U,5,-12);zeros(&f,i*70U,6);}
 CHECK(f.clicks==1U&&f.discarded==1U&&f.replayed==999U);
 uint32_t last=999U*70U;zeros(&f,last+60U,20);CHECK(f.clicks==1U); /* No catch-up click. */
 (void)packet(&f,last+270U,3,0);zeros(&f,last+270U,6);CHECK(f.clicks==2U);
 init(&f);(void)packet(&f,0,1,0);(void)packet(&f,40,2,0);(void)packet(&f,120,0,0);
 CHECK(f.clicks==1U);zeros(&f,120,11);(void)packet(&f,240,3,0);zeros(&f,240,6);
 CHECK(f.clicks==1U&&f.replayed==1U); /* candidate completes only180ms after late first click. */
 zeros(&f,300,10);CHECK(f.clicks==1U);
 puts("PASS 1000 deliberate taps240ms apart, 199/200ms rearm, 1000 rebound pulses yield one click, no delayed second click, late-first explicit decision interval");
}
static void extremes_and_cancellation(void){
 struct fixture f;
 for(int x=-128;x<=127;x++)for(int y=-128;y<=127;y++){
  init(&f);(void)packet(&f,0,(int16_t)x,(int16_t)y);zeros(&f,0,6);
  int path=(x<0?-x:x)+(y<0?-y:y);CHECK(f.clicks==(path>=3?1U:0U));
 }
 for(int v=INT16_MIN;v<=INT16_MAX;v++){
  init(&f);(void)packet(&f,0,(int16_t)v,0);zeros(&f,0,6);
  bool eligible=(v<=-3&&v>=-256)||(v>=3&&v<=256);CHECK(f.clicks==(eligible?1U:0U));
 }
 init(&f);(void)packet(&f,0,3,0);(void)feed(&f,60,0,0,false,true,false);CHECK(f.clicks==0&&f.replayed==1);
 init(&f);(void)packet(&f,0,3,0);(void)feed(&f,60,0,0,true,true,true);CHECK(f.clicks==0&&f.replayed==1);
 init(&f);(void)packet(&f,0,3,0);(void)packet(&f,101,0,0);CHECK(f.clicks==0&&f.replayed==1);
 init(&f);(void)packet(&f,100,3,0);(void)packet(&f,90,0,0);CHECK(f.clicks==0&&f.replayed==1);
 init(&f);uint32_t base=UINT32_MAX-25U;(void)packet(&f,base,3,0);zeros(&f,base,19);
 (void)packet(&f,base+200U,3,0);zeros(&f,base+200U,6);CHECK(f.clicks==2U);
 init(&f);for(unsigned i=0;i<1000U;i++)(void)feed(&f,i*10U,1,-1,true,false,false);
 CHECK(f.replayed==1000U&&f.clicks==0U);
 puts("PASS all65536 int8 XY and65536 int16 axes, unknown/blocked/disabled transparency, gap/rollback, click interval across uptime rollover");
}
static uint32_t rng=UINT32_C(0x31415926);
static uint32_t random_u32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void randomized_ledger(void){
 struct fixture f;init(&f);uint32_t t=UINT32_MAX-200U;
 for(unsigned i=0;i<100000U;i++){
  uint32_t r=random_u32();t+=r&31U;if(i%997U==0U)t+=101U;
  int16_t x=(int16_t)((int32_t)((r>>8)%65U)-32),y=(int16_t)((int32_t)((r>>16)%65U)-32);
  if((r&7U)==0U)x=y=0;
  (void)feed(&f,t,x,y,(r&0x100U)!=0U,(r&0x200U)!=0U,(r&0x400U)==0U);
 }
 printf("PASS ordered100000 ledger: input=%" PRIu64 " replay=%" PRIu64 " discarded=%" PRIu64 " clicks=%" PRIu64 "\n",f.input,f.replayed,f.discarded,f.clicks);
}
int main(void){
 shifted_range_and_first_latency();frame_span_and_navigation();repeat_guard_and_no_deferred_click();extremes_and_cancellation();randomized_ledger();
 printf("PASS policy: %" PRIu64 " steps, %" PRIu64 " checks; no physical tap accuracy claim\n",steps,checks);return 0;
}
