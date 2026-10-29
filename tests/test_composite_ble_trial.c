/* SPDX-License-Identifier: MIT
 * Actual trial header + pinned HOG fixture + separate lifecycle TU.
 * Old counterfactual fixtures remain byte-for-byte unchanged and still run
 * against the old header in the historical suite. No radio/OS claims here.
 */
#define main historical_candidate_main
#define k_spin_lock historical_spin_lock
#define k_spin_unlock historical_spin_unlock
#define bbp_gamepad_transport_invalidate historical_transport_invalidate
#include "test_composite_ble_candidate.c"
#undef k_spin_lock
#undef k_spin_unlock
#undef bbp_gamepad_transport_invalidate
#undef main

static void (*trial_before_lock)(void), (*trial_during_invalidate)(void);
static unsigned int trial_gate_lock_calls;
void bbp_gamepad_transport_invalidate(void) {
    historical_transport_invalidate();
    call_hook(&trial_during_invalidate);
}
k_spinlock_key_t k_spin_lock(struct k_spinlock *l) {
    if (l == &bbp_trial_gate_lock) {
        ++trial_gate_lock_calls;
        if (trial_before_lock && trial_gate_lock_calls == 2U) {
            void (*fn)(void) = trial_before_lock; trial_before_lock = NULL; fn();
        }
    }
    return historical_spin_lock(l);
}
void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t key) {
    historical_spin_unlock(l,key);
}
uint8_t bt_conn_index(const struct bt_conn *conn) { return conn->dst.a.val[5]; }
extern struct bt_conn_cb bbp_composite_trial_connection;
static void trial_setup(void) {
    setup();
    memset(bbp_trial_gate,0,sizeof(bbp_trial_gate));
    trial_gate_lock_calls=0; trial_before_lock=trial_during_invalidate=NULL;
}
static void test_off_precommit_is_closed(void) {
    allow_two(); size_t before=sent_count;
    precommit_result[0]=precommit_result[1]=-999;
    precommit_hook=try_fresh_precommit;
    assert(write_ccc(0,0,2)==2 && !precommit_hook);
    assert(precommit_result[0]==-EAGAIN && precommit_result[1]==-EAGAIN && sent_count==before);
    assert(!bbp_trial_gate[1].denied); /* Another peer's requested gate unchanged. */
    assert(bt_gatt_is_subscribed(&peer[1],hog_svc.attrs+VALUE,BT_GATT_CCC_NOTIFY));
    struct bbp_gamepad_report_body body=known();
    assert(send_to(&peer[0],&body,epoch)==-EAGAIN);
    assert(write_ccc(0,1,2)==2 && send_to(&peer[0],&body,epoch)==0);
    balanced();
}
static void test_first_on_waits_actual_subscription(void) {
    precommit_result[0]=precommit_result[1]=-999;
    precommit_hook=try_fresh_precommit;
    assert(write_ccc(0,1,2)==2);
    assert(precommit_result[0]==-EAGAIN && precommit_result[1]==-EAGAIN && !sent_count);
    struct bbp_gamepad_report_body body=known();
    assert(send_to(&peer[0],&body,epoch)==0);
    /* Same ON is deliberately permitted: requested and actual permissions agree. */
    precommit_hook=try_fresh_precommit;
    assert(write_ccc(0,1,1)==1 && precommit_result[0]==0 && precommit_result[1]==0);
    balanced();
}
static void test_epoch_rechecked_at_claim(void) {
    allow_two(); struct bbp_gamepad_report_body body=known();
    uint32_t old=epoch; trial_gate_lock_calls=0; trial_before_lock=ctx_invalidate;
    assert(send_to(&peer[0],&body,old)==-ESTALE && !trial_before_lock && !sent_count);
    body=bbp_gamepad_neutral(); trial_gate_lock_calls=0; trial_before_lock=ctx_invalidate;
    assert(send_to(&peer[0],&body,old)==-EAGAIN && !trial_before_lock && !sent_count);
    balanced();
}
static void test_disconnected_and_reused_slot(void) {
    allow_two(); uint32_t old=epoch; struct bbp_gamepad_report_body body=known();
    bbp_composite_trial_connection.disconnected(&peer[0],0x13);
    assert(bbp_trial_gate[0].denied && send_to(&peer[0],&body,epoch)==-EAGAIN);
    uint32_t generation=bbp_trial_gate[0].generation;
    bbp_composite_trial_connection.connected(&peer[0],0);
    assert(!bbp_trial_gate[0].denied && bbp_trial_gate[0].generation>generation);
    /* Bonded restore needs no new CCC write; actual existing value is still ON. */
    assert(send_to(&peer[0],&body,old)==-ESTALE);
    assert(send_to(&peer[0],&body,epoch)==0);
    bbp_composite_trial_connection.connected(&peer[0],1);
    assert(bbp_trial_gate[0].denied && send_to(&peer[0],&body,epoch)==-EAGAIN);
    balanced();
}
static void test_claimed_call_not_retracted(void) {
    allow_two(); struct bbp_gamepad_report_body body=known();
    notify_hook=off_primary; size_t before=sent_count;
    assert(send_to(&peer[0],&body,epoch)==0 && sent_count==before+1);
    assert(send_to(&peer[0],&body,epoch)==-EAGAIN && sent_count==before+1);
    balanced();
}
static void test_reset_cannot_override_newer_off(void) {
    allow_two(); trial_during_invalidate=off_primary;
    bbp_trial_connection_reset(&peer[0],true);
    assert(!trial_during_invalidate && bbp_trial_gate[0].denied);
    struct bbp_gamepad_report_body body=known();
    assert(send_to(&peer[0],&body,epoch)==-EAGAIN && !sent_count);
    balanced();
}
static int reentrant_result;
static void try_reentrant_send(void) {
    assert(bbp_trial_gate[0].busy);
    struct bbp_gamepad_report_body body=known();
    reentrant_result=send_to(&peer[0],&body,epoch);
    off_primary();
    /* OFF must not make the in-flight invocation's slot reusable. */
    assert(bbp_trial_gate[0].busy);
}
static void test_inflight_slot_owned_until_return(void) {
    allow_two(); struct bbp_gamepad_report_body body=known();
    reentrant_result=0; notify_hook=try_reentrant_send;
    assert(send_to(&peer[0],&body,epoch)==0);
    assert(reentrant_result==-EAGAIN && sent_count==1 && !bbp_trial_gate[0].busy);
    assert(bbp_trial_gate[0].denied);
    assert(write_ccc(0,1,2)==2 && send_to(&peer[0],&body,epoch)==0 && sent_count==2);
    balanced();
}
int main(int argc,char **argv) {
    if (argc>1) { return historical_candidate_main(argc,argv); }
#define TRIAL_RUN(fn) do { trial_setup(); fn(); puts("PASS trial " #fn); } while(0)
    TRIAL_RUN(test_structure_and_map);
    TRIAL_RUN(test_reads_output_and_stock);
    TRIAL_RUN(test_ccc_cycles_and_early_return);
    TRIAL_RUN(test_encoder_vectors);
    TRIAL_RUN(test_send_peer_and_races);
    TRIAL_RUN(test_old_peer_neutral);
    TRIAL_RUN(test_snapshot_stock_chain);
    TRIAL_RUN(test_off_precommit_is_closed);
    TRIAL_RUN(test_first_on_waits_actual_subscription);
    TRIAL_RUN(test_epoch_rechecked_at_claim);
    TRIAL_RUN(test_disconnected_and_reused_slot);
    TRIAL_RUN(test_claimed_call_not_retracted);
    TRIAL_RUN(test_reset_cannot_override_newer_off);
    TRIAL_RUN(test_inflight_slot_owned_until_return);
    puts("LIMIT fixed HOG/mock CCC; no actual encryption, radio, OS, firmware or host ACK proof");
    return 0;
}
