/* Long-sequence invariants against the real runtime. Simulated clock, not a
 * physical-duration/USB/radio burn-in. Never reset runtime/peer state per cycle. */
#define main existing_gamepad_runtime_suite
#include "test_gamepad_runtime.c"
#undef main

static uint64_t cycles, sends;
static void check_idle(void) {
    assert(!lock_depth && count == 0 && !pending_valid && !last_valid && !releasing);
    assert(peer_a.refs == 1 && peer_b.refs == 1);
    struct bbp_gamepad_report_body body=bbp_gamepad_snapshot(&gamepad);
    assert(bbp_gamepad_is_neutral(&body));
}
static void drain_to_idle(void) {
    run(80,10);
    check_idle();
    sends += accepted_count;accepted_count=0;
}
static void fresh_route(bool ble, bool second) {
    forced_send_error=0;peer_a.connected=peer_b.connected=true;
    peer_a.subscribed=peer_b.subscribed=true;peer_a.secure=peer_b.secure=true;
    usb_ready=true;usb_epoch++;
    selected.transport=ble?ZMK_TRANSPORT_BLE:ZMK_TRANSPORT_USB;
    selected.ble.profile_index=second?1U:0U;active_peer=second?&peer_b:&peer_a;
    highest_layer=3;context_changed(NULL);run(8,0);
}
static void recoverable_sequences(void) {
    fixture(false);
    const int64_t checkpoints[]={INT64_C(2147483600),INT64_C(4294967200),
        INT64_C(4294967300),INT64_C(8589934600),INT64_C(10368000000)};
    unsigned stage=0;
    for(unsigned i=0;i<30000U;i++) {
        if(i%6000U==0) { now_ms=checkpoints[stage++]; }
        else { now_ms+=10000; }
        bool ble=(i&1U)!=0, second=(i&2U)!=0;
        fresh_route(ble,second);
        unsigned begin=accepted_count;
        assert(begin && bbp_gamepad_is_neutral(&accepted[begin-1].body));
        edge(35,BBP_GP_A,true);motion(12,-6);run(1,0);
        assert(last_valid && last.body.buttons_lo==1);
        if(i%5U==0) {
            /* FIFO overload cannot retain old held actions or leak its refs. */
            for(unsigned n=0;n<GP_QUEUE_CAPACITY+2U;n++) { edge(24,BBP_GP_B,(n&1U)==0); }
        } else if(i%5U==1) {
            /* Transient backpressure, followed by TTL expiration/recovery. */
            forced_send_error=-EAGAIN;edge(24,BBP_GP_B,true);run(1,0);
            now_ms+=251;forced_send_error=0;
        } else if(i%5U==2 && ble) {
            /* A disconnected old peer must never keep a captured reference. */
            active_peer->connected=false;gp_disconnected(active_peer,0);
        } else if(i%5U==3 && ble) {
            /* Bounded failed old-peer neutral, no notification/host ACK claim. */
            active_peer->subscribed=false;highest_layer=0;context_changed(NULL);
        } else {
            edge(35,UINT32_MAX,false);motion(0,0);now_ms+=120;
        }
        highest_layer=0;context_changed(NULL);drain_to_idle();
        /* Recovery uses the same initialized runtime, not a reboot/fixture. */
        fresh_route(ble,second);
        edge(24,BBP_GP_B,true);run(1,0);
        assert(last_valid && last.body.buttons_lo==2 && !last.body.buttons_hi);
        edge(24,UINT32_MAX,false);run(2,0);drain_to_idle();
        cycles++;
    }
    no_leaks();
}
int main(void) {
    recoverable_sequences();
    printf("PASS stability gamepad: %llu uninterrupted route/fault/recovery cycles, %llu accepted host-model reports; bounded FIFO/ref cleanup, fresh post-recovery keys, clocks beyond signed/u32 boundaries and 120days; NOT real-time/device burn-in\n",
           (unsigned long long)cycles,(unsigned long long)sends);
    return 0;
}
