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
    struct tp_microtap_frame ledger[4];
    unsigned int n;
    uint64_t input,replayed,discarded,clicks;
    int64_t ix,iy,rx,ry,tx,ty;
};
static uint64_t magnitude(int64_t v) { return v<0?(uint64_t)(-v):(uint64_t)v; }
static void init(struct fixture *f) { *f=(struct fixture){0};tp_microtap_reset(&f->s); }
static struct tp_microtap_result feed(struct fixture *f,uint32_t t,int16_t x,int16_t y,
                                      bool valid,bool enabled,bool blocked) {
    struct tp_microtap_state before=f->s;
    struct tp_microtap_result r=tp_microtap_step(&f->s,t,x,y,valid,enabled,blocked);
    steps++;CHECK(r.count<=4U);CHECK(f->s.count<=3U);
    if(!valid||!enabled||blocked||(before.have_time&&(uint32_t)(t-before.last_poll_ms)>100U))CHECK(!r.click);
    if(r.click) {
        /* Independently reconstruct every accepted candidate, not just state sums. */
        CHECK(f->n>=1U&&f->n<=3U&&before.count==f->n);
        int64_t sx=0,sy=0;uint64_t path=0,max_offset=0;
        for(unsigned int i=0;i<f->n;i++) {
            sx+=f->ledger[i].dx;sy+=f->ledger[i].dy;
            path+=magnitude(f->ledger[i].dx)+magnitude(f->ledger[i].dy);
            if(magnitude(sx)>max_offset)max_offset=magnitude(sx);
            if(magnitude(sy)>max_offset)max_offset=magnitude(sy);
        }
        CHECK(path>=2U&&path<=24U&&max_offset<=16U);
        CHECK((uint32_t)(t-f->ledger[f->n-1U].at_ms)>=60U);
        CHECK((uint32_t)(t-f->ledger[0].at_ms)<=120U);
        CHECK((uint32_t)(f->ledger[f->n-1U].at_ms-f->ledger[0].at_ms)<=50U);
        if(f->n>1U)CHECK(path-magnitude(sx)-magnitude(sy)>=TP_MICROTAP_MIN_CANCELLED_PATH);
        f->tx+=sx;f->ty+=sy;f->discarded+=f->n;f->clicks++;f->n=0;
    }
    if(valid&&(x!=0||y!=0)) {
        CHECK(f->n<4U);f->ledger[f->n++]=(struct tp_microtap_frame){t,x,y};
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
    for(unsigned int i=0;i<f->n;i++){px+=f->ledger[i].dx;py+=f->ledger[i].dy;}
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
    init(&f);
    /* Quiet completion and the next candidate can share one motion step. */
    for(unsigned int i=0;i<1000U;i++) {
        uint32_t t=i*70U;
        (void)packet(&f,t,3,0);(void)packet(&f,t+10U,-2,0);
    }
    zeros(&f,69940U,6);
    CHECK(f.clicks==1000U&&f.discarded==2000U&&f.replayed==0U);
    init(&f);(void)packet(&f,0,2,0);
    struct tp_microtap_result r=packet(&f,60,30,0);
    CHECK(r.click&&r.count==1U&&r.frames[0].at_ms==60U);
    CHECK(f.clicks==1U&&f.discarded==1U&&f.replayed==1U);
    puts("PASS recorded single-frame taps + rapid single/multi clicks; no cooldown");
}
static void multi_frame_shapes(void) {
    struct fixture f;init(&f);
    (void)packet(&f,0,3,0);(void)packet(&f,20,-2,0);
    CHECK(f.n==2U&&f.replayed==0U);zeros(&f,20,5);CHECK(f.clicks==0U);
    (void)packet(&f,80,0,0);CHECK(f.clicks==1U&&f.discarded==2U&&f.tx==1);
    init(&f);(void)packet(&f,0,2,1);(void)packet(&f,20,1,1);(void)packet(&f,40,-2,-1);
    CHECK(f.n==3U&&f.replayed==0U);zeros(&f,40,6);
    CHECK(f.clicks==1U&&f.discarded==3U&&f.tx==1&&f.ty==1);
    init(&f);(void)packet(&f,0,1,1);(void)packet(&f,50,-1,-1);zeros(&f,50,6);
    CHECK(f.clicks==1U&&f.discarded==2U&&f.tx==0&&f.ty==0); /* minimal path=4 */
    /* One count of reversal is insufficient, even if the path is nontrivial. */
    init(&f);(void)packet(&f,0,3,0);(void)packet(&f,10,-1,0);zeros(&f,10,6);
    CHECK(f.clicks==0U&&f.replayed==2U);
    init(&f);(void)packet(&f,0,2,0);(void)packet(&f,10,2,0);(void)packet(&f,20,-1,0);
    zeros(&f,20,6);CHECK(f.clicks==0U&&f.replayed==3U);
    for(unsigned int n=2;n<=3U;n++) {
        init(&f);for(unsigned int i=0;i<n;i++)(void)packet(&f,i*10U,2,-1);
        CHECK(f.n==n&&f.replayed==0U);zeros(&f,(n-1U)*10U,6);
        CHECK(f.clicks==0U&&f.replayed==n);
    }
    puts("PASS two/three-frame reversal, weak reversal and same-direction rejection");
}
static void moving_not_tapping(void) {
    struct fixture f;init(&f);
    (void)packet(&f,0,2,0);(void)packet(&f,10,-2,0);(void)packet(&f,20,2,0);
    struct tp_microtap_result r=packet(&f,30,-2,0);
    CHECK(r.count==4U&&f.n==0U&&!r.click);
    zeros(&f,30,20);CHECK(f.clicks==0U&&f.replayed==4U);
    init(&f);(void)packet(&f,0,2,0);(void)packet(&f,0,-2,0);(void)packet(&f,0,2,0);
    CHECK(f.n==3U&&f.clicks==0U);r=packet(&f,0,-2,0);
    CHECK(r.count==4U&&f.replayed==4U&&!r.click); /* equal times cannot hide a fourth frame */
    /* span 51 immediately flushes, rather than waiting for quiet. */
    init(&f);(void)packet(&f,0,-1,-1);r=packet(&f,51,1,1);
    CHECK(r.count==2U&&!r.click);zeros(&f,51,20);CHECK(f.clicks==0U);
    init(&f);(void)packet(&f,0,20,0);zeros(&f,0,18);
    r=packet(&f,190,2,0);CHECK(r.count==1U&&!tp_microtap_pending(&f.s));
    zeros(&f,190,20);(void)packet(&f,400,-1,-1);zeros(&f,400,6);
    CHECK(f.clicks==1U); /* only after ordinary-motion quiet >=200ms */
    init(&f);(void)packet(&f,0,17,0);zeros(&f,0,19);
    (void)packet(&f,199,2,0);CHECK(f.replayed==2U);zeros(&f,199,19);
    (void)packet(&f,399,2,0);zeros(&f,399,6);CHECK(f.clicks==1U);
    puts("PASS fourth-frame ordered flush, span violation and unchanged 200ms guard");
}
static void boundaries_and_extremes(void) {
    struct fixture f;init(&f);
    (void)packet(&f,0,1,0);zeros(&f,0,6);CHECK(f.clicks==0U&&f.replayed==1U);
    init(&f);(void)packet(&f,0,16,8);zeros(&f,0,6);CHECK(f.clicks==1U);
    init(&f);(void)packet(&f,0,16,9);CHECK(f.replayed==1U);zeros(&f,0,6);CHECK(f.clicks==0U);
    init(&f);(void)packet(&f,0,17,0);CHECK(f.replayed==1U);
    init(&f);(void)packet(&f,0,16,0);(void)packet(&f,50,-8,0);zeros(&f,50,6);
    CHECK(f.clicks==1U&&f.discarded==2U); /* span=50, path=24, peak=16 */
    init(&f);(void)packet(&f,0,16,0);struct tp_microtap_result r=packet(&f,10,-9,0);
    CHECK(r.count==2U&&f.replayed==2U); /* path=25, endpoint still small */
    init(&f);(void)packet(&f,0,16,0);r=packet(&f,10,1,0);
    CHECK(r.count==2U&&f.replayed==2U); /* cumulative Linf=17 */
    init(&f);(void)packet(&f,0,2,0);(void)packet(&f,25,2,0);r=packet(&f,51,-2,0);
    CHECK(r.count==3U&&!r.click); /* third-frame span violation */
    init(&f);(void)packet(&f,0,2,0);(void)packet(&f,50,-2,0);
    (void)packet(&f,100,0,0);r=packet(&f,120,0,0);
    CHECK(r.click&&f.discarded==2U); /* candidate age=120 inclusive */
    init(&f);(void)packet(&f,0,2,0);(void)packet(&f,50,-2,0);
    (void)packet(&f,100,0,0);r=packet(&f,121,0,0);
    CHECK(!r.click&&r.count==2U&&f.replayed==2U);
    for(int v=INT16_MIN;v<=INT16_MAX;v++) {
        init(&f);(void)packet(&f,0,(int16_t)v,0);zeros(&f,0,6);
        bool eligible=(v<=-2&&v>=-16)||(v>=2&&v<=16);
        CHECK(f.clicks==(eligible?1U:0U));
    }
    puts("PASS path/Linf/span/120ms boundaries and all65536 int16 impulses");
}
static void invalid_time_and_modes(void) {
    struct fixture f;
    for(unsigned int n=1;n<=3U;n++)for(unsigned int mode=0;mode<3U;mode++) {
        init(&f);for(unsigned int i=0;i<n;i++)(void)packet(&f,i*10U,(i&1U)?-2:2,0);
        struct tp_microtap_result r=feed(&f,(n-1U)*10U+60U,INT16_MIN,INT16_MAX,
                                         mode!=0U,mode!=1U,mode==2U);
        CHECK(!r.click&&r.count==(mode==0U?n:n+1U));
        CHECK(f.clicks==0U&&f.n==0U&&f.replayed==(mode==0U?n:n+1U));
    }
    /* Zero-valued mode cancellation must also flush, never complete a click. */
    for(unsigned int mode=0;mode<3U;mode++) {
        init(&f);(void)packet(&f,0,2,0);(void)packet(&f,10,-2,0);(void)packet(&f,20,2,0);
        (void)feed(&f,80,0,0,mode!=0U,mode!=1U,mode==2U);
        CHECK(f.clicks==0U&&f.replayed==3U);
    }
    init(&f);(void)packet(&f,0,-1,-1);(void)packet(&f,100,0,0);
    CHECK(f.clicks==1U); /* poll gap=100 inclusive */
    init(&f);(void)packet(&f,0,-1,-1);(void)packet(&f,101,0,0);
    CHECK(f.clicks==0U&&f.replayed==1U);
    init(&f);(void)packet(&f,0,2,0);(void)packet(&f,10,-2,0);(void)packet(&f,20,2,0);
    struct tp_microtap_result r=packet(&f,121,1,0);
    CHECK(!r.click&&r.count==4U&&f.replayed==4U);
    init(&f);(void)packet(&f,100,-1,-1);(void)packet(&f,90,0,0);
    CHECK(f.clicks==0U&&f.replayed==1U);
    init(&f);(void)packet(&f,0,2,0);(void)packet(&f,UINT32_C(0x80000000),0,0);
    CHECK(f.clicks==0U&&f.replayed==1U);
    init(&f);uint32_t base=UINT32_MAX-25U;(void)packet(&f,base,-1,-1);zeros(&f,base,6);
    CHECK(f.clicks==1U);
    init(&f);(void)packet(&f,base,2,0);(void)packet(&f,base+25U,-2,0);
    (void)packet(&f,base+50U,2,0);zeros(&f,base+50U,6);
    CHECK(f.clicks==1U&&f.discarded==3U);
    init(&f);(void)packet(&f,base,2,0);(void)packet(&f,base+50U,-2,0);
    (void)packet(&f,base+100U,0,0);(void)packet(&f,base+121U,0,0);
    CHECK(f.clicks==0U&&f.replayed==2U);
    init(&f);for(unsigned int i=0;i<1000U;i++)(void)feed(&f,i*10U,1,-1,true,false,false);
    CHECK(f.replayed==1000U&&f.clicks==0U);
    puts("PASS error/mode flushes, inclusive poll gap, rollback and natural wrap");
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
    (void)feed(&f,t+1U,0,0,false,true,false);CHECK(f.n==0U);
    printf("PASS ordered ledger100000: input=%" PRIu64 " replay=%" PRIu64 " discarded=%" PRIu64 " clicks=%" PRIu64 "\n",f.input,f.replayed,f.discarded,f.clicks);
    /* Structured randomized episodes ensure conservation also covers real
     * pending/click/flush paths, not mostly passthrough large random motion. */
    init(&f);t=UINT32_MAX-200U;
    for(unsigned int i=0;i<10000U;i++) {
        uint32_t r=random_u32();unsigned int n=1U+r%4U;
        int16_t amplitude=(int16_t)(2U+(r>>8)%7U);
        int16_t sign=(r&0x10U)?1:-1;
        for(unsigned int j=0;j<n;j++) {
            int16_t dx=(int16_t)(amplitude*sign);
            if((r&0x20U)&&(j&1U))dx=(int16_t)-dx;
            (void)packet(&f,t+j*10U,dx,0);
        }
        uint32_t last=t+(n-1U)*10U;
        if((r&0x40U)!=0U)(void)feed(&f,last+60U,0,0,false,true,false);
        else zeros(&f,last,6);
        zeros(&f,last+60U,20);CHECK(f.n==0U);t=last+270U;
    }
    CHECK(f.clicks>0U&&f.discarded>f.clicks&&f.replayed>0U);
    printf("PASS randomized candidate ledger: input=%" PRIu64 " replay=%" PRIu64 " discarded=%" PRIu64 " clicks=%" PRIu64 "\n",f.input,f.replayed,f.discarded,f.clicks);
}
int main(void) {
    CHECK(TP_MICROTAP_MIN_CANCELLED_PATH==4U&&TP_MICROTAP_MULTI_MIN_PATH==4U);
    CHECK(TP_MICROTAP_MAX_FRAMES==3U&&TP_MICROTAP_MAX_MOTION_SPAN_MS==50U);
    CHECK(TP_MICROTAP_MAX_CANDIDATE_MS==120U&&TP_MICROTAP_MAX_POLL_GAP_MS==100U);
    real_tap_and_multiclick();multi_frame_shapes();moving_not_tapping();boundaries_and_extremes();
    invalid_time_and_modes();randomized_ledger();
    printf("PASS pulse policy: %" PRIu64 " steps, %" PRIu64 " checks; host logic only, not contact accuracy\n",steps,checks);
    return 0;
}
