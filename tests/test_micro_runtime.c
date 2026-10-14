#include "test_env.h"
#define CONFIG_ZMK_USB 0
#define CONFIG_ZMK_BLE 0
static int32_t atomic_inc(atomic_t *p) { int32_t old=*p; (*p)++; return old; }
static bool zmk_endpoint_instance_eq(struct zmk_endpoint_instance a, struct zmk_endpoint_instance b) {
    return a.transport==b.transport;
}
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_tap_output.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_pointer.h"
static struct tp_tap_output_inflight status;
static int refs[5];
static unsigned int submissions, moves, trace_count;
static char trace[512];
static int pointer_x,pointer_y;
static struct tp_pointer_state pointer;
int bbp9981_hid_mouse_button_references(unsigned int i) { return refs[i]; }
int tp_tap_output_init(void) { status.initialized=true; return 0; }
struct tp_tap_output_inflight tp_tap_output_read_inflight(void) {
    status.busy=status.phase!=TP_TAP_OUTPUT_IDLE||status.owns_left;
    return status;
}
int tp_tap_output_submit(void) {
    if(status.phase!=TP_TAP_OUTPUT_IDLE)return -EBUSY;
    status.phase=TP_TAP_OUTPUT_HOLDING;status.owns_left=true;refs[0]++;
    status.down_reports++;submissions++;trace[trace_count++]='D';return 0;
}
static void complete(void) {
    assert(status.owns_left && refs[0]>0);
    refs[0]--;status.owns_left=false;status.phase=TP_TAP_OUTPUT_IDLE;
    status.up_reports++;trace[trace_count++]='U';
}
int tp_tap_output_cancel(void) {
    if(status.owns_left)complete();
    status.phase=TP_TAP_OUTPUT_IDLE;return 0;
}
int tp_tap_output_update_context(struct tp_tap_output_context c) {
    if(!c.enabled||c.physical_left_down||!c.physical_left_known) {
        (void)tp_tap_output_cancel();
    }
    return 0;
}
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap_runtime.c"
static void emit(void *ctx,int16_t dx,int16_t dy,uint32_t t) {
    (void)ctx;assert(!status.owns_left);
    int16_t x,y;tp_pointer_motion(&pointer,(int8_t)dx,(int8_t)dy,false,t,&x,&y);
    pointer_x+=x;pointer_y+=y;moves++;trace[trace_count++]='M';
}
static void poll_at(uint32_t t,int16_t dx,int16_t dy,bool valid,bool scroll) {
    fake_now=t;tp_microtap_runtime_poll(t,dx,dy,valid,scroll,0,0,emit,NULL);
}
static void fixture(bool on) {
    memset(&status,0,sizeof(status));memset(refs,0,sizeof(refs));
    memset(events,0,sizeof(events));head=count=0;initialized=false;have_context=false;
    seen_epoch=seen_mode=seen_speed=usb_generation=0;context_epoch=1;
    enabled=change_epoch=shoulder_busy=0;
    submissions=moves=trace_count=0;pointer_x=pointer_y=0;memset(trace,0,sizeof(trace));
    tp_pointer_reset(&pointer);tp_microtap_reset(&gesture);
    tp_microtap_runtime_init();assert(!tp_microtap_runtime_enabled());
    if(on) { tp_microtap_runtime_toggle(); }
    poll_at(0,0,0,true,false);
}
int main(void) {
    fixture(false);
    poll_at(10,1,-1,true,false);assert(moves==1&&pointer_x==1&&pointer_y==-1&&submissions==0);
    poll_at(20,1,1,true,false);assert(moves==2&&submissions==0);

    fixture(true);
    poll_at(10,1,0,true,false);assert(moves==0&&tp_microtap_runtime_pending());
    poll_at(20,1,0,true,false);poll_at(30,1,0,true,false);
    poll_at(40,0,0,true,false);poll_at(50,0,0,true,false);poll_at(60,0,0,true,false);
    assert(submissions==1&&moves==0&&pointer_x==0&&pointer_y==0);
    assert(pointer.x.remainder==0&&pointer.y.remainder==0);
    /* Successive three-frame gestures queue even while transport holds first. */
    poll_at(70,1,0,true,false);poll_at(80,1,0,true,false);poll_at(90,1,0,true,false);
    poll_at(100,0,0,true,false);poll_at(110,0,0,true,false);
    poll_at(120,0,0,true,false);assert(submissions==1&&count==2);
    complete();poll_at(130,0,0,true,false);assert(submissions==2);
    poll_at(140,1,0,true,false);poll_at(150,1,0,true,false);poll_at(160,1,0,true,false);
    poll_at(170,0,0,true,false);poll_at(180,0,0,true,false);
    poll_at(190,0,0,true,false);assert(count==2);
    complete();poll_at(200,0,0,true,false);assert(submissions==3);
    complete();poll_at(210,0,0,true,false);
    assert(moves==0&&count==0&&trace_count==6&&memcmp(trace,"DUDUDU",6)==0);

    fixture(true);
    poll_at(10,2,0,true,false);poll_at(20,20,0,true,false);
    assert(submissions==0&&moves==2&&pointer_x==26);
    poll_at(30,1,0,true,false);poll_at(40,0,0,true,false);poll_at(50,0,0,true,false);
    poll_at(60,0,0,true,false);assert(submissions==0); /* moving tail isn't a click */

    fixture(true);
    poll_at(10,1,0,true,false);poll_at(20,1,0,true,false);poll_at(30,1,0,true,false);
    poll_at(40,0,0,true,false);poll_at(50,0,0,true,false);
    poll_at(60,0,0,true,false);assert(submissions==1);
    poll_at(70,30,0,true,false);assert(moves==0&&count==2); /* no drag while synthetic held */
    complete();poll_at(80,0,0,true,false);
    assert(moves==1&&pointer_x==36&&memcmp(trace,"DUM",3)==0);

    fixture(true);refs[0]=1;
    poll_at(10,2,0,true,false);assert(moves==1&&submissions==0);
    poll_at(40,0,0,true,false);assert(submissions==0&&refs[0]==1);
    fixture(true);refs[1]=1;
    poll_at(10,2,0,true,false);assert(moves==1&&submissions==0);

    fixture(true);poll_at(10,2,0,true,false);poll_at(20,0,0,false,false);
    assert(moves==1&&submissions==0);poll_at(50,0,0,true,false);assert(submissions==0);
    fixture(true);poll_at(10,2,0,true,false);tp_microtap_runtime_toggle();
    poll_at(20,0,0,true,false);poll_at(50,0,0,true,false);
    assert(!tp_microtap_runtime_enabled()&&moves==0&&submissions==0&&!tp_microtap_runtime_pending());
    fixture(true);poll_at(10,2,0,true,false);fake_transport=ZMK_TRANSPORT_USB;
    if(seen_endpoint.transport==ZMK_TRANSPORT_USB)fake_transport=ZMK_TRANSPORT_BLE;
    poll_at(20,0,0,true,false);poll_at(50,0,0,true,false);assert(submissions==0&&moves==0);
    fixture(true);poll_at(10,2,0,true,false);poll_at(20,0,0,true,true);
    poll_at(50,0,0,true,true);assert(submissions==0&&moves==0);
    fixture(true);tp_microtap_runtime_shoulder_busy(true);
    poll_at(10,2,0,true,false);assert(moves==1&&submissions==0);
    puts("PASS Actual micro runtime: off-transparent pointer, triple click FIFO, zero candidate drift, early flush, no tail click, no synthetic drag, physical owners, error/mode/endpoint/toggle cancellation");
    return 0;
}
