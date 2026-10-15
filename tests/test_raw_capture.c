#include <assert.h>
#include <stdio.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_raw_capture.h"
static struct tp_raw_capture_event seen[128];static unsigned n;
static void emit_event(void *ctx,const struct tp_raw_capture_event *e) {
    (void)ctx;assert(n<128U);seen[n++]=*e;
}
static void poll(struct tp_raw_capture_state *s,uint32_t t,bool online,bool dtr,int irq) {
    tp_raw_capture_poll(s,t,online,dtr,irq,false,emit_event,0);
}
int main(void) {
    struct tp_raw_capture_state s;tp_raw_capture_reset(&s);
    uint8_t raw[7]={255,255,255,255,1,36,154};
    tp_raw_capture_read(&s,0,TP_RAW_CAPTURE_OK,0,raw,false,emit_event,0);
    assert(n==0&&s.seq==1);poll(&s,10,true,true,1);
    assert(seen[0].kind==TP_RAW_CAPTURE_BEGIN&&seen[0].first_seq==2&&seen[1].kind==TP_RAW_CAPTURE_GPIO);
    tp_raw_capture_read(&s,20,TP_RAW_CAPTURE_OK,0,raw,false,emit_event,0);
    assert(seen[n-1].seq==2&&seen[n-1].raw_valid&&seen[n-1].dx==-1&&seen[n-1].dy==-1);
    raw[6]=0;assert(seen[n-1].raw[6]==154);
    tp_raw_capture_read(&s,30,TP_RAW_CAPTURE_POINTER,-5,0,false,emit_event,0);
    assert(!seen[n-1].raw_valid&&seen[n-1].error==-5&&seen[n-1].seq==3);
    tp_raw_capture_read(&s,40,TP_RAW_CAPTURE_BURST,-6,0,true,emit_event,0);
    assert(!seen[n-1].raw_valid&&seen[n-1].scroll&&seen[n-1].read_stage==TP_RAW_CAPTURE_BURST);
    poll(&s,1010,true,true,-5);assert(s.seq==4&&s.attempts==3&&s.successes==1&&s.errors==2);
    assert(seen[n-1].kind==TP_RAW_CAPTURE_HEARTBEAT&&seen[n-1].error==-5);
    poll(&s,1020,true,false,1);assert(!s.active&&seen[n-1].kind==TP_RAW_CAPTURE_END);
    assert(seen[n-1].attempts==3&&seen[n-1].last_seq==4&&seen[n-1].reason==TP_RAW_CAPTURE_REASON_DTR_CLOSED);
    poll(&s,1030,true,true,1);assert(s.session==2&&s.first_seq==5&&s.attempts==0);
    poll(&s,1040,false,true,1);assert(seen[n-1].reason==TP_RAW_CAPTURE_REASON_USB_OFFLINE);
    n=0;tp_raw_capture_reset(&s);uint32_t start=UINT32_MAX-9U;
    poll(&s,start,true,true,1);poll(&s,10,true,true,1);assert(s.active&&s.session==1);
    poll(&s,start+TP_RAW_CAPTURE_MAX_MS,true,true,1);
    assert(!s.active&&s.wait_close&&seen[n-1].reason==TP_RAW_CAPTURE_REASON_WINDOW_LIMIT);
    unsigned before=n;poll(&s,start+TP_RAW_CAPTURE_MAX_MS+10U,true,true,1);assert(n==before);
    poll(&s,180020U,true,false,1);poll(&s,180030U,true,true,1);assert(s.active&&s.session==2);
    poll(&s,180000U,true,true,1);assert(!s.active&&seen[n-1].reason==TP_RAW_CAPTURE_REASON_CLOCK_DISCONTINUITY);
    puts("PASS raw session: actual-read sequence, copied raw7, failed stages, no GPIO/cache frames, DTR/USB lifecycle, zero-frame END, bounded180s, rollover and clock faults");
    return 0;
}
