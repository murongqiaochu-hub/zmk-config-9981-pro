/* SPDX-License-Identifier: MIT -- trial adapter with actual unchanged runtime.
 * Include prior integration boundaries verbatim, never modify its old oracle.
 */
#include "trial_runtime_fixture.inc"

static void trial_clear_gate(void) { memset(bbp_trial_gate,0,sizeof(bbp_trial_gate)); }
static void test_trial_runtime_precommit_closed(void) {
    runtime_setup(); real_allow_two(); size_t before=sent_count;
    precommit_hook=pump_in_precommit_window;
    assert(write_ccc(0,0,2)==2 && !precommit_hook);
    assert(sent_count==before); /* Fresh producer+real pump never submits DOWN. */
    for(unsigned int i=0;i<10;++i) { pump_once(10); }
    assert(sent_count==before);
    assert(write_ccc(0,1,2)==2); pump_once(0);
    for(size_t i=before;i<sent_count;++i) { assert(is_pad_neutral(&sent[i])); }
    key(24,BBP_GP_A,true); pump_once(0);
    assert(sent_count>before && sent[sent_count-1].data[2]==64);
    runtime_balanced();
}
static void test_trial_sdk_off_race_keeps_finite_release(void) {
    runtime_setup(); real_allow_two(); key(24,BBP_GP_A,true); pump_once(0);
    assert(last_valid && last.target.conn==&peer[0]); size_t before=sent_count;
    choose(1); bbp_gamepad_transport_invalidate(); sdk_check_hook=off_primary;
    pump_once(0);
    assert(!sdk_check_hook && sdk_subscription_rejections==1 && last_valid && peer[0].refs>1);
    for(unsigned int i=0;i<50;++i) { pump_once(10); }
    assert(!last_valid && peer[0].refs==1);
    for(size_t i=before;i<sent_count;++i) { assert(sent[i].conn!=&peer[0]); }
    runtime_balanced(); /* No claim that the old peer received neutral. */
}
int main(void) {
#define TRIAL_RUN(fn) do { trial_clear_gate(); fn(); puts("PASS trial runtime " #fn); } while(0)
    TRIAL_RUN(test_actual_action_to_ble_wire);
    TRIAL_RUN(test_actual_runtime_fifo);
    TRIAL_RUN(test_actual_switch_and_usb);
    TRIAL_RUN(test_actual_ccc_and_disconnect);
    TRIAL_RUN(test_actual_fifo_overflow);
    TRIAL_RUN(test_actual_other_peer_ccc_stales_queue);
    TRIAL_RUN(test_actual_worker_postgate_event);
    TRIAL_RUN(test_trial_runtime_precommit_closed);
    TRIAL_RUN(test_trial_sdk_off_race_keeps_finite_release);
    puts("LIMIT unchanged actual runtime with mocks; SDK may block beyond mock deadlines, no remote ACK");
    return 0;
}
