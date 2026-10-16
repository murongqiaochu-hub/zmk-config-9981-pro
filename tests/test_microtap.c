/* SPDX-License-Identifier: Apache-2.0
 * Pulse-policy host tests and an independent raw-frame conservation ledger.
 * These tests do not prove physical tap recognition or host HID acceptance. */
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap.h"
static uint64_t checks,steps;
#define CHECK(x) do { checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d %s\n",__func__,__LINE__,#x);exit(1);} } while(0)
struct fixture {
    struct tp_microtap_state s;
    struct tp_microtap_frame ledger[16];
    unsigned int n;
    uint64_t input,replayed,discarded,clicks;
    int64_t ix,iy,rx,ry,tx,ty;
};
static void init(struct fixture *f) { *f=(struct fixture){0};tp_microtap_reset(&f->s); }
static struct tp_microtap_result feed(struct fixture *f,uint32_t t,int16_t x,int16_t y,
                                      bool valid,bool enabled,bool blocked) {
    struct tp_microtap_state before=f->s;
    struct tp_microtap_result r=tp_microtap_step(&f->s,t,x,y,valid,enabled,blocked);
    steps++;CHECK(r.count<=3U);CHECK(f->s.count<=2U);
    if(!valid||!enabled||blocked||(before.have_time&&(uint32_t)(t-before.last_poll_ms)>100U))CHECK(!r.click);
    if(r.click) {
        CHECK(before.count>=1U&&before.count<=2U&&f->n==before.count&&before.path>=1U&&before.path<=256U);
        CHECK(before.max_offset<=128U&&(uint32_t)(t-before.last_motion_ms)>=60U);
        CHECK((uint32_t)(t-before.first_ms)<=100U);
        CHECK((uint32_t)(before.last_motion_ms-before.first_ms)<=25U);
        for(unsigned j=0;j<f->n;j++){f->tx+=f->ledger[j].dx;f->ty+=f->ledger[j].dy;}
        f->discarded+=f->n;f->clicks++;f->n=0;
    }
    if(valid&&(x!=0||y!=0)) {
        CHECK(f->n<16U);f->ledger[f->n++]=(struct tp_microtap_frame){t,x,y};
        f->input++;f->ix+=x;f->iy+=y;
    }
    for(unsigned int i=0;i<r.count;i++) {
        CHECK(f->n>0U);CHECK(r.frames[i].at_ms==f->ledger[0].at_ms);
        CHECK(r.frames[i].dx==f->ledger[0].dx&&r.frames[i].dy==f->ledger[0].dy);
        f->rx+=r.frames[i].dx;f->ry+=r.frames[i].dy;f->replayed++;f->n--;
        for(unsigned int j=0;j<f->n;j++)f->ledger[j]=f->ledger[j+1U];
    }
    CHECK(f->n==f->s.count&&tp_microtap_pending(&f->s)==(f->n!=0U));
    int64_t px=0,py=0;
    for(unsigned j=0;j<f->n;j++){px+=f->ledger[j].dx;py+=f->ledger[j].dy;}
    CHECK(f->input==f->replayed+f->discarded+f->n);
    CHECK(f->ix==f->rx+f->tx+px&&f->iy==f->ry+f->ty+py);
    return r;
}
static struct tp_microtap_result packet(struct fixture *f,uint32_t t,int16_t x,int16_t y) {
    return feed(f,t,x,y,true,true,false);
}
static void zeros(struct fixture *f,uint32_t base,unsigned int n) {
    for(unsigned int i=1;i<=n;i++)(void)packet(f,base+i*10U,0,0);
}
static void real_tap_and_multiclick(void) {
    struct fixture f;init(&f);
    /* The two ACTUAL motion observations in old light-tap windows. */
    (void)packet(&f,0,-1,-1);zeros(&f,0,5);CHECK(f.clicks==0U);
    (void)packet(&f,60,0,0);CHECK(f.clicks==1U&&f.replayed==0U);
    (void)packet(&f,70,5,-12);zeros(&f,70,6);CHECK(f.clicks==2U);
    (void)packet(&f,140,2,0);zeros(&f,140,6);CHECK(f.clicks==3U);
    CHECK(f.replayed==0U&&f.discarded==3U);
    init(&f);
    for(unsigned int i=0;i<1000U;i++) {
        (void)packet(&f,i*70U,-1,-1);zeros(&f,i*70U,6);
    }
    CHECK(f.clicks==1000U&&f.discarded==1000U&&f.replayed==0U);
    puts("PASS recorded tap-window impulses + 1000 consecutive clicks; no post-click cooldown");
}
static void moving_not_tapping(void) {
    struct fixture f;init(&f);
    (void)packet(&f,0,2,0);struct tp_microtap_result r=packet(&f,10,1,0);
    CHECK(r.count==0U&&f.s.count==2U);
    r=packet(&f,20,1,0);
    CHECK(r.count==3U&&r.frames[0].at_ms==0U&&r.frames[1].at_ms==10U&&r.frames[2].at_ms==20U);
    zeros(&f,20,20);
    CHECK(f.clicks==0U&&f.replayed==3U);
    init(&f);
    /* A second frame even at59ms invalidates the isolated-pulse hypothesis. */
    (void)packet(&f,0,-1,-1);r=packet(&f,59,1,1);CHECK(r.count==2U&&!r.click);
    zeros(&f,59,20);CHECK(f.clicks==0U);
    init(&f);(void)packet(&f,0,129,0);zeros(&f,0,18);
    r=packet(&f,190,2,0);CHECK(r.count==1U&&!tp_microtap_pending(&f.s));
    zeros(&f,190,20);(void)packet(&f,400,-1,-1);zeros(&f,400,6);
    CHECK(f.clicks==1U); /* only after ordinary-motion quiet>=200ms */
    puts("PASS third-frame/late-second rejection, continuous micro-move/tail bypass and navigation-only guard");
}
static void two_frame_boundaries(void) {
    struct fixture f;
    for(uint32_t span=0;span<=30U;span++) {
        init(&f);(void)packet(&f,0,-1,0);(void)packet(&f,span,0,1);zeros(&f,span,6);
        CHECK(f.clicks==(span<=25U?1U:0U));
        CHECK(f.discarded==(span<=25U?2U:0U));
    }
    init(&f);(void)packet(&f,0,-128,-128);(void)packet(&f,10,1,0);zeros(&f,10,6);
    CHECK(f.clicks==0U&&f.replayed==2U);
    init(&f);(void)packet(&f,0,128,0);(void)packet(&f,10,1,0);zeros(&f,10,6);
    CHECK(f.clicks==0U&&f.replayed==2U);
    init(&f);
    for(uint32_t i=0;i<1000U;i++) {
        (void)packet(&f,i*100U,1,0);(void)packet(&f,i*100U+10U,0,1);zeros(&f,i*100U+10U,6);
    }
    CHECK(f.clicks==1000U&&f.discarded==2000U&&f.replayed==0U);
    puts("PASS two-frame span0..25 inclusive/26 reject, path/offset limits, 1000 repeated 2-frame clicks, unchanged single-frame acceptance");
}
static void boundaries_and_extremes(void) {
    struct fixture f;init(&f);
    (void)packet(&f,0,1,0);zeros(&f,0,6);CHECK(f.clicks==1U&&f.discarded==1U);
    init(&f);(void)packet(&f,0,-128,-128);zeros(&f,0,6);CHECK(f.clicks==1U);
    init(&f);(void)packet(&f,0,128,129);CHECK(f.replayed==1U);zeros(&f,0,6);CHECK(f.clicks==0U);
    init(&f);(void)packet(&f,0,129,0);CHECK(f.replayed==1U);
    for(int x=-128;x<=127;x++)for(int y=-128;y<=127;y++) {
        init(&f);(void)packet(&f,0,(int16_t)x,(int16_t)y);zeros(&f,0,6);
        CHECK(f.clicks==((x!=0||y!=0)?1U:0U));
    }
    for(int v=INT16_MIN;v<=INT16_MAX;v++) {
        init(&f);(void)packet(&f,0,(int16_t)v,0);zeros(&f,0,6);
        bool eligible=(v<=-1&&v>=-128)||(v>=1&&v<=128);
        CHECK(f.clicks==(eligible?1U:0U));
    }
    puts("PASS energy/offset/quiet boundaries and all65536 int16 impulses");
}
static void invalid_time_and_modes(void) {
    struct fixture f;init(&f);
    (void)packet(&f,0,-1,-1);(void)feed(&f,60,0,0,false,true,false);
    CHECK(f.clicks==0U&&f.replayed==1U);
    init(&f);(void)packet(&f,0,-1,-1);(void)feed(&f,60,0,0,true,true,true);
    CHECK(f.clicks==0U&&f.replayed==1U);
    init(&f);(void)packet(&f,0,-1,-1);(void)packet(&f,101,0,0);
    CHECK(f.clicks==0U&&f.replayed==1U);
    init(&f);(void)packet(&f,100,-1,-1);(void)packet(&f,90,0,0);
    CHECK(f.clicks==0U&&f.replayed==1U);
    init(&f);uint32_t base=UINT32_MAX-25U;(void)packet(&f,base,-1,-1);zeros(&f,base,6);
    CHECK(f.clicks==1U);
    init(&f);for(unsigned int i=0;i<1000U;i++)(void)feed(&f,i*10U,1,-1,true,false,false);
    CHECK(f.replayed==1000U&&f.clicks==0U);
    puts("PASS unknown/error, disabled/blocked transparency, gap/rollback and natural clock wrap");
}
static uint32_t rng=UINT32_C(0x31415926);
static uint32_t random_u32(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static void randomized_ledger(void) {
    struct fixture f;init(&f);uint32_t t=UINT32_MAX-200U;
    for(unsigned int i=0;i<100000U;i++) {
        uint32_t r=random_u32();t+=r&31U;
        if(i%997U==0U)t+=101U;
        int16_t x=(int16_t)((int32_t)((r>>8)%65U)-32);
        int16_t y=(int16_t)((int32_t)((r>>16)%65U)-32);
        if((r&7U)==0U)x=y=0;
        (void)feed(&f,t,x,y,(r&0x100U)!=0U,(r&0x200U)!=0U,(r&0x400U)==0U);
    }
    printf("PASS ordered ledger100000: input=%" PRIu64 " replay=%" PRIu64 " discarded=%" PRIu64 " clicks=%" PRIu64 "\n",f.input,f.replayed,f.discarded,f.clicks);
}
int main(void) {
    real_tap_and_multiclick();moving_not_tapping();two_frame_boundaries();boundaries_and_extremes();
    invalid_time_and_modes();randomized_ledger();
    printf("PASS pulse policy: %" PRIu64 " steps, %" PRIu64 " checks; host logic only, not contact accuracy\n",steps,checks);
    return 0;
}
