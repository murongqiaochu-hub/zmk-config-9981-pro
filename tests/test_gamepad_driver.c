/* Actual A320 + actual micro runtime. Gamepad transport/state publication is
 * stubbed here; the actual gamepad runtime is compiled in its separate suite.
 * No physical sensor, RTOS, mouse/HID host or game compatibility is tested. */
#define CONFIG_BBP9981_GAMEPAD 1
#include "test_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap_runtime.h"
static bool game_mode;
static uint32_t game_pointer_epoch;
static unsigned game_samples;
static int16_t game_dx,game_dy;
static bool game_valid;
static struct bbp_gamepad_state sensor_gamepad;
static int64_t k_uptime_get(void){return (int64_t)fake_now;}
bool bbp_gamepad_layer_active(void){return game_mode;}
uint32_t bbp_gamepad_pointer_epoch(void){return game_pointer_epoch;}
void bbp_gamepad_trackpad_poll(int16_t dx,int16_t dy,bool valid,int64_t now){
    assert(game_mode);game_samples++;game_dx=dx;game_dy=dy;game_valid=valid;
    if(valid)(void)bbp_gamepad_motion(&sensor_gamepad,dx,dy,now);
    else (void)bbp_gamepad_motion_cancel(&sensor_gamepad);
}
#define main original_micro_suite
#define fixture runtime_fixture
#include "test_micro_runtime.c"
#undef fixture
#undef main
#define CONFIG_A320_TAP_OUTPUT 1
#define CONFIG_A320_MICROTAP 1
void tp_microtap_shoulder_motion(uint32_t now){(void)now;}
#define main original_pointer_suite
#include "test_pointer.c"
#undef main
static void ownership(bool game){
    if(game_mode!=game){
        game_mode=game;game_pointer_epoch++;
        tp_microtap_runtime_interrupt();bbp_gamepad_reset(&sensor_gamepad);
    }
}
static void enter_game(void){ownership(true);}
static void enter_exit_game(void){ownership(true);ownership(false);}
static void bridge_fixture(bool tap){
    game_mode=false;game_pointer_epoch=last_gamepad_pointer_epoch=game_samples=0;
    game_dx=game_dy=0;game_valid=false;bbp_gamepad_reset(&sensor_gamepad);
    fixture();runtime_fixture(tap);
}
static void default_left_and_right_modifier_no_mouse(void){
    bridge_fixture(true);ownership(true);poll(12,-7,10);
    assert(game_samples==1&&game_valid&&game_dx==12&&game_dy==-7);
    struct bbp_gamepad_report_body r=bbp_gamepad_snapshot(&sensor_gamepad);
    assert(r.x==72&&r.y==-42&&!r.z&&!r.rz);
    assert(!report_count&&!submissions&&!tp_microtap_runtime_pending()&&tp_microtap_runtime_enabled());
    assert(bbp_gamepad_key(&sensor_gamepad,3,BBP_GP_RIGHT_STICK_MOD,true));
    poll(-10,6,20);r=bbp_gamepad_snapshot(&sensor_gamepad);
    assert(!r.x&&!r.y&&r.z==-60&&r.rz==36&&!report_count&&!submissions);
    idle(139);r=bbp_gamepad_snapshot(&sensor_gamepad);assert(r.z==-60);
    idle(140);r=bbp_gamepad_snapshot(&sensor_gamepad);assert(!r.z&&!r.rz);
    assert(!report_count&&!submissions&&tp_microtap_runtime_enabled());
}
static void latched_scroll_and_slow_do_not_steal_stick(void){
    bridge_fixture(false);
    key(41,true,1);key(41,false,10);key(41,true,20);key(41,false,30);
    assert(tp_scroll_mode_active());key(37,true,35);
    assert(tp_mouse_orientation_toggle()); /* The volatile mouse flag must not rotate stick axes. */
    ownership(true);poll(4,5,50);
    assert(game_valid&&game_dx==4&&game_dy==5&&!report_count&&!submissions);
    struct bbp_gamepad_report_body r=bbp_gamepad_snapshot(&sensor_gamepad);
    assert(r.x==24&&r.y==30&&!r.z&&!r.rz);
    assert(sum_dx==0&&sum_dy==0&&sample_cnt==0&&cursor_motion.x.remainder==0&&cursor_motion.y.remainder==0);
    assert(tp_scroll_mode_active());ownership(false);assert(tp_scroll_mode_active());
}
static void sensor_fault_centres_without_mouse(void){
    bridge_fixture(true);ownership(true);
    assert(bbp_gamepad_key(&sensor_gamepad,34,BBP_GP_A,true));poll(10,0,10);
    assert(bbp_gamepad_snapshot(&sensor_gamepad).x==60);
    burst_error=-EIO;poll(50,50,20);
    struct bbp_gamepad_report_body r=bbp_gamepad_snapshot(&sensor_gamepad);
    assert(!game_valid&&!r.x&&!r.y&&!r.z&&!r.rz&&r.buttons_lo==1&&!report_count&&!submissions);
    burst_error=0;poll(-5,4,30);assert(bbp_gamepad_snapshot(&sensor_gamepad).x==-30);
    fake_gpio_state=-EIO;fake_now=40;report_count=0;
    a320_poll_work_handler(&sensor_data.poll_work.work);r=bbp_gamepad_snapshot(&sensor_gamepad);
    assert(!game_valid&&!r.x&&!r.y&&r.buttons_lo==1&&!report_count&&!tp_is_touched());
}
static void exit_restores_without_replay(void){
    bridge_fixture(false);ownership(true);poll(10,2,10);assert(!report_count);
    ownership(false);poll(1,0,20);expect_xy(1,0);assert(!tp_microtap_runtime_enabled());
    bridge_fixture(true);poll(8,0,10);poll(8,0,20);poll(8,0,30);
    assert(!submissions&&!report_count&&tp_microtap_runtime_pending());
    ownership(true);ownership(false); /* Entire game interval between sensor polls. */
    idle(40);assert(!submissions&&!report_count&&!tp_microtap_runtime_pending());
    /* Keep the original <=100ms poll-gap and auto rearm/spacing policy. */
    for(uint32_t t=50;t<=300;t+=10)idle(t);
    poll(5,0,310);for(uint32_t t=320;t<=420;t+=10)idle(t);idle(430);
    assert(submissions==1&&tp_microtap_runtime_enabled());complete();
}
static void late_gate_and_fraction_ownership(void){
    bridge_fixture(true);poll(8,0,10);poll(8,0,20);poll(8,0,30);
    for(uint32_t t=40;t<=140;t+=10)idle(t);
    after_context_hook=enter_game;idle(150);
    assert(game_mode&&!submissions&&!report_count&&!tp_microtap_runtime_pending());
    bridge_fixture(true);poll(8,0,10);poll(8,0,20);poll(8,0,30);
    for(uint32_t t=40;t<=140;t+=10)idle(t);
    after_context_hook=enter_exit_game;idle(150);
    assert(!game_mode&&!submissions&&!report_count&&!tp_microtap_runtime_pending());
    bridge_fixture(false);key(37,true,1);ownership(true);
    struct micro_cursor_context ctx={.dev=&sensor,.slow=true};
    report_count=0;micro_emit_cursor(&ctx,1,0,10);
    assert(!report_count&&cursor_motion.x.remainder==240);
    ownership(false);poll(1,0,20);expect_xy(0,0);
    assert(cursor_motion.x.remainder==240); /* Fresh fraction, not old 240+240 -> mouse1. */
}
int main(void){
    game_mode=false;assert(original_micro_suite()==0);runtime_fixture(false);assert(original_pointer_suite()==0);
    fprintf(stderr,"DRIVER CASE default-left/right-modifier\n");default_left_and_right_modifier_no_mouse();
    fprintf(stderr,"DRIVER CASE latched-scroll/slow\n");latched_scroll_and_slow_do_not_steal_stick();
    fprintf(stderr,"DRIVER CASE sensor-fault\n");sensor_fault_centres_without_mouse();
    fprintf(stderr,"DRIVER CASE exit-restore\n");exit_restores_without_replay();
    fprintf(stderr,"DRIVER CASE late-gate/fractions\n");late_gate_and_fraction_ownership();
    puts("PASS actual A320/micro ownership: original normal suites with gamepad enabled; raw left/right stick routing, no XY/wheel/auto click in game, scroll/slow preserved, GPIO/I2C axis fault, 120ms local centre, normal restore, transient-mode cancellation and late click/cursor gate; host only");
    return 0;
}
