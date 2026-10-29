/* SPDX-License-Identifier: MIT
 * HOST-ONLY integration: compiler preincludes real snapshot and candidate, then
 * this TU includes fixed HOG through the existing candidate fixture and includes
 * the unmodified real gamepad_runtime.c below. The prior fixture's fake runtime
 * and kernel symbols are renamed, NOT used by the candidate or real runtime.
 * All kernel, CCC/ATT, Bluetooth, USB and time behavior is deterministic mock.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "runtime integration requires enabled assertions"
#endif

/* The fixed kernel mock was already read by the real compiler preincludes.
 * Give the included fixture renamed stubs, so the actual runtime and snapshot
 * resolve the ORIGINAL kernel names to the mock implementations below. */
k_spinlock_key_t fixture_spin_lock(struct k_spinlock *);
void fixture_spin_unlock(struct k_spinlock *,k_spinlock_key_t);
int64_t fixture_uptime_get(void);
int fixture_msgq_put(struct k_msgq *,const void *,k_timeout_t);
int fixture_msgq_get(struct k_msgq *,void *,k_timeout_t);
int fixture_msgq_peek(struct k_msgq *,void *);
unsigned int fixture_msgq_num_used_get(struct k_msgq *);
int fixture_work_submit_to_queue(struct k_work_q *,struct k_work *);
int fixture_work_schedule_for_queue(struct k_work_q *,struct k_work_delayable *,k_timeout_t);
void fixture_work_queue_start(struct k_work_q *,void *,size_t,int,const struct k_work_queue_config *);
int fixture_bt_gatt_notify_cb(struct bt_conn *,struct bt_gatt_notify_params *);
#define bt_gatt_notify_cb fixture_bt_gatt_notify_cb
#define k_spin_lock fixture_spin_lock
#define k_spin_unlock fixture_spin_unlock
#define k_uptime_get fixture_uptime_get
#define k_msgq_put fixture_msgq_put
#define k_msgq_get fixture_msgq_get
#define k_msgq_peek fixture_msgq_peek
#define k_msgq_num_used_get fixture_msgq_num_used_get
#define k_work_submit_to_queue fixture_work_submit_to_queue
#define k_work_schedule_for_queue fixture_work_schedule_for_queue
#define k_work_queue_start fixture_work_queue_start
#define bbp_gamepad_context_epoch fixture_fake_context_epoch
#define bbp_gamepad_context_valid fixture_fake_context_valid
#define bbp_gamepad_transport_invalidate fixture_fake_transport_invalidate
#define bbp_gamepad_get_report fixture_fake_get_report
#define main fixture_candidate_main
#include "test_composite_ble_candidate.c"
/* The first strict build exposed this leftover test macro; clear it rather
 * than weakening -Werror or modifying the existing fixture. */
#undef RUN
#undef main
#undef bbp_gamepad_context_epoch
#undef bbp_gamepad_context_valid
#undef bbp_gamepad_transport_invalidate
#undef bbp_gamepad_get_report
#undef bt_gatt_notify_cb
#undef k_spin_lock
#undef k_spin_unlock
#undef k_uptime_get
#undef k_msgq_put
#undef k_msgq_get
#undef k_msgq_peek
#undef k_msgq_num_used_get
#undef k_work_submit_to_queue
#undef k_work_schedule_for_queue
#undef k_work_queue_start

/* Only the SDK boundary declarations needed by the actual runtime; no event
 * framework, actual atomic implementation or automatic scheduler is claimed. */
enum bt_security_err { BT_SECURITY_ERR_SUCCESS };
struct bt_conn_cb {
    void (*connected)(struct bt_conn *,uint8_t);
    void (*disconnected)(struct bt_conn *,uint8_t);
    void (*security_changed)(struct bt_conn *,bt_security_t,enum bt_security_err);
};
#define BT_CONN_CB_DEFINE(name) struct bt_conn_cb name
#undef SYS_INIT
#define SYS_INIT(...)
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
int k_work_reschedule_for_queue(struct k_work_q *,struct k_work_delayable *,k_timeout_t);
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c"

static int64_t runtime_now;
static uint8_t runtime_layer;
static bool usb_ready, runtime_work_pending;
static uint32_t usb_epoch, runtime_last_delay;
static unsigned int runtime_schedule_count, tap_cancels, sdk_subscription_rejections;
static void (*sdk_check_hook)(void);
struct usb_record { struct bbp_gamepad_report_body body; uint32_t usb_generation, context; };
static struct usb_record usb_sent[32];
static size_t usb_count;

int bt_gatt_notify_cb(struct bt_conn *c,struct bt_gatt_notify_params *p) {
    /* Model the Source 2 ENFORCE_SUBSCRIPTION option as one additional check.
     * The real SDK, its security and any concurrent execution are NOT tested. */
#if IS_ENABLED(CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION)
    if(p && p->attr==&hog_svc.attrs[VALUE]) {
        call_hook(&sdk_check_hook); /* Mock event after candidate gate, before SDK check. */
        if(!bt_gatt_is_subscribed(c,p->attr,BT_GATT_CCC_NOTIFY)) {
            ++sdk_subscription_rejections; return -EINVAL;
        }
    }
#endif
    return fixture_bt_gatt_notify_cb(c,p);
}

/* Real candidate and runtime both use these names, with no SDK call under the
 * modeled state spinlock. The scheduler records requests, never runs threads. */
k_spinlock_key_t k_spin_lock(struct k_spinlock *l) {
    assert(l && !l->held && !lock_depth); l->held=true; return lock_depth++;
}
void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t key) {
    assert(l && l->held && lock_depth==key+1U); l->held=false; --lock_depth;
}
int64_t k_uptime_get(void) { return runtime_now; }
int k_msgq_put(struct k_msgq *q,const void *data,k_timeout_t t) { return fixture_msgq_put(q,data,t); }
int k_msgq_get(struct k_msgq *q,void *data,k_timeout_t t) { return fixture_msgq_get(q,data,t); }
int k_msgq_peek(struct k_msgq *q,void *data) { return fixture_msgq_peek(q,data); }
unsigned int k_msgq_num_used_get(struct k_msgq *q) { return fixture_msgq_num_used_get(q); }
int k_work_submit_to_queue(struct k_work_q *q,struct k_work *w) { return fixture_work_submit_to_queue(q,w); }
int k_work_reschedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,k_timeout_t delay) {
    assert(!lock_depth && q==&gamepad_work_q && q->started && w && delay>=0);
    ++runtime_schedule_count; runtime_last_delay=(uint32_t)delay; runtime_work_pending=true; return 1;
}
int k_work_schedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,k_timeout_t delay) {
    assert(q && w && delay>=0);
    if(q==&gamepad_work_q) {
        assert(!lock_depth && q->started);
        if(runtime_work_pending) { return 0; }
        ++runtime_schedule_count; runtime_last_delay=(uint32_t)delay; runtime_work_pending=true; return 1;
    }
    return fixture_work_schedule_for_queue(q,w,delay);
}
void k_work_queue_start(struct k_work_q *q,void *stack,size_t size,int priority,
                        const struct k_work_queue_config *config) {
    assert(q && stack && config);
    if(q==&gamepad_work_q) { assert(size==2048U && priority==5); q->started=true; return; }
    fixture_work_queue_start(q,stack,size,priority,config);
}
uint8_t zmk_keymap_highest_layer_active(void) { assert(!lock_depth); return runtime_layer; }
uint8_t zmk_keymap_layer_index_to_id(uint8_t index) { return index; }
bool zmk_usb_is_hid_ready(void) { assert(!lock_depth); return usb_ready; }
uint32_t bbp9981_usb_hid_connection_epoch(void) { return usb_epoch; }
int tp_tap_output_cancel(void) { assert(!lock_depth); ++tap_cancels; return 0; }
int bbp_gamepad_usb_send(const struct bbp_gamepad_report_body *body,uint32_t expected_usb,
                          uint32_t expected_context) {
    assert(!lock_depth && body);
    if(!usb_ready || expected_usb!=usb_epoch) { return -ESTALE; }
    if(!bbp_gamepad_is_neutral(body) && !bbp_gamepad_context_valid(expected_context)) { return -ESTALE; }
    assert(usb_count<ARRAY_SIZE(usb_sent));
    usb_sent[usb_count++]=(struct usb_record){.body=*body,.usb_generation=expected_usb,.context=expected_context};
    return 0; /* USB boundary mock: no actual USB driver or device involved. */
}
static void release_runtime(void) {
    struct gp_frame f;
    while(pop_frame(&f)) { target_drop(&f.target); }
    if(pending_valid) { target_drop(&pending.target); }
    if(last_valid) { target_drop(&last.target); }
    pending_valid=last_valid=releasing=false;
}
static void runtime_setup(void) {
    release_runtime();
    setup(); /* Previous fixture resets actual HOG, peer objects and CCC mock. */
    assert(!lock_depth && refs==unrefs);
    bbp_gamepad_reset(&gamepad); state_lock=(struct k_spinlock){0}; head=count=0;
    state_destination_valid=false; state_conn_token=0; state_transport=state_profile=0;
    state_usb_epoch=state_context=0; context_epoch=1; ready=0; idle_rearm=0;
    context_was_game=0; pointer_mode_epoch=0; initialized_epoch=idle_init_epoch=UINT32_MAX;
    idle_init_attempts=0; idle_security_requested=false; idle_next_probe=0;
    runtime_now=100; runtime_layer=BBP_GAMEPAD_LAYER; usb_ready=true; usb_epoch=1;
    gamepad_work_q=(struct k_work_q){0}; runtime_work_pending=false;
    runtime_schedule_count=runtime_last_delay=tap_cancels=sdk_subscription_rejections=0;
    sdk_check_hook=NULL; usb_count=0;
    assert(gamepad_init()==0);
}
static void runtime_balanced(void) {
    release_runtime(); balanced();
}
static void pump_once(unsigned int advance) {
    runtime_now+=(int64_t)advance; runtime_work_pending=false; pump(NULL);
}
static void key(unsigned int position,unsigned int action,bool down) {
    assert(bbp_gamepad_key_event(position,action,down,runtime_now)==0);
}
static bool is_pad_neutral(const struct notification *r) {
    static const uint8_t neutral[10]={15,0,0,128,128,128,128,0,0,0};
    return r->attr==&hog_svc.attrs[VALUE] && r->len==10 && !memcmp(r->data,neutral,10);
}
static void real_allow_two(void) { assert(write_ccc(0,1,2)==2 && write_ccc(1,1,2)==2); }
static void test_actual_action_to_ble_wire(void) {
    static const uint8_t hat[BBP_GAMEPAD_ACTION_COUNT] = {
        0,4,6,2,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    };
    /* Independent protocol oracle: A/B/X/Y, shoulders, digital trigger bits,
     * View/Menu/Guide, L3/R3. */
    static const uint8_t buttons[BBP_GAMEPAD_ACTION_COUNT][2] = {
        {0,0},{0,0},{0,0},{0,0},{0,0x40},{0,0x20},{0,0x10},{0,0x08},
        {0,0x04},{0,0x02},{4,0},{8,0},{64,0},{32,0},{16,0},{0,1},{128,0},
        {0,0},{0,1},
    };
    /* Decode the 10-byte body as dpad, button bits, four centered axes, LT/RT.
     * The extra final byte is reserved and must remain zero. */
    for (unsigned int action=0; action<BBP_GAMEPAD_ACTION_COUNT; ++action) {
        uint8_t want[10]={hat[action],buttons[action][0],buttons[action][1],128,128,128,128,0,0,0};
        if (action==BBP_GP_LT) { want[7]=255; }
        if (action==BBP_GP_RT) { want[8]=255; }
        runtime_setup(); real_allow_two();
        key(action,action,true); pump_once(0);
        assert(sent_count==2 && is_pad_neutral(&sent[0]));
        assert(sent[1].conn==&peer[0] && sent[1].len==10 && !memcmp(sent[1].data,want,10));
        key(action,action,false); pump_once(0);
        assert(sent_count==3 && is_pad_neutral(&sent[2]));
        runtime_balanced();
    }
    /* Right-stick modifier changes only which axis pair carries sensor motion;
     * the descriptor remains a single gamepad report. */
    runtime_setup(); real_allow_two(); key(17,BBP_GP_RIGHT_STICK_MOD,true); pump_once(0);
    bbp_gamepad_trackpad_poll(40,-20,true,runtime_now); pump_once(0);
    assert(sent_count==3 && sent[2].data[3]==128 && sent[2].data[4]==128 &&
           (sent[2].data[5]!=128 || sent[2].data[6]!=128));
    key(17,BBP_GP_RIGHT_STICK_MOD,false); pump_once(0);
    assert(sent_count==4 && is_pad_neutral(&sent[3]));
    runtime_balanced();
}
static void test_actual_runtime_fifo(void) {
    runtime_setup();
    assert(fixture_fake_context_epoch()==10 && bbp_gamepad_context_epoch()==1);
    real_allow_two();
    assert(fixture_fake_context_epoch()==10 && bbp_gamepad_context_epoch()!=1);
    key(24,BBP_GP_A,true); assert(count==1 && state_destination_valid && peer[0].refs==2);
    uint8_t readback[10]; const uint8_t a_down[10]={15,0,64,128,128,128,128,0,0,0};
    assert(hog_svc.attrs[VALUE].read(&peer[0],hog_svc.attrs+VALUE,readback,10,0)==10 &&
           !memcmp(readback,a_down,10));
    assert(hog_svc.attrs[VALUE].read(&peer[1],hog_svc.attrs+VALUE,readback,10,0)==10 && readback[0]==15 && readback[2]==0);
    pump_once(0);
    assert(sent_count==2 && sent[0].conn==&peer[0] && is_pad_neutral(&sent[0]));
    assert(sent[1].conn==&peer[0] && sent[1].len==10 && !memcmp(sent[1].data,a_down,10));
    assert(last_valid && last.target.conn==&peer[0] && !pending_valid && !count);
    key(24,999,false); pump_once(0);
    assert(sent_count==3 && sent[2].conn==&peer[0] && is_pad_neutral(&sent[2]) && !last_valid);
    bbp_gamepad_trackpad_poll(10,0,true,runtime_now); pump_once(0);
    assert(sent_count==4 && sent[3].conn==&peer[0] && sent[3].len==10 && sent[3].data[3]==188);
    runtime_balanced();
}
static void test_actual_switch_and_usb(void) {
    runtime_setup(); real_allow_two(); key(24,BBP_GP_A,true); pump_once(0);
    assert(last_valid && last.target.conn==&peer[0]); size_t n=sent_count;
    choose(1); bbp_gamepad_transport_invalidate(); key(23,BBP_GP_B,true); pump_once(0);
    assert(sent_count==n+3 && sent[n].conn==&peer[0] && is_pad_neutral(&sent[n]));
    assert(sent[n+1].conn==&peer[1] && is_pad_neutral(&sent[n+1]));
    assert(sent[n+2].conn==&peer[1] && sent[n+2].len==10 && sent[n+2].data[2]==32);
    assert(last_valid && last.target.conn==&peer[1]); n=sent_count;
    endpoint.transport=ZMK_TRANSPORT_USB; bbp_gamepad_transport_invalidate();
    key(8,BBP_GP_UP,true); pump_once(0);
    assert(sent_count==n+1 && sent[n].conn==&peer[1] && is_pad_neutral(&sent[n]));
    assert(usb_count==2 && bbp_gamepad_is_neutral(&usb_sent[0].body));
    assert(sizeof(usb_sent[1].body)==9 && usb_sent[1].body.hat==0 && !usb_sent[1].body.buttons_lo);
    assert(usb_sent[1].usb_generation==usb_epoch && !last.target.conn);
    runtime_balanced();
}
static void test_actual_ccc_and_disconnect(void) {
    runtime_setup(); real_allow_two(); key(24,BBP_GP_A,true); pump_once(0);
    assert(last_valid && last.target.conn==&peer[0]); size_t n=sent_count;
    uint32_t old=bbp_gamepad_context_epoch();
    assert(write_ccc(0,0,2)==2 && !bbp_gamepad_context_valid(old));
    choose(1); bbp_gamepad_transport_invalidate(); pump_once(0);
    assert(sent_count==n && last_valid && last.target.conn==&peer[0]);
    for(unsigned int i=0;i<50;++i) { pump_once(10); }
    assert(!last_valid && peer[0].refs==1);
    for(size_t i=n;i<sent_count;++i) { assert(sent[i].conn!=&peer[0]); }
    runtime_balanced();

    runtime_setup(); real_allow_two(); key(24,BBP_GP_A,true); pump_once(0); n=sent_count;
    peer[0].state=BT_CONN_STATE_DISCONNECTED; gp_disconnected(&peer[0],0x13); pump_once(0);
    assert(!last_valid && peer[0].refs==1 && sent_count==n);
    choose(1); gp_connected(&peer[1],0); pump_once(0);
    assert(sent_count==n+1 && sent[n].conn==&peer[1] && is_pad_neutral(&sent[n]));
    runtime_balanced();
}
static void test_sdk_einval_discards_old_release(void) {
    runtime_setup(); real_allow_two(); key(24,BBP_GP_A,true); pump_once(0);
    assert(last_valid && last.target.conn==&peer[0]); size_t before=sent_count;
    choose(1); bbp_gamepad_transport_invalidate(); sdk_check_hook=off_primary;
    pump_once(0);
    /* The fake SDK rechecks after candidate's final gate, observes OFF and
     * returns -EINVAL. Real runtime treats this as terminal and drops last;
     * no neutral was accepted for peer0, and no host release is guaranteed. */
    assert(!sdk_check_hook && sdk_subscription_rejections==1 && !last_valid && peer[0].refs==1);
    for(size_t i=before;i<sent_count;++i) { assert(sent[i].conn!=&peer[0]); }
    runtime_balanced();
}
static void test_actual_fifo_overflow(void) {
    runtime_setup(); real_allow_two(); key(24,BBP_GP_A,true); pump_once(0);
    assert(last_valid && last.target.conn==&peer[0]);
    size_t before=sent_count; uint32_t old=bbp_gamepad_context_epoch();
    for(unsigned int i=0;i<34;++i) { key(23,BBP_GP_B,(i%2U)==0); }
    assert(!bbp_gamepad_context_valid(old)); /* 32-entry FIFO overflow invalidates. */
    for(unsigned int i=0;i<20;++i) { pump_once(10); }
    assert(!count && !pending_valid && !last_valid);
    for(size_t i=before;i<sent_count;++i) { assert(is_pad_neutral(&sent[i])); }
    runtime_balanced();
}
static void test_actual_other_peer_ccc_stales_queue(void) {
    runtime_setup(); real_allow_two(); key(24,BBP_GP_A,true); assert(count==1);
    uint32_t old=bbp_gamepad_context_epoch();
    assert(write_ccc(1,0,2)==2 && write_ccc(1,1,2)==2 && !bbp_gamepad_context_valid(old));
    pump_once(0);
    assert(!count && !pending_valid && !last_valid);
    for(size_t i=0;i<sent_count;++i) { assert(is_pad_neutral(&sent[i])); }
    runtime_balanced();
}
static void invalidate_in_notify(void) { bbp_gamepad_transport_invalidate(); }
static void test_actual_worker_postgate_event(void) {
    runtime_setup(); real_allow_two(); pump_once(0);
    assert(sent_count==1 && is_pad_neutral(&sent[0]));
    size_t before=sent_count; key(24,BBP_GP_A,true); notify_hook=invalidate_in_notify;
    pump_once(0);
    /* The event fires inside the mock SDK call after the candidate's final
     * gate: that A-down submission is not revoked. The real worker later
     * attempts neutral to the same captured peer; none of this is host ACK. */
    assert(!notify_hook && sent_count>=before+2);
    assert(sent[before].conn==&peer[0] && sent[before].data[2]==64);
    assert(sent[before+1].conn==&peer[0] && is_pad_neutral(&sent[before+1]));
    assert(!last_valid && !pending_valid && !count);
    runtime_balanced();
}
static void pump_in_precommit_window(void) { key(24,BBP_GP_A,true); pump_once(0); }
static void test_actual_runtime_precommit_counterexample(void) {
    runtime_setup(); real_allow_two(); size_t n=sent_count;
    precommit_hook=pump_in_precommit_window;
    assert(write_ccc(0,0,2)==2 && !precommit_hook);
    /* cfg_write invalidated, then the real producer+worker ran before the mock
     * SDK committed OFF; a fresh epoch can still see old ON and submit DOWN. */
    assert(sent_count==n+2 && sent[n].conn==&peer[0] && is_pad_neutral(&sent[n]));
    assert(sent[n+1].conn==&peer[0] && sent[n+1].len==10 && sent[n+1].data[2]==64);
    assert(last_valid && last.target.conn==&peer[0]);
    struct bbp_gamepad_report_body b=known();
    assert(send_to(&peer[0],&b,bbp_gamepad_context_epoch())==-EAGAIN);
    runtime_balanced();
}
int main(void) {
    assert(bbp_candidate_map_valid());
#define RUN(fn) do { fn(); puts("PASS " #fn); } while(0)
    RUN(test_actual_action_to_ble_wire); RUN(test_actual_runtime_fifo); RUN(test_actual_switch_and_usb); RUN(test_actual_ccc_and_disconnect);
    RUN(test_sdk_einval_discards_old_release); RUN(test_actual_fifo_overflow); RUN(test_actual_other_peer_ccc_stales_queue);
    RUN(test_actual_worker_postgate_event); RUN(test_actual_runtime_precommit_counterexample);
    puts("LIMIT actual runtime+candidate+fixed HOG, but mocked scheduling/CCC/ATT/USB; no Zephyr concurrency, host ACK, ARM or device proof");
    return 0;
}
