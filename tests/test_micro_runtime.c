#include "test_env.h"
#define CONFIG_ZMK_USB 0
#define CONFIG_ZMK_BLE 0
static int32_t atomic_inc(atomic_t *p) { int32_t old=*p; (*p)++; return old; }
static bool zmk_endpoint_instance_eq(struct zmk_endpoint_instance a, struct zmk_endpoint_instance b) {
    return a.transport==b.transport;
}
/* Local mock only: notifications may publish guard fields, never call the SDK
 * or bridge inside this critical section. No global test_env changes needed. */
static unsigned int guard_irq_depth;
static unsigned int irq_lock(void) { assert(guard_irq_depth==0);return guard_irq_depth++; }
static void irq_unlock(unsigned int key) { assert(key==0&&guard_irq_depth==1);guard_irq_depth=0; }
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_tap_output.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_pointer.h"
static struct tp_tap_output_inflight status;
static int refs[5];
static unsigned int submissions, moves, trace_count;
static char trace[512];
static int pointer_x,pointer_y;
static struct tp_pointer_state pointer;
static void (*after_context_hook)(void);
static uint32_t emit_wall_advance;
int bbp9981_hid_mouse_button_references(unsigned int i) { assert(!guard_irq_depth);return refs[i]; }
int tp_tap_output_init(void) { assert(!guard_irq_depth);status.initialized=true;return 0; }
struct tp_tap_output_inflight tp_tap_output_read_inflight(void) {
    assert(!guard_irq_depth);status.busy=status.phase!=TP_TAP_OUTPUT_IDLE||status.owns_left;
    return status;
}
int tp_tap_output_submit(void) {
    assert(!guard_irq_depth);
    if(status.phase!=TP_TAP_OUTPUT_IDLE)return -EBUSY;
    status.phase=TP_TAP_OUTPUT_HOLDING;status.owns_left=true;refs[0]++;
    status.down_reports++;status.accepted_at=fake_now;submissions++;trace[trace_count++]='D';return 0;
}
static void complete(void) {
    assert(!guard_irq_depth&&status.owns_left&&refs[0]>0);
    refs[0]--;status.owns_left=false;status.phase=TP_TAP_OUTPUT_IDLE;
    status.up_reports++;trace[trace_count++]='U';
}
int tp_tap_output_cancel(void) {
    assert(!guard_irq_depth);
    if(status.owns_left)complete();
    status.phase=TP_TAP_OUTPUT_IDLE;return 0;
}
int tp_tap_output_update_context(struct tp_tap_output_context c) {
    assert(!guard_irq_depth);
    if(!c.enabled||c.physical_left_down||!c.physical_left_known) {
        (void)tp_tap_output_cancel();
    }
    if(after_context_hook) { void (*hook)(void)=after_context_hook;after_context_hook=NULL;hook(); }
    return 0;
}
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap_runtime.c"
static struct tp_microtap_frame emitted[512];
static void emit(void *ctx,int16_t dx,int16_t dy,uint32_t t) {
    (void)ctx;assert(!guard_irq_depth&&!status.owns_left);
    assert(moves<512);emitted[moves]=(struct tp_microtap_frame){.at_ms=t,.dx=dx,.dy=dy};
    int16_t x,y;tp_pointer_motion(&pointer,(int8_t)dx,(int8_t)dy,false,t,&x,&y);
    pointer_x+=x;pointer_y+=y;moves++;trace[trace_count++]='M';
    fake_now+=emit_wall_advance;
}
static void poll_at(uint32_t t,int16_t dx,int16_t dy,bool valid,bool scroll) {
    fake_now=t;tp_microtap_runtime_poll(t,dx,dy,valid,scroll,0,0,emit,NULL);
}
static void tap_at(uint32_t t,int16_t dx,int16_t dy) {
    poll_at(t,dx,dy,true,false);poll_at(t,dx,dy,true,false);poll_at(t,dx,dy,true,false);
}
static void zeros(uint32_t first,uint32_t last) {
    for(uint32_t t=first;t<=last;t+=10U)poll_at(t,0,0,true,false);
}
static void position_edge(uint32_t wall,uint32_t position,bool down) {
    fake_now=wall;
    zmk_event_t event={.valid=true,.data={.position=position,.state=down,.timestamp=wall}};
    assert(native_position(&event)==ZMK_EV_EVENT_BUBBLE);
}
static void fixture(bool on) {
    memset(&status,0,sizeof(status));memset(refs,0,sizeof(refs));
    memset(events,0,sizeof(events));head=count=0;initialized=false;have_context=false;
    seen_epoch=seen_mode=seen_speed=usb_generation=0;context_epoch=1;
    enabled=change_epoch=0;
    /* Simulated POWER ON, not a production gesture/mode reset. */
    memset(&native_guard,0,sizeof(native_guard));
    memset(&auto_spacing,0,sizeof(auto_spacing));
    native_seen_generation=native_activity_at=0;native_have_activity=false;
    guard_irq_depth=0;after_context_hook=NULL;emit_wall_advance=0;fake_now=0;
    fake_transport=ZMK_TRANSPORT_BLE;
    submissions=moves=trace_count=0;pointer_x=pointer_y=0;memset(trace,0,sizeof(trace));
    memset(emitted,0,sizeof(emitted));tp_pointer_reset(&pointer);tp_microtap_reset(&gesture);
    tp_microtap_runtime_init();assert(!tp_microtap_runtime_enabled());
    if(on) { tp_microtap_runtime_toggle(); }
    poll_at(0,0,0,true,false);
}
static void test_floor_and_first(void) {
    fixture(false);poll_at(10,1,-1,true,false);
    assert(moves==1&&pointer_x==1&&pointer_y==-1&&submissions==0);
    for(int16_t weak=1;weak<6;weak++) {
        fixture(true);poll_at(10,weak,0,true,false);zeros(20,120);
        poll_at(129,0,0,true,false);assert(moves==0&&submissions==0);
        poll_at(130,0,0,true,false);
        assert(moves==1&&emitted[0].dx==weak&&emitted[0].at_ms==10&&submissions==0);
    }
    fixture(true);tap_at(10,8,0);zeros(20,120);
    poll_at(129,0,0,true,false);assert(submissions==0&&moves==0);
    poll_at(130,0,0,true,false);assert(submissions==1&&moves==0);
    assert(pointer.x.remainder==0&&pointer.y.remainder==0&&!native_have_activity);
    complete();poll_at(140,0,0,true,false);zeros(150,350);
    tap_at(360,8,0);zeros(370,480);assert(submissions==2);
    complete();poll_at(490,0,0,true,false);zeros(500,700);
    tap_at(710,8,0);zeros(720,830);assert(submissions==3);
    complete();poll_at(840,0,0,true,false);
    assert(moves==0&&count==0&&trace_count==6&&memcmp(trace,"DUDUDU",6)==0);
    assert(native_guard.generation==0&&!native_have_activity); /* Own LEFT is not native. */
}
static void test_fifo_and_backpressure(void) {
    fixture(true);
    for(uint32_t i=0;i<9U;i++)poll_at(10U+i*10U,1,0,true,false);
    poll_at(100,0,0,true,false); /* Drain the nine-frame FIFO batch. */
    assert(moves==9&&pointer_x==10&&submissions==0&&!tp_microtap_runtime_pending());
    for(unsigned int i=0;i<9U;i++)assert(emitted[i].at_ms==10U+i*10U);
    zeros(110,220);assert(submissions==0);

    fixture(true);tap_at(10,8,0);zeros(20,130);assert(submissions==1);
    poll_at(140,30,0,true,false);poll_at(150,1,0,true,false);poll_at(160,1,0,true,false);
    assert(moves==0&&count==4);complete();poll_at(170,0,0,true,false);
    assert(moves==3&&pointer_x==38&&memcmp(trace,"DUMMM",5)==0);
    assert(emitted[0].at_ms==140&&emitted[1].at_ms==150&&emitted[2].at_ms==160);
    zeros(180,500);assert(submissions==1&&count==0);

    fixture(true);tap_at(10,8,0);zeros(20,350);
    tap_at(360,8,0);zeros(370,700);
    tap_at(710,8,0);zeros(720,830);
    assert(submissions==1&&count==3);
    fake_now=840;complete();poll_at(840,0,0,true,false);assert(submissions==2);
    fake_now=850;complete();poll_at(850,0,0,true,false);assert(submissions==2&&count==0);
    zeros(860,1250);assert(submissions==2); /* Never wait and bunch automatic clicks. */
}
static void test_native_edges_and_busy(void) {
    fixture(true);
    /* Actual short native pos4 DOWN/UP+refs occurs entirely between two polls. */
    position_edge(15,4,true);refs[0]++;position_edge(17,4,false);refs[0]--;
    poll_at(20,6,0,true,false);assert(moves==1&&submissions==0&&refs[0]==0);
    zeros(30,610);poll_at(619,0,0,true,false);
    assert(native_guard_observe(false).blocked);tap_at(620,8,0);
    assert(!native_guard_observe(false).blocked&&moves==1);
    zeros(630,730);assert(submissions==0);poll_at(740,0,0,true,false);assert(submissions==1);

    fixture(true);position_edge(10,0,true); /* Delayed native input still has refs==0. */
    poll_at(10,6,0,true,false);zeros(20,1000);assert(submissions==0&&moves==1);
    assert(native_guard.held!=0U&&native_guard_observe(false).blocked);
    position_edge(1001,0,false);poll_at(1001,0,0,true,false);zeros(1011,1591);
    poll_at(1600,0,0,true,false);assert(native_guard_observe(false).blocked);
    tap_at(1601,8,0);assert(!native_guard_observe(false).blocked);
    zeros(1611,1721);assert(submissions==1);

    fixture(true);fake_now=10;tp_microtap_runtime_shoulder_busy(true);
    fake_now=19;tp_microtap_runtime_shoulder_busy(false); /* Flush D/U already refs==0. */
    poll_at(20,6,0,true,false);assert(moves==1&&submissions==0);
    zeros(30,90);uint32_t gen=native_guard.generation;
    fake_now=100;tp_microtap_runtime_shoulder_busy(false);poll_at(100,0,0,true,false);
    assert(native_guard.generation==gen);zeros(110,610);
    poll_at(619,0,0,true,false);assert(native_guard_observe(false).blocked);
    tap_at(620,8,0);assert(!native_guard_observe(false).blocked);
    zeros(630,740);assert(submissions==1);
}
static void test_refs_context_and_faults(void) {
    fixture(true);refs[0]=1;poll_at(10,6,0,true,false);zeros(20,1000);
    poll_at(1010,8,-3,true,false);
    assert(moves==2&&submissions==0&&refs[0]==1); /* Native drag/move stays live. */
    refs[0]=0;poll_at(1020,0,0,true,false);zeros(1030,1610);
    poll_at(1619,0,0,true,false);assert(native_guard_observe(false).blocked);
    tap_at(1620,8,0);zeros(1630,1740);assert(submissions==1);

    fixture(true);refs[2]=-1;poll_at(10,6,0,true,false);zeros(20,700);
    assert(submissions==0&&moves==1);refs[2]=0;poll_at(710,0,0,true,false);
    assert(native_guard_observe(false).blocked);zeros(720,1300);
    tap_at(1310,8,0);zeros(1320,1430);assert(submissions==1);

    fixture(true);position_edge(10,4,true);position_edge(11,4,false);
    poll_at(20,0,0,true,false);uint32_t gen=native_guard.generation;
    zeros(30,90);tp_microtap_runtime_interrupt();poll_at(100,0,0,true,false);
    tp_microtap_runtime_toggle();tp_microtap_runtime_toggle();poll_at(110,0,0,true,false);
    fake_now=120;tp_microtap_runtime_poll(120,0,0,true,false,1,1,emit,NULL);
    fake_transport=ZMK_TRANSPORT_USB;poll_at(130,0,0,true,false);
    tp_microtap_reset(&gesture); /* Even an explicit gesture reset cannot clear native. */
    zeros(140,340);poll_at(350,6,0,true,false);assert(moves==1&&submissions==0);
    zeros(360,610);poll_at(619,0,0,true,false);assert(native_guard_observe(false).blocked);
    assert(native_guard.generation==gen);tap_at(620,8,0);
    zeros(630,740);assert(submissions==1);

    fixture(true);poll_at(10,6,0,true,false);poll_at(20,0,0,false,false);
    assert(moves==1&&submissions==0);zeros(30,180);assert(submissions==0);
    fixture(true);poll_at(10,6,0,true,false);tp_microtap_runtime_toggle();
    poll_at(20,0,0,true,false);assert(!tp_microtap_runtime_pending()&&moves==0);
    fixture(true);poll_at(10,6,0,true,false);poll_at(20,0,0,true,true);
    poll_at(130,0,0,true,true);assert(submissions==0&&moves==0);
    fixture(true);position_edge(10,37,true);assert(native_guard.generation==0);
}
static void inject_native_du(void) {
    position_edge(fake_now,4,true);refs[0]++;
    position_edge(fake_now,4,false);refs[0]--;
}
static void test_submit_rechecks_and_no_late_click(void) {
    fixture(true);tap_at(10,8,0);zeros(20,120);
    after_context_hook=inject_native_du;poll_at(130,0,0,true,false);
    assert(submissions==0&&count==0);zeros(140,1000);assert(submissions==0);

    fixture(true);
    status.phase=TP_TAP_OUTPUT_HOLDING;status.owns_left=true;refs[0]=1;
    assert(enqueue((struct micro_event){.click=true,.started=true}));
    assert(enqueue((struct micro_event){.click=true,.motion.at_ms=30,.native_generation=0}));
    assert(enqueue((struct micro_event){.motion={.at_ms=35,.dx=9,.dy=-2}}));
    position_edge(40,4,true);position_edge(41,4,false);
    assert(native_guard_observe(false).blocked);
    fake_now=1000;drain(true,emit,NULL);assert(count==3&&submissions==0);
    assert(!native_guard_observe(false).blocked); /* More than 600ms elapsed behind head. */
    complete();drain(true,emit,NULL);
    assert(count==0&&submissions==0&&moves==1&&emitted[0].at_ms==35);
    fake_now=2000;drain(true,emit,NULL);assert(submissions==0); /* Never catch up after native. */

    fixture(true);position_edge(100,4,true);position_edge(100,4,false);
    assert(native_guard_observe(false).blocked);fake_now=700;
    assert(!native_guard_observe(false).blocked);
    /* White-box invariant: expiry/TTL cannot revive a stale generation, even
     * if that malformed queued item carries an otherwise fresh timestamp. */
    assert(enqueue((struct micro_event){.click=true,.motion.at_ms=700,.native_generation=0}));
    drain(true,emit,NULL);assert(submissions==0&&count==0);

    /* Cancelling bridge phases must revoke only its own reference, never foreign. */
    for(int phase=TP_TAP_OUTPUT_QUEUED;phase<=TP_TAP_OUTPUT_RELEASING;phase++) {
        fixture(true);status.phase=(enum tp_tap_output_phase)phase;
        status.owns_left=phase!=TP_TAP_OUTPUT_QUEUED;refs[0]=status.owns_left?2:1;
        assert(enqueue((struct micro_event){.click=true,.started=true}));
        poll_at(10,6,0,true,false);assert(refs[0]==1&&!status.owns_left&&submissions==0&&moves==1);
    }
}
static void queued_click(uint32_t sample) {
    assert(enqueue((struct micro_event){.click=true,.motion.at_ms=sample,
        .native_generation=native_guard.generation}));
}
static void test_fresh_wall_and_wrap(void) {
    /* A producer's stale sample now can precede accepted_at. Unsigned sample
     * subtraction would look huge and bypass the bridge spacing check. */
    const uint32_t walls[]={1020,1349,1350,900,UINT32_C(0x800003e8)};
    for(unsigned int i=0;i<sizeof(walls)/sizeof(walls[0]);i++) {
        fixture(true);status.down_reports=1;status.accepted_at=1000;
        fake_now=walls[i];queued_click(900);drain(true,emit,NULL);
        assert(submissions==(i==2U?1U:0U));
    }
    for(unsigned int i=0;i<2;i++) {
        fixture(true);status.down_reports=1;status.accepted_at=UINT32_MAX-100U;
        fake_now=248U+i;queued_click(100);drain(true,emit,NULL);
        assert(submissions==i); /* Natural rollover: 349 drop /350 allow. */
    }
    for(unsigned int age=500;age<=501;age++) {
        fixture(true);fake_now=1000U+age;queued_click(1000);
        drain(true,emit,NULL);assert(submissions==(age==500U?1U:0U));
    }
    fixture(true);fake_now=999;queued_click(1000);drain(true,emit,NULL);
    assert(submissions==0); /* Future/backwards/half-range event age fails closed. */
    fixture(true);fake_now=100;emit_wall_advance=501;
    assert(enqueue((struct micro_event){.motion={.at_ms=10,.dx=8}}));
    queued_click(100);drain(true,emit,NULL);
    assert(fake_now==601&&submissions==0&&moves==1&&emitted[0].at_ms==10);
    /* Real producer now stays stale while earlier motion emit consumes wall time. */
    fixture(true);fake_now=1501;queued_click(1000);
    tp_microtap_runtime_poll(1000,0,0,true,false,0,0,emit,NULL);
    assert(submissions==0&&count==0);

    fixture(true);position_edge(UINT32_MAX-100U,4,true);
    position_edge(UINT32_MAX-100U,4,false);assert(native_guard_observe(false).blocked);
    fake_now=498;assert(native_guard_observe(false).blocked);
    fake_now=499;assert(!native_guard_observe(false).blocked); /* 599/600ms across u32 wrap. */
    assert(!native_have_activity);
    fake_now=UINT32_C(0x800001f3);assert(!native_guard_observe(false).blocked);
    fake_now=UINT32_MAX;assert(!native_guard_observe(false).blocked);
    fake_now=0;assert(!native_guard_observe(false).blocked); /* Expired idle never invents activity. */
    fixture(true);position_edge(1000,4,true);position_edge(1000,4,false);
    assert(native_guard_observe(false).blocked);fake_now=999;
    assert(native_guard_observe(false).blocked);fake_now=1598;
    assert(native_guard_observe(false).blocked);fake_now=1599;
    assert(!native_guard_observe(false).blocked);
}
static void test_spacing_retirement(void) {
    /* One real auto acceptance, then ordinary idle polls retire its350ms token.
     * Its SDK history remains unchanged even over half-range and full wrap. */
    fixture(true);tap_at(10,8,0);zeros(20,130);assert(submissions==1);
    complete();poll_at(140,0,0,true,false);zeros(150,470);assert(auto_spacing.live);
    poll_at(480,0,0,true,false);assert(!auto_spacing.live&&status.down_reports==1);
    uint32_t late=UINT32_C(0x80000082); /* accepted_at130 + half a u32 period. */
    poll_at(late,0,0,true,false);assert(!auto_spacing.live);
    zeros(late+10U,late+350U);tap_at(late+360U,8,0);
    zeros(late+370U,late+480U);assert(submissions==2); /* Old SDK time cannot reject forever. */
    complete();poll_at(late+490U,0,0,true,false);zeros(late+500U,late+830U);
    assert(!auto_spacing.live);poll_at(UINT32_MAX,0,0,true,false);
    poll_at(0,0,0,true,false);assert(!auto_spacing.live);
    zeros(10,350);tap_at(360,8,0);zeros(370,480);assert(submissions==3);

    /* Sleep across half-range while a token was still live: allow exactly one
     * conservative350ms window at wake, not repeated reloads of oldaccepted_at. */
    fixture(true);status.down_reports=1;status.accepted_at=1000;
    poll_at(1000,0,0,true,false);assert(auto_spacing.live&&!auto_spacing.reanchored);
    uint32_t wake=UINT32_C(0x800003e8);
    poll_at(wake,0,0,true,false);assert(auto_spacing.live&&auto_spacing.reanchored);
    zeros(wake+10U,wake+340U);poll_at(wake+349U,0,0,true,false);assert(auto_spacing.live);
    poll_at(wake+350U,0,0,true,false);assert(!auto_spacing.live);
    fake_now=UINT32_MAX;assert(!auto_spacing_blocked(status));
    fake_now=1000;assert(!auto_spacing_blocked(status)); /* Same acceptance never re-arms. */
    queued_click(1000);drain(true,emit,NULL);assert(submissions==1);

    fixture(true);status.down_reports=1;status.accepted_at=1000;
    poll_at(900,0,0,true,false);assert(auto_spacing.live&&auto_spacing.reanchored);
    poll_at(1249,0,0,true,false);assert(auto_spacing.live);
    poll_at(1250,0,0,true,false);assert(!auto_spacing.live); /* Future timestamp recovers. */
    /* A new SDK acceptance, including report-counter rollover, still starts its
     * own normal interval; caching expiry cannot bypass a later real click. */
    status.down_reports=UINT32_MAX;status.accepted_at=1300;fake_now=1300;
    assert(auto_spacing_blocked(status));fake_now=1650;assert(!auto_spacing_blocked(status));
    status.down_reports=0;status.accepted_at=1700;fake_now=2049;
    assert(auto_spacing_blocked(status));fake_now=2050;assert(!auto_spacing_blocked(status));
}
int main(void) {
    test_floor_and_first();test_fifo_and_backpressure();test_native_edges_and_busy();
    test_refs_context_and_faults();test_submit_rechecks_and_no_late_click();test_fresh_wall_and_wrap();
    test_spacing_retirement();
    puts("PASS Actual runtime: floor8/min2 frames, frames8/span120, first120ms, FIFO/no catch-up, own LEFT excluded, native pos0/4+held+refs/unknown+shoulder flush600ms, reset/host persistence, generation cancellation, fresh-wall gap350, 599/600 and u32 wrap; no host ACK or contact claim");
    return 0;
}
