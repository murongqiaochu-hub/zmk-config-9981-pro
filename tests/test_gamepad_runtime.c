#include <stdio.h>
#include "gamepad_runtime_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/behavior_gamepad.c"

static unsigned tap_bridge_cancels;
int tp_tap_output_cancel(void){assert(!lock_depth);tap_bridge_cancels++;return 0;}
uint32_t bbp9981_usb_hid_connection_epoch(void){return usb_epoch;}
static int accept(uint8_t transport,struct bt_conn *conn,const struct bbp_gamepad_report_body *body,uint32_t epoch){
    assert(!lock_depth);
    if(during_send){void(*fn)(void)=during_send;during_send=NULL;fn();}
    if(!bbp_gamepad_is_neutral(body)&&!bbp_gamepad_context_valid(epoch))return -ESTALE;
    if(forced_send_error)return forced_send_error;
    if(conn){assert(conn->refs>1);if(!conn->connected)return -ENOTCONN;if(!conn->subscribed)return -EAGAIN;if(!conn->secure)return -EACCES;}
    assert(accepted_count<4096);
    accepted[accepted_count++]=(struct accepted_frame){transport,conn?conn->id:0,epoch,*body};
    return 0;
}
int bbp_gamepad_usb_send(const struct bbp_gamepad_report_body *b,uint32_t u,uint32_t e){
    if(u!=usb_epoch||!usb_ready)return -ESTALE;
    return accept(0,NULL,b,e);
}
#ifndef TEST_LINK_REAL_BLE_DISABLED
int bbp_gamepad_ble_send(struct bt_conn *c,const struct bbp_gamepad_report_body *b,uint32_t e){
    if(!c)return -ENOTCONN;
    if(!bbp_gamepad_is_neutral(b)&&(selected.transport!=ZMK_TRANSPORT_BLE||c!=active_peer))return -ESTALE;
    return accept(1,c,b,e);
}
#endif
static void cleanup(void){
    struct gp_frame f;
    while(pop_frame(&f))target_drop(&f.target);
    if(pending_valid)target_drop(&pending.target);
    if(last_valid)target_drop(&last.target);
    pending_valid=last_valid=releasing=false;
}
static void fixture(bool ble){
    cleanup();assert(!lock_depth);
    bbp_gamepad_reset(&gamepad);head=count=0;state_destination_valid=false;
    state_conn_token=0;context_was_game=pointer_mode_epoch=0;work_pending=false;tap_bridge_cancels=0;
    context_epoch=1;ready=0;initialized_epoch=idle_init_epoch=UINT32_MAX;
    idle_init_attempts=0;idle_security_requested=false;idle_rearm=0;idle_next_probe=0;
    reset_layer_order();
    now_ms=100;highest_layer=3;usb_ready=true;usb_epoch=1;
    peer_a=(struct bt_conn){.id=10,.refs=1,.connected=true,.subscribed=true,.secure=true};
    peer_b=(struct bt_conn){.id=20,.refs=1,.connected=true,.subscribed=true,.secure=true};
    active_peer=&peer_a;selected=(struct zmk_endpoint_instance){.transport=ble?ZMK_TRANSPORT_BLE:ZMK_TRANSPORT_USB};
    schedules=security_requests=accepted_count=0;forced_send_error=0;during_send=NULL;
    memset(accepted,0,sizeof(accepted));assert(gamepad_init()==0);
}
static void edge(unsigned position,unsigned action,bool down){
    struct zmk_behavior_binding b={.param1=action};
    struct zmk_behavior_binding_event e={.position=position,.timestamp=now_ms};
    assert(binding_key(&b,e,down)==0);
}
static void run(unsigned n,unsigned delta){for(unsigned i=0;i<n;i++){now_ms+=delta;work_pending=false;pump(NULL);}}
static void exit_game(void){highest_layer=0;bbp_gamepad_transport_invalidate();}
static void no_leaks(void){cleanup();assert(peer_a.refs==1&&peer_b.refs==1);}
static void usb_and_owner(void){
    fixture(false);edge(8,BBP_GP_UP,true);edge(23,BBP_GP_B,true);edge(0,BBP_GP_L,true);run(1,0);
    assert(accepted_count==4&&bbp_gamepad_is_neutral(&accepted[0].body));
    assert(accepted[3].body.hat==0&&(accepted[3].body.buttons_lo&66)==66);
    unsigned before=count;edge(23,BBP_GP_A,true);assert(count==before);
    edge(24,BBP_GP_B,true);edge(23,999,false);run(1,0);
    assert(accepted[accepted_count-1].body.buttons_lo&2);
    edge(24,999,false);edge(8,BBP_GP_UP,false);edge(0,BBP_GP_L,false);run(1,0);
    assert(bbp_gamepad_is_neutral(&accepted[accepted_count-1].body)&&!last_valid);
    no_leaks();
}
static void queued_exit(void){
    fixture(false);edge(24,BBP_GP_A,true);run(1,0);assert(last_valid);
    edge(8,BBP_GP_UP,true);unsigned n=accepted_count;exit_game();run(8,10);
    assert(accepted_count==n+2&&bbp_gamepad_is_neutral(&accepted[n].body)&&
           bbp_gamepad_is_neutral(&accepted[n+1].body)); /* Release owner, then centre new context. */
    struct bbp_gamepad_report_body b;bbp_gamepad_get_report(0,&b);assert(bbp_gamepad_is_neutral(&b));
    assert(!count&&!last_valid&&!pending_valid);no_leaks();
    fixture(false);edge(24,BBP_GP_A,true);during_send=exit_game;run(8,10);
    for(unsigned i=0;i<accepted_count;i++)assert(bbp_gamepad_is_neutral(&accepted[i].body));
    no_leaks();
}
static void peers_and_transport(void){
    fixture(true);edge(24,BBP_GP_A,true);run(1,0);assert(last_valid&&last.target.conn==&peer_a);
    unsigned n=accepted_count;
    active_peer=&peer_b;selected.ble.profile_index=1;bbp_gamepad_transport_invalidate();
    edge(23,BBP_GP_B,true);run(8,0);
    assert(accepted_count==n+3);
    assert(accepted[n].peer==10&&bbp_gamepad_is_neutral(&accepted[n].body));
    assert(accepted[n+1].peer==20&&bbp_gamepad_is_neutral(&accepted[n+1].body));
    assert(accepted[n+2].peer==20&&accepted[n+2].body.buttons_lo==2);
    n=accepted_count;selected.transport=ZMK_TRANSPORT_USB;bbp_gamepad_transport_invalidate();
    edge(8,BBP_GP_UP,true);run(8,0);
    assert(accepted[n].peer==20&&bbp_gamepad_is_neutral(&accepted[n].body));
    assert(accepted[n+1].transport==0&&bbp_gamepad_is_neutral(&accepted[n+1].body));
    assert(accepted[n+2].body.hat==0);no_leaks();
}
static void disconnect_and_rearm(void){
    fixture(true);edge(24,BBP_GP_A,true);run(1,0);peer_a.connected=false;
    gp_disconnected(&peer_a,0);run(8,10);assert(!last_valid);
    peer_a.connected=true;gp_connected(&peer_a,0);run(8,10);
    unsigned n=accepted_count;
    edge(23,BBP_GP_B,true);run(1,0);
    assert(accepted_count==n+1&&accepted[n].body.buttons_lo==2); /* Reconnect already centred; no old A replay. */
    no_leaks();
    fixture(false);usb_ready=false;edge(24,BBP_GP_A,true);usb_ready=true;run(8,10);
    assert(accepted_count==1&&bbp_gamepad_is_neutral(&accepted[0].body));
    edge(23,BBP_GP_B,true);run(1,0);assert(accepted[1].body.buttons_lo==2);no_leaks();
}
static void full_expiry_and_security(void){
    fixture(true);edge(24,BBP_GP_A,true);run(1,0);unsigned n=accepted_count;
    for(unsigned i=0;i<34;i++)edge(23,BBP_GP_B,(i%2)==0);
    run(20,10);assert(!count&&!pending_valid&&!last_valid);
    for(unsigned i=n;i<accepted_count;i++)assert(bbp_gamepad_is_neutral(&accepted[i].body));
    no_leaks();
    fixture(true);forced_send_error=-EAGAIN;edge(24,BBP_GP_A,true);run(1,0);
    now_ms+=300;forced_send_error=0;run(8,0);
    edge(23,BBP_GP_B,true);run(1,0);assert(accepted[accepted_count-1].body.buttons_lo==2);no_leaks();
    fixture(true);peer_a.secure=false;edge(24,BBP_GP_A,true);run(6,10);assert(security_requests==1);
    peer_a.secure=true;gp_security_changed(&peer_a,2,0);run(2,0);assert(last_valid);no_leaks();
    fixture(true);edge(24,BBP_GP_A,true);run(1,0);peer_a.subscribed=false;exit_game();run(50,10);
    assert(!last_valid&&peer_a.refs==1);no_leaks(); /* Bounded failed release, not a host ACK. */
}
static void idle_neutral_before_first_key(void){
    fixture(true);highest_layer=0;run(6,10);
    assert(accepted_count==1&&bbp_gamepad_is_neutral(&accepted[0].body));
    assert(accepted[0].body.hat==8&&!last_valid&&!state_destination_valid);no_leaks();
    fixture(true);highest_layer=0;peer_a.subscribed=false;run(8,10);
    assert(!accepted_count&&!security_requests&&peer_a.refs==1);
    peer_a.subscribed=true;bbp_gamepad_transport_invalidate();run(1,0);
    assert(accepted_count==1&&bbp_gamepad_is_neutral(&accepted[0].body));no_leaks();
    fixture(true);highest_layer=0;peer_a.secure=false;run(10,10);
    assert(!accepted_count&&security_requests==1);
    peer_a.secure=true;gp_security_changed(&peer_a,2,0);run(1,0);
    assert(accepted_count==1&&bbp_gamepad_is_neutral(&accepted[0].body));no_leaks();
    fixture(true);edge(24,BBP_GP_A,true);run(1,0);
    uint32_t old_epoch=bbp_gamepad_context_epoch();peer_a.secure=false;
    gp_security_changed(&peer_a,1,0);assert(!bbp_gamepad_context_valid(old_epoch));run(2,10);
    peer_a.secure=true;gp_security_changed(&peer_a,2,0);run(8,0);
    edge(23,BBP_GP_B,true);run(1,0);
    assert(accepted[accepted_count-1].body.buttons_lo==2);no_leaks(); /* Low security cancels old held A. */
    fixture(true);highest_layer=0;peer_a.secure=false;run(45,10);
    assert(idle_init_attempts==40&&!accepted_count&&security_requests==1);
    gp_security_changed(&peer_b,2,0);run(1,0); /* Other peer ready must NOT request L2 again for A. */
    assert(!accepted_count&&security_requests==1);
    peer_a.secure=true;gp_security_changed(&peer_a,2,0);run(1,0);
    assert(accepted_count==1&&bbp_gamepad_is_neutral(&accepted[0].body)&&!state_destination_valid);no_leaks();
    /* A stays subscribed, so aggregate CCC remains1 when B later subscribes.
     * Do not synthesize cfg_changed: low-rate per-peer neutral must recover. */
    fixture(true);highest_layer=0;active_peer=&peer_b;selected.ble.profile_index=1;
    peer_a.subscribed=true;peer_b.subscribed=false;run(45,10);
    assert(idle_init_attempts==40&&!accepted_count&&!security_requests);
    uint32_t same_epoch=bbp_gamepad_context_epoch();peer_b.subscribed=true;run(1,500);
    assert(bbp_gamepad_context_epoch()==same_epoch&&accepted_count==1&&accepted[0].peer==20&&
           bbp_gamepad_is_neutral(&accepted[0].body));
    run(5,500);assert(accepted_count==1);no_leaks();
}
static void motion(int16_t dx,int16_t dy){bbp_gamepad_trackpad_poll(dx,dy,true,now_ms);}
static void analog_coalescing_and_quiet(void){
    fixture(false);
    for(unsigned i=0;i<10000;i++)motion(20,0);
    assert(count==0&&!pending_valid&&accepted_count==0); /* Motion never fills the edge FIFO. */
    run(1,0);assert(accepted_count==2&&accepted[1].body.x==127&&accepted[1].body.hat==8);
    assert(!accepted[1].body.z&&!accepted[1].body.rz&&!accepted[1].body.buttons_lo);
    assert(scheduled_delay==50);
    run(1,119);assert(accepted_count==2&&scheduled_delay==1);
    run(1,1);assert(accepted_count==3&&bbp_gamepad_is_neutral(&accepted[2].body)&&!last_valid);
    no_leaks();
    fixture(false);for(unsigned i=0;i<8;i++)motion(20,0);run(1,0);
    now_ms+=110;motion(20,0); /* Same saturated value returns false, but refreshes quiet time. */
    run(1,10);assert(accepted[accepted_count-1].body.x==127);
    run(1,109);assert(last_valid&&accepted[accepted_count-1].body.x==127);
    run(1,1);assert(!last_valid&&bbp_gamepad_is_neutral(&accepted[accepted_count-1].body));no_leaks();
}
static void axes_while_button_held_and_fault(void){
    fixture(true);edge(34,BBP_GP_A,true);run(1,0);
    motion(10,-6);run(1,0);
    assert(last_valid&&last.body.buttons_lo==1&&last.body.x==60&&last.body.y==-36);
    run(1,120);assert(last_valid&&last.body.buttons_lo==1&&!last.body.x&&!last.body.y);
    motion(-8,4);run(1,0);assert(last.body.x==-48&&last.body.y==24);
    uint32_t epoch=bbp_gamepad_context_epoch();
    bbp_gamepad_trackpad_poll(100,100,false,now_ms);run(1,0);
    assert(bbp_gamepad_context_epoch()==epoch&&last_valid&&last.body.buttons_lo==1&&
           !last.body.x&&!last.body.y&&!last.body.z&&!last.body.rz);
    edge(34,999,false);run(1,0);assert(!last_valid);no_leaks();
}
static void right_mode_click_owner_and_axes_epoch(void){
    fixture(false);motion(6,-4);run(1,0);assert(last.body.x==36&&last.body.y==-24);
    unsigned before=accepted_count;
    edge(3,BBP_GP_RIGHT_STICK_MOD,true);edge(4,BBP_GP_STICK_CLICK,true);
    motion(8,-5);run(1,0);
    assert(last.body.buttons_hi==64&&last.body.z==48&&last.body.rz==-30&&!last.body.x&&!last.body.y);
    for(unsigned i=before;i<accepted_count;i++)assert(!accepted[i].body.x&&!accepted[i].body.y);
    edge(3,999,false);motion(3,3);run(1,0);
    assert(last.body.buttons_hi==64&&last.body.x==18&&last.body.y==18&&!last.body.z&&!last.body.rz);
    edge(4,999,false);run(1,0);assert(!last.body.buttons_hi);run(1,120);assert(!last_valid);
    no_leaks();
    fixture(false);highest_layer=0;context_changed(NULL);
    uint32_t p=bbp_gamepad_pointer_epoch();highest_layer=3;context_changed(NULL);
    highest_layer=0;context_changed(NULL);assert(bbp_gamepad_pointer_epoch()==p+2&&tap_bridge_cancels==2);
    no_leaks();
}
static void observed_tick_and_sample_freshness(void){
    fixture(false);motion(8,0);run(1,0);assert(last.body.x==48);
    now_ms=101;struct bbp_gamepad_report_body r;bbp_gamepad_get_report(0,&r);
    bbp_gamepad_trackpad_poll(4,0,true,100);run(1,0);
    assert(last_valid&&last.body.x==36); /* Getter's newer tick is not a backwards sensor clock. */
    now_ms=221;bbp_gamepad_trackpad_poll(20,0,true,101);run(1,0);
    assert(!last_valid&&bbp_gamepad_is_neutral(&accepted[accepted_count-1].body));
    no_leaks();
}
static void new_motion_during_send(void){motion(20,0);}
static void scheduling_and_unnotified_peer_change(void){
    fixture(false);edge(34,BBP_GP_A,true);forced_send_error=-EAGAIN;during_send=new_motion_during_send;
    run(1,0);assert(work_pending&&scheduled_delay==0); /* Deferred retry cannot replace producer wake0. */
    forced_send_error=0;run(1,0);assert(last_valid&&last.body.buttons_lo==1&&last.body.x==95);no_leaks();
    fixture(true);edge(34,BBP_GP_A,true);motion(10,0);run(1,0);
    active_peer=&peer_b; /* Same profile index, different conn, callback deliberately delayed. */
    struct bbp_gamepad_report_body r;bbp_gamepad_get_report(1,&r);assert(bbp_gamepad_is_neutral(&r));
    uint32_t e=bbp_gamepad_context_epoch();motion(12,0);assert(!bbp_gamepad_context_valid(e));
    run(8,0);motion(4,0);run(1,0);
    assert(last_valid&&last.target.conn==&peer_b&&last.body.x==24&&!last.body.buttons_lo);no_leaks();
}
int main(void){
    usb_and_owner();queued_exit();peers_and_transport();disconnect_and_rearm();full_expiry_and_security();
    idle_neutral_before_first_key();
    analog_coalescing_and_quiet();axes_while_button_held_and_fault();
    right_mode_click_owner_and_axes_epoch();observed_tick_and_sample_freshness();
    scheduling_and_unnotified_peer_change();
    puts("PASS actual Xbox-layout runtime: FIFO button edges, 10000 coalesced motion samples, quiet deadline refresh/expiry with held buttons, fault-only-axis reset, BB/captured R3, pointer epoch, producer wake0 precedence, old/new peer neutrality, route/conn token, full/expired cancellation, security and balanced refs; host mocks only");
    return 0;
}
