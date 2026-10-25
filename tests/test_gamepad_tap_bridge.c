/* Actual asynchronous mouse bridge + GP ownership, not the immediate micro
 * submit stub. The original bridge suite also runs with GP compiled but OFF. */
#define CONFIG_BBP9981_GAMEPAD 1
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h"
static bool bridge_game_mode;
static uint32_t bridge_game_epoch;
static unsigned bridge_gp_reads,bridge_flip_on_read;
bool bbp_gamepad_layer_active(void){
    bridge_gp_reads++;
    if(bridge_flip_on_read&&bridge_gp_reads==bridge_flip_on_read){
        bridge_game_mode=true;bridge_game_epoch++;bridge_flip_on_read=0;
    }
    return bridge_game_mode;
}
uint32_t bbp_gamepad_pointer_epoch(void){return bridge_game_epoch;}
#define main original_async_bridge_suite
#include "test_tap_output.c"
#undef main
static void bridge_boot(void){
    bridge_game_mode=false;bridge_game_epoch=bridge_gp_reads=bridge_flip_on_read=0;cold_boot();
}
static void change_game(bool on,bool cancel){
    if(bridge_game_mode!=on){bridge_game_mode=on;bridge_game_epoch++;}
    if(cancel)assert(tp_tap_output_cancel()==0); /* Matches GP context callback, outside lock. */
}
static void queued_before_game_never_becomes_down(void){
    for(unsigned cycle=0;cycle<2;cycle++)for(unsigned cancel=0;cancel<2;cancel++){
        bridge_boot();assert(tp_tap_output_submit()==0);
        assert(tp_tap_output_read_inflight().phase==TP_TAP_OUTPUT_QUEUED);
        change_game(true,cancel!=0);if(cycle)change_game(false,cancel!=0);
        run_worker_at(1);
        assert(!mock_press_calls&&!mock_release_calls&&!mock_send_calls&&!mock_refs[0]);assert_idle();
    }
}
static void idle_gap_and_fresh_context(void){
    bridge_boot();change_game(true,true);change_game(false,true);
    assert(tp_tap_output_submit()==-EACCES); /* Old producer, still old normal context. */
    assert(!mock_press_calls&&!mock_send_calls);assert_idle();
    assert(tp_tap_output_update_context(context(2))==0);start_down(2);
    run_worker_at(2+TP_TAP_OUTPUT_HOLD_MS);assert(mock_report_count==2);assert_idle();
    bridge_boot();change_game(true,false);
    assert(tp_tap_output_update_context(context(2))==0);
    assert(tp_tap_output_submit()==-EACCES&&!mock_press_calls&&!mock_send_calls);
}
static void admission_after_ref_acquire_drops_down_but_corrects_up(void){
    bridge_boot();assert(tp_tap_output_submit()==0);
    /* First remaining GP check is QUEUED admission; second is DOWN precommit
     * after taking our ref. Model game already entered at that final check. */
    bridge_flip_on_read=bridge_gp_reads+2;run_worker_at(0);
    assert(bridge_game_mode&&mock_press_calls==1&&mock_release_calls==1&&mock_refs[0]==0);
    assert(!mock_send_calls&&!mock_report_count);
    assert(tp_tap_output_read_inflight().phase==TP_TAP_OUTPUT_RELEASING);
    run_worker_at(1);
    assert(mock_send_calls==1&&mock_report_count==1&&mock_reports[0].body.buttons==0);
    assert(tp_tap_output_read_inflight().down_reports==0&&tp_tap_output_read_inflight().up_reports==1);
    assert_idle();
}
static void accepted_down_releases_only_our_reference_in_game(void){
    bridge_boot();start_down(0);assert(mock_report_count==1&&mock_refs[0]==1);
    assert(zmk_hid_mouse_button_press(0)==0&&mock_refs[0]==2); /* Foreign physical owner. */
    change_game(true,true);run_worker_at(TP_TAP_OUTPUT_HOLD_MS);
    assert(mock_release_calls==1&&mock_refs[0]==1&&mock_report_count==2);
    assert(mock_reports[1].body.buttons==1); /* UP for our token is not foreign UP. */
    assert(tp_tap_output_read_inflight().up_reports==1);assert_idle();
    assert(tp_tap_output_submit()==-EACCES); /* GP blocks new DOWN, never required UP. */
}
int main(void){
    assert(original_async_bridge_suite()==0);
    queued_before_game_never_becomes_down();idle_gap_and_fresh_context();
    admission_after_ref_acquire_drops_down_but_corrects_up();
    accepted_down_releases_only_our_reference_in_game();
    puts("PASS real asynchronous tap/GP bridge: old full suite with GP OFF, queued enter/cycle before poll (with and without cancel), IDLE-gap late submit, fresh-context recovery, DOWN final gate, corrective UP and exactly-one-ref release retaining foreign owner; host only");
    return 0;
}
