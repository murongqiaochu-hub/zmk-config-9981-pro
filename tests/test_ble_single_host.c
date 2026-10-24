/* SPDX-License-Identifier: MIT
 * Actual pinned ZMK BLE TU + actual policy service, compiled in separate TUs.
 * All stimuli use public APIs or captured real registration callbacks. No SDK
 * private variable/static-function access and no fixture rewriting.
 */
#include "ble_single_host_mocks/test_env.h"
#include <zmk/ble.h>
#if TEST_POLICY_PRESENT
#include "ble_single_host.h"
#endif

#if IS_ENABLED(CONFIG_BBP9981_BLE_SINGLE_HOST) && IS_ENABLED(CONFIG_ZMK_BLE) && \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)) && \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT_BLE) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
#define TEST_POLICY_ACTIVE 1
#else
#define TEST_POLICY_ACTIVE 0
#endif

#if IS_ENABLED(CONFIG_ZMK_BLE)
static bt_addr_le_t peers[3];
#define EMPTY_PROFILE (ZMK_BLE_PROFILE_COUNT-1)

static void bootstrap(bool duplicate) {
    peers[0]=test_address(10,BT_ADDR_LE_PUBLIC);
    peers[1]=test_address(20,BT_ADDR_LE_RANDOM);
    peers[2]=duplicate?peers[0]:test_address(30,BT_ADDR_LE_PUBLIC);
    test_run_inits();
#if IS_ENABLED(CONFIG_SETTINGS)
    for(uint8_t i=0;i<3;i++)test_load_profile(i,&peers[i]);
    test_load_active(0);test_commit_settings();
#else
    assert(!duplicate);
    for(uint8_t i=0;i<3;i++){
        assert(zmk_ble_prof_select(i)==0);
        struct bt_conn *c=test_seed_conn(7,&peers[i],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
        assert(test_pair_accept(c)==BT_SECURITY_ERR_SUCCESS);
        test_pair_complete(c,true);
        test_emit_disconnected(c,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
    assert(zmk_ble_prof_select(0)==0);
#endif
    test_advance(2000);
    assert(zmk_ble_active_profile_index()==0);
    for(uint8_t i=0;i<3;i++)assert(!bt_addr_le_cmp(zmk_ble_profile_address(i),&peers[i]));
    assert(zmk_ble_profile_is_open(EMPTY_PROFILE));
    test_assert_balanced();test_reset_metrics();
}
static struct bt_conn *connect_peer(unsigned index,unsigned peer) {
    struct bt_conn *c=test_seed_conn(index,&peers[peer],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(c,0);return c;
}
static void check_profiles(void) {
    for(uint8_t i=0;i<3;i++)assert(!bt_addr_le_cmp(zmk_ble_profile_address(i),&peers[i]));
    assert(!test_metrics.unpairs&&!test_metrics.settings_deletes);
}
static void stock_case(void) {
    bootstrap(false);
    unsigned callbacks=test_conn_callback_count,listeners=test_listener_count;
    struct bt_conn *old=connect_peer(0,0);test_run_ready();
    assert(zmk_ble_prof_select(1)==0);test_advance(10000);
    assert(old->disconnect_calls==0);
    assert(zmk_ble_active_profile_index()==1);
    assert(zmk_ble_prof_select(ZMK_BLE_PROFILE_COUNT)==-ERANGE);
    unsigned saves=test_metrics.settings_saves,events=test_metrics.events;
    assert(zmk_ble_prof_select(1)==0);test_advance(10000);
    assert(test_metrics.settings_saves==saves&&test_metrics.events==events);
    assert(test_conn_callback_count==callbacks&&test_listener_count==listeners);
    assert(!test_metrics.disconnects);
    assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);test_run_ready();
    assert(test_pair_accept(old)==BT_SECURITY_ERR_SUCCESS);
    check_profiles();test_assert_balanced();
    printf("TRACE stock settings=%u callbacks=%u listeners=%u saves=%u events=%u adv=%u/%u work=%u/%u\n",
           CONFIG_SETTINGS,callbacks,listeners,test_metrics.settings_saves,test_metrics.events,
           test_metrics.adv_starts,test_metrics.adv_stops,test_metrics.work_schedules,test_metrics.work_runs);
}
static void selection_case(bool require_policy) {
    bootstrap(false);
    struct bt_conn *a=connect_peer(0,0),*c=connect_peer(1,2),*b=connect_peer(2,1);
    assert(zmk_ble_prof_select(1)==0);test_run_ready();
    if(require_policy){assert(a->disconnect_calls>0);assert(c->disconnect_calls>0);}
    assert(b->disconnect_calls==0);
    assert(zmk_ble_active_profile_index()==1);
    check_profiles();
    unsigned count=test_metrics.disconnects;
    assert(zmk_ble_prof_select(1)==0);test_run_ready();assert(test_metrics.disconnects==count);
    assert(zmk_ble_prof_select(ZMK_BLE_PROFILE_COUNT)==-ERANGE);
    assert(zmk_ble_active_profile_index()==1);
    test_emit_disconnected(a,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    test_emit_disconnected(c,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    test_advance(10000);assert(b->disconnect_calls==0);
    assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);
    assert(zmk_ble_prof_next()==0&&zmk_ble_active_profile_index()==0);
    assert(zmk_ble_prof_prev()==0&&zmk_ble_active_profile_index()==EMPTY_PROFILE);
    test_advance(10000);check_profiles();test_assert_balanced();
}
static void fast_case(void) {
    bootstrap(false);
    struct bt_conn *a=connect_peer(0,0),*b=connect_peer(1,1),*c=connect_peer(2,2);
    assert(zmk_ble_prof_select(1)==0);assert(zmk_ble_prof_select(2)==0);assert(zmk_ble_prof_select(0)==0);
    test_run_ready();assert(!a->disconnect_calls);assert(b->disconnect_calls&&c->disconnect_calls);
    test_emit_disconnected(b,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    test_emit_disconnected(c,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    assert(zmk_ble_prof_select(1)==0);assert(zmk_ble_prof_select(0)==0);
    test_emit_security(a,BT_SECURITY_L2,BT_SECURITY_ERR_SUCCESS);
    test_advance(10000);assert(!a->disconnect_calls);check_profiles();test_assert_balanced();
}
static void already_inactive_case(void) {
    bootstrap(false);
    struct bt_conn *old=test_seed_conn(0,&peers[2],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    struct bt_conn *current=test_seed_conn(1,&peers[1],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    /* Deliberately no connected callback: existing connections must be found. */
    assert(zmk_ble_prof_select(1)==0);test_run_ready();
    assert(old->disconnect_calls&&!current->disconnect_calls);
    test_emit_disconnected(old,BT_HCI_ERR_REMOTE_USER_TERM_CONN);test_advance(10000);
    check_profiles();test_assert_balanced();
}
static void pairing_case(void) {
    bootstrap(false);assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);test_run_ready();
    struct bt_conn *old=connect_peer(0,1);
    assert(test_pair_accept(old)!=BT_SECURITY_ERR_SUCCESS);
    /* A completion callback after rejected admission must not overwrite/unpair. */
    unsigned saves=test_metrics.settings_saves;
    test_pair_complete(old,true);test_run_ready();
    assert(zmk_ble_active_profile_is_open());assert(test_metrics.settings_saves==saves);
    assert(old->disconnect_calls);check_profiles();
    test_emit_disconnected(old,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    bt_addr_le_t unknown=test_address(91,BT_ADDR_LE_PUBLIC);
    struct bt_conn *fresh=test_seed_conn(1,&unknown,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(fresh,0);test_run_ready();
    assert(!fresh->disconnect_calls&&test_pair_accept(fresh)==BT_SECURITY_ERR_SUCCESS);
    test_pair_complete(fresh,true);test_advance(10000);
    assert(!bt_addr_le_cmp(zmk_ble_active_profile_addr(),&unknown));
    assert(!fresh->disconnect_calls);check_profiles();test_assert_balanced();
}
static void candidate_release_case(void) {
    bootstrap(false);assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);test_run_ready();
    bt_addr_le_t rpa=test_address(101,BT_ADDR_LE_RANDOM);rpa.a.val[5]=0x40;
    bt_addr_le_t fresh_addr=test_address(102,BT_ADDR_LE_PUBLIC);
    struct bt_conn *first=test_seed_conn(0,&rpa,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(first,0);test_run_ready();assert(!first->disconnect_calls);
    first->disconnect_result=-EIO;
    test_emit_identity(first,&peers[1]);test_run_ready();assert(first->disconnect_calls);
    /* Identity evidence invalidated the provisional owner; no disconnect
     * completion is required before another legitimate unknown can pair. */
    struct bt_conn *fresh=test_seed_conn(1,&fresh_addr,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(fresh,0);test_run_ready();assert(!fresh->disconnect_calls);
    assert(test_pair_accept(fresh)==BT_SECURITY_ERR_SUCCESS);
    test_pair_complete(fresh,true);test_advance(10000);
    assert(!bt_addr_le_cmp(zmk_ble_active_profile_addr(),&fresh_addr));
    check_profiles();test_assert_balanced();
}
static void candidate_pairing_case(void) {
    bootstrap(false);assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);test_run_ready();
    bt_addr_le_t first_addr=test_address(103,BT_ADDR_LE_PUBLIC),second_addr=test_address(104,BT_ADDR_LE_PUBLIC);
    struct bt_conn *first=test_seed_conn(0,&first_addr,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    struct bt_conn *second=test_seed_conn(1,&second_addr,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(first,0);test_emit_connected(second,0);
    assert(test_pair_accept(first)==BT_SECURITY_ERR_SUCCESS);
    assert(test_pair_accept(second)!=BT_SECURITY_ERR_SUCCESS);
    test_pair_complete(second,true);assert(zmk_ble_active_profile_is_open());
    test_pair_complete(first,true);test_advance(10000);
    assert(!bt_addr_le_cmp(zmk_ble_active_profile_addr(),&first_addr));
    assert(!first->disconnect_calls&&second->disconnect_calls);
    check_profiles();test_assert_balanced();
}
static void pairing_cancel_case(bool failed) {
    bootstrap(false);assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);test_run_ready();
    bt_addr_le_t abandoned=test_address(failed?106:105,BT_ADDR_LE_PUBLIC);
    bt_addr_le_t replacement=test_address(failed?108:107,BT_ADDR_LE_PUBLIC);
    struct bt_conn *first=test_seed_conn(0,&abandoned,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    first->disconnect_result=-EIO;
    test_emit_connected(first,0);assert(test_pair_accept(first)==BT_SECURITY_ERR_SUCCESS);
    if(failed)test_pair_failed(first,BT_SECURITY_ERR_AUTH_FAIL);else test_pair_cancel(first);
    test_run_ready();assert(first->disconnect_calls&&zmk_ble_active_profile_is_open());
    unsigned saves=test_metrics.settings_saves;
    test_pair_complete(first,true);test_run_ready();
    assert(zmk_ble_active_profile_is_open()&&test_metrics.settings_saves==saves);
    /* The abandoned owner is STILL CONNECTED: no successful disconnect and no
     * callback is supplied. Cancellation/failure must release admission now. */
    struct bt_conn *fresh=test_seed_conn(1,&replacement,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(fresh,0);assert(test_pair_accept(fresh)==BT_SECURITY_ERR_SUCCESS);
    test_pair_complete(fresh,true);test_advance(10000);
    assert(!bt_addr_le_cmp(zmk_ble_active_profile_addr(),&replacement));
    assert(!fresh->disconnect_calls);check_profiles();test_assert_balanced();
}
static void pair_race_case(void) {
    bootstrap(false);assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);test_run_ready();
    bt_addr_le_t unknown=test_address(92,BT_ADDR_LE_PUBLIC);
    struct bt_conn *fresh=test_seed_conn(0,&unknown,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(fresh,0);assert(test_pair_accept(fresh)==BT_SECURITY_ERR_SUCCESS);
    assert(zmk_ble_prof_select(1)==0);
    test_pair_complete(fresh,true);test_advance(10000);
    assert(zmk_ble_active_profile_index()==1);assert(zmk_ble_profile_is_open(EMPTY_PROFILE));
    check_profiles();test_assert_balanced();
}
static void pairing_final_gate_case(bool blocking_backend) {
    bootstrap(false);assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);test_run_ready();
    bt_addr_le_t unknown=test_address(blocking_backend?94:93,BT_ADDR_LE_PUBLIC);
    struct bt_conn *fresh=test_seed_conn(0,&unknown,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(fresh,0);assert(test_pair_accept(fresh)==BT_SECURITY_ERR_SUCCESS);
    if(blocking_backend)test_select_on_settings_block=1;
    else test_select_on_sched_lock=1;
    test_pair_complete(fresh,true);test_advance(10000);
    assert(zmk_ble_active_profile_index()==1);
    assert(!bt_addr_le_cmp(zmk_ble_profile_address(EMPTY_PROFILE),&unknown));
    assert(test_metrics.sched_locks>0&&test_metrics.sched_locks==test_metrics.sched_unlocks);
    assert(test_metrics.sched_max_depth>=2);
    assert(test_sched_depth==0);check_profiles();test_assert_balanced();
}
static void roles_case(void) {
    bootstrap(false);
    struct bt_conn *central=test_seed_conn(0,&peers[1],BT_CONN_ROLE_CENTRAL,BT_ID_DEFAULT);
    struct bt_conn *identity=test_seed_conn(1,&peers[1],BT_CONN_ROLE_PERIPHERAL,1);
    struct bt_conn *br=test_seed_conn(2,&peers[1],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);br->type=BT_CONN_TYPE_BR;
    struct bt_conn *active=connect_peer(3,0);
    test_emit_connected(central,0);test_emit_connected(identity,0);test_emit_connected(br,0);
    test_run_ready();test_advance(10000);
    assert(!central->disconnect_calls&&!identity->disconnect_calls&&!br->disconnect_calls&&!active->disconnect_calls);
    assert(test_pair_accept(central)==BT_SECURITY_ERR_SUCCESS);
    test_pair_cancel(central);test_pair_failed(central,BT_SECURITY_ERR_AUTH_FAIL);test_run_ready();
    assert(!central->disconnect_calls&&!identity->disconnect_calls&&!br->disconnect_calls&&!active->disconnect_calls);
    check_profiles();test_assert_balanced();
}
static void rpa_case(const char *kind) {
    bootstrap(false);
    bool empty=!strcmp(kind,"rpa-empty"),expire=!strcmp(kind,"rpa-expire"),inactive=!strcmp(kind,"rpa-inactive");
    if(empty){assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);test_run_ready();}
    bt_addr_le_t rpa=test_address(77,BT_ADDR_LE_RANDOM);rpa.a.val[5]=0x40;
    struct bt_conn *conn=test_seed_conn(0,&rpa,BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    test_emit_connected(conn,0);test_run_ready();assert(!conn->disconnect_calls);
    test_advance(999);assert(!conn->disconnect_calls);
    if(!expire&&!empty){
        test_emit_identity(conn,&peers[inactive?1:0]);test_run_ready();
        if(inactive)assert(conn->disconnect_calls);else assert(!conn->disconnect_calls);
    }
    test_advance(10000);
    if(expire||inactive)assert(conn->disconnect_calls);else assert(!conn->disconnect_calls);
    if(empty){
        /* The unknown may pair indefinitely, but becomes rejectable when the
         * stack resolves it to an already mapped inactive host identity. */
        test_emit_identity(conn,&peers[1]);test_run_ready();assert(conn->disconnect_calls);
    }
    check_profiles();test_assert_balanced();
}
static void retry_case(int error) {
    bootstrap(false);
    struct bt_conn *old=test_seed_conn(0,&peers[1],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    old->disconnect_result=error;test_emit_connected(old,0);test_advance(10000);
    assert(old->disconnect_calls==3);
    unsigned calls=old->disconnect_calls,work=test_metrics.work_runs;
    test_advance(60000);assert(old->disconnect_calls==calls&&test_metrics.work_runs==work);
    assert(zmk_ble_prof_select(1)==0);test_run_ready();assert(old->disconnect_calls==calls);
    assert(zmk_ble_prof_select(0)==0);test_advance(10000);
    assert(old->disconnect_calls>calls&&old->disconnect_calls<=calls+3);
    check_profiles();test_assert_balanced();
}
static void work_failure_case(void) {
    bootstrap(false);test_work_result=-EIO;
    struct bt_conn *old=connect_peer(0,1);test_advance(10000);assert(!old->disconnect_calls);
    test_work_result=0;
    assert(zmk_ble_prof_select(1)==0);assert(zmk_ble_prof_select(0)==0);test_run_ready();
    assert(old->disconnect_calls);check_profiles();test_assert_balanced();
}
static void reuse_case(void) {
    bootstrap(false);
    struct bt_conn *old=test_seed_conn(0,&peers[1],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    old->disconnect_result=-EIO;test_emit_connected(old,0);test_run_ready();assert(old->disconnect_calls==1);
    unsigned serial=old->serial;test_emit_disconnected(old,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    struct bt_conn *fresh=connect_peer(0,0);assert(fresh->serial!=serial);
    test_advance(10000);assert(!fresh->disconnect_calls);check_profiles();test_assert_balanced();
}
#if TEST_POLICY_PRESENT
static void same_index_peer_case(void) {
    bootstrap(false);
    struct bt_conn *old=test_seed_conn(0,&peers[0],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    struct bt_conn *latest=test_seed_conn(1,&peers[1],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    /* Queue first, arm second: reconcile's own lock cannot consume the hook.
     * Only the service TU's public getter call is bridged; the first worker
     * target getter must itself hold the scheduler lock. The registered h_set
     * writer is deferred until the outer unlock, never injected inside it. */
    assert(!test_snapshot_getter_armed&&test_snapshot_getter_hits==0);
    bbp_ble_single_host_reconcile();
    test_update_profile_addr=peers[1];test_snapshot_getter_armed=true;
    test_run_ready();
    assert(!test_snapshot_getter_armed&&test_snapshot_getter_hits==1);
    assert(!bt_addr_le_cmp(zmk_ble_profile_address(0),&peers[1]));
    assert(!latest->disconnect_calls);
    bbp_ble_single_host_reconcile();test_run_ready();
    assert(old->disconnect_calls&&!latest->disconnect_calls);
    peers[0]=peers[1];test_advance(10000);check_profiles();test_assert_balanced();
}
#endif
static void duplicate_case(void) {
    bootstrap(true);
    assert(zmk_ble_prof_select(2)==0);
    struct bt_conn *same=connect_peer(0,0);test_advance(10000);
    assert(!same->disconnect_calls);check_profiles();test_assert_balanced();
}
static void failure_case(void) {
    bootstrap(false);
    struct bt_conn *bad=test_seed_conn(0,&peers[1],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    bad->info_error=-EIO;test_emit_connected(bad,0);test_advance(1000);assert(!bad->disconnect_calls);
    bad->info_error=0;test_emit_connected(bad,0x3e);test_run_ready();assert(!bad->disconnect_calls);
    struct bt_conn *gone=test_seed_conn(1,&peers[1],BT_CONN_ROLE_PERIPHERAL,BT_ID_DEFAULT);
    gone->disconnect_result=-ENOTCONN;test_emit_connected(gone,0);test_advance(10000);
    assert(gone->disconnect_calls>0&&gone->disconnect_calls<=3);
    test_adv_start_result=-EIO;test_adv_stop_result=-EIO;
    assert(zmk_ble_prof_select(EMPTY_PROFILE)==0);
    unsigned starts=test_metrics.adv_starts,stops=test_metrics.adv_stops;
    test_run_ready();assert(test_metrics.adv_starts==starts&&test_metrics.adv_stops==stops);
    check_profiles();test_assert_balanced();
}
#endif

int main(int argc,char **argv) {
    assert(argc==2);const char *mode=argv[1];
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if(!strcmp(mode,"stock"))stock_case();
    else if(!strcmp(mode,"expect-policy"))selection_case(true);
#if TEST_POLICY_ACTIVE
    else if(!strcmp(mode,"selection"))selection_case(true);
    else if(!strcmp(mode,"fast"))fast_case();
    else if(!strcmp(mode,"inactive"))already_inactive_case();
    else if(!strcmp(mode,"pairing"))pairing_case();
    else if(!strcmp(mode,"candidate-release"))candidate_release_case();
    else if(!strcmp(mode,"candidate-pairing"))candidate_pairing_case();
    else if(!strcmp(mode,"pair-cancel"))pairing_cancel_case(false);
    else if(!strcmp(mode,"pair-failed"))pairing_cancel_case(true);
    else if(!strcmp(mode,"pair-race"))pair_race_case();
    else if(!strcmp(mode,"pair-final-gate"))pairing_final_gate_case(false);
    else if(!strcmp(mode,"pair-backend-block"))pairing_final_gate_case(true);
    else if(!strcmp(mode,"roles"))roles_case();
    else if(!strncmp(mode,"rpa-",4))rpa_case(mode);
    else if(!strcmp(mode,"retry"))retry_case(-EIO);
    else if(!strcmp(mode,"retry-already"))retry_case(-EALREADY);
    else if(!strcmp(mode,"work-failure"))work_failure_case();
    else if(!strcmp(mode,"reuse"))reuse_case();
    else if(!strcmp(mode,"duplicate"))duplicate_case();
    else if(!strcmp(mode,"same-index-peer"))same_index_peer_case();
    else if(!strcmp(mode,"failures"))failure_case();
#endif
    else assert(false&&"unknown or inactive test scenario");
#else
    (void)mode;test_run_inits();test_advance(10000);
    assert(!test_conn_callback_count&&!test_listener_count&&!test_pending_work());
    assert(!test_metrics.disconnects&&!test_metrics.settings_saves&&!test_metrics.unpairs);
    test_assert_balanced();
#endif
    printf("PASS actual BLE/policy host scenario=%s policy=%u settings=%u BLE=%u USB=%u split=%u/%u\n",
           mode,TEST_POLICY_ACTIVE,CONFIG_SETTINGS,CONFIG_ZMK_BLE,CONFIG_ZMK_USB,
           CONFIG_ZMK_SPLIT_BLE,CONFIG_ZMK_SPLIT_ROLE_CENTRAL);
    return 0;
}
