#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_spike.h"
static void warm(struct tp_spike_state *s,uint32_t start) {
    tp_spike_reset(s);
    for(unsigned i=0;i<4;i++){int16_t x=0,y=0;assert(!tp_spike_apply(s,&x,&y,start+i*10));}
}
static int magnitude(int x) { return x<0?-x:x; }
int main(void) {
    const unsigned gaps[]={1,5,10,20,50,51};
    for(unsigned g=0;g<sizeof(gaps)/sizeof(gaps[0]);g++) {
        struct tp_spike_state seed;warm(&seed,1000);
        for(int ix=-128;ix<=127;ix++)for(int iy=-128;iy<=127;iy++) {
            struct tp_spike_state s=seed;int16_t x=ix,y=iy;
            bool clipped=tp_spike_apply(&s,&x,&y,1030+gaps[g]);
            assert(magnitude(x)<=magnitude(ix) && magnitude(y)<=magnitude(iy));
            assert(magnitude(x)>=(magnitude(ix)+1)/2 && magnitude(y)>=(magnitude(iy)+1)/2);
            assert((x>0)==(ix>0) && (x<0)==(ix<0));
            assert((y>0)==(iy>0) && (y<0)==(iy<0));
            int norm=magnitude(ix)>magnitude(iy)?magnitude(ix):magnitude(iy);
            unsigned cap=(6*gaps[g]+9)/10,half=((unsigned)norm+1)/2;
            if(cap<half){cap=half;}
            assert(clipped==(gaps[g]<=50 && (unsigned)norm>cap));
            if(!clipped){assert(x==ix&&y==iy);}
            if(clipped){assert((unsigned)(magnitude(x)>magnitude(y)?magnitude(x):magnitude(y))==cap);}
        }
    }
    struct tp_spike_state s={0};int16_t x=100,y=-100;
    assert(!tp_spike_apply(&s,&x,&y,1000)&&x==100&&y==-100); /* Cold start passes. */
    warm(&s,1000);x=20;y=0;assert(tp_spike_apply(&s,&x,&y,1040)&&x==10&&y==0);
    x=20;assert(!tp_spike_apply(&s,&x,&y,1050)&&x==20);
    x=-20;assert(!tp_spike_apply(&s,&x,&y,1060)&&x==-20);
    for(unsigned i=0;i<30;i++){x=y=0;tp_spike_apply(&s,&x,&y,1070+i*10);assert(x==0&&y==0);}
    x=20;y=0;assert(tp_spike_apply(&s,&x,&y,1370)&&x==10);
    warm(&s,1000);x=20;y=0;assert(!tp_spike_apply(&s,&x,&y,1030)&&x==20); /* dt=0 resets. */
    warm(&s,UINT32_MAX-35);x=20;y=-20;
    assert(tp_spike_apply(&s,&x,&y,4)&&x==10&&y==-10); /* wrap, dt=10 */
    warm(&s,1000);x=4;y=0;assert(!tp_spike_apply(&s,&x,&y,1040)&&x==4);
    x=20;assert(!tp_spike_apply(&s,&x,&y,1050)&&x==20); /* medium motion disarms */
    for(unsigned i=0;i<4;i++){x=1;y=-1;tp_spike_apply(&s,&x,&y,1060+i*10);assert(x==1&&y==-1);}
    x=20;y=-20;assert(tp_spike_apply(&s,&x,&y,1100)&&x==10&&y==-10);
    tp_spike_reset(&s);x=20;y=0;assert(!tp_spike_apply(&s,&x,&y,1110)&&x==20);
    puts("PASS Spike limiter: exhaustive signed XY x 6 intervals, bounded half reduction, sign/zero axes, cold/slow/medium/fast motion, no queued tail, reset and wrap");
}
