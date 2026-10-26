/* SPDX-License-Identifier: MIT -- actual adapter, mocked SDK/runtime boundaries. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_BT_MAX_CONN 2
#include "../experiments/gamepad_protocol/pure_transport.h"

static struct bt_conn peers[3], *active;
static struct zmk_endpoint_instance endpoint;
static int profile, notify_error;
static uint32_t epoch;
static unsigned int lock_depth, notifications, ref_calls, unref_calls, invalidations;
static struct bbp_gamepad_report_body current;
static uint8_t delivered[10];
static struct bt_conn *delivered_to;
static void (*get_hook)(void), (*subscribe_hook)(void), (*active_hook)(void);
static void (*ref_hook)(void), (*info_hook)(void), (*notify_hook)(void);
static unsigned int info_calls, info_hook_at;
static void hook(void (**p)(void)) { void (*fn)(void)=*p; *p=NULL; if(fn) { fn(); } }

k_spinlock_key_t k_spin_lock(struct k_spinlock *l) {
    assert(!lock_depth && !l->held); l->held=true;lock_depth++;return 0;
}
void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t key) {
    (void)key;assert(lock_depth==1 && l->held);l->held=false;lock_depth--;
}
struct bt_conn *bt_conn_ref(struct bt_conn *c) {
    assert(!lock_depth && c && c->refs>0);c->refs++;ref_calls++;hook(&ref_hook);return c;
}
void bt_conn_unref(struct bt_conn *c) {
    assert(!lock_depth && c && c->refs>1);c->refs--;unref_calls++;
}
int bt_conn_get_info(const struct bt_conn *c,struct bt_conn_info *out) {
    assert(!lock_depth && c);out->state=c->state;
    if(++info_calls==info_hook_at) { hook(&info_hook); }
    return c->info_error;
}
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *c) { assert(!lock_depth && c);return &c->dst; }
int zmk_ble_active_profile_index(void) { assert(!lock_depth);return profile; }
int zmk_ble_profile_index(const bt_addr_le_t *addr) { assert(!lock_depth);return addr->a.val[0]; }
struct bt_conn *zmk_ble_active_profile_conn(void) {
    assert(!lock_depth);struct bt_conn *c=active?bt_conn_ref(active):NULL;hook(&active_hook);return c;
}
struct zmk_endpoint_instance zmk_endpoints_selected(void) { assert(!lock_depth);return endpoint; }
uint32_t bbp_gamepad_context_epoch(void) { return epoch; }
bool bbp_gamepad_context_valid(uint32_t value) { return value==epoch; }
void bbp_gamepad_transport_invalidate(void) { assert(!lock_depth);epoch++;invalidations++;current=bbp_gamepad_neutral(); }
void bbp_gamepad_get_report(uint8_t transport,struct bbp_gamepad_report_body *out) {
    assert(!lock_depth && transport==ZMK_TRANSPORT_BLE);*out=current;hook(&get_hook);
}
bool bt_gatt_is_subscribed(struct bt_conn *c,const struct bt_gatt_attr *a,uint16_t type) {
    assert(!lock_depth && c && a==bbp_exp_hids.attrs+8 && type==BT_GATT_CCC_NOTIFY);
    bool answer=c->subscribed;hook(&subscribe_hook);return answer;
}
int bt_gatt_notify_cb(struct bt_conn *c,struct bt_gatt_notify_params *p) {
    assert(!lock_depth && c && c->refs>=2 && c->state==BT_CONN_STATE_CONNECTED);
    assert(p->attr==bbp_exp_hids.attrs+8 && p->len==10 && !p->uuid && !p->func && !p->user_data);
    hook(&notify_hook); /* Post-gate cancellation: one in-flight submission allowed. */
    memcpy(delivered,p->data,10);delivered_to=c;notifications++;
    return notify_error;
}
ssize_t bt_gatt_attr_read(struct bt_conn *c,const struct bt_gatt_attr *a,void *buf,
                          uint16_t len,uint16_t off,const void *data,uint16_t size) {
    (void)c;(void)a;assert(!lock_depth);
    if(off>size) { return -BT_ATT_ERR_INVALID_OFFSET; }
    size_t n=size-off;if(n>len) { n=len; } if(n) { memcpy(buf,(const uint8_t *)data+off,n); }return n;
}
ssize_t bt_gatt_attr_read_service(struct bt_conn *c,const struct bt_gatt_attr *a,void *b,uint16_t n,uint16_t o) {
    return bt_gatt_attr_read(c,a,b,n,o,&((struct bt_uuid *)a->user_data)->val,2);
}
ssize_t bt_gatt_attr_read_chrc(struct bt_conn *c,const struct bt_gatt_attr *a,void *b,uint16_t n,uint16_t o) {
    return bt_gatt_attr_read(c,a,b,n,o,&((struct bt_gatt_chrc *)a->user_data)->properties,1);
}
ssize_t bt_gatt_attr_read_ccc(struct bt_conn *c,const struct bt_gatt_attr *a,void *b,uint16_t n,uint16_t o) {
    (void)c;(void)a;(void)b;(void)n;(void)o;return -ENOTSUP;
}
ssize_t bt_gatt_attr_write_ccc(struct bt_conn *c,const struct bt_gatt_attr *a,const void *b,uint16_t n,uint16_t o,uint8_t flags) {
    (void)c;(void)a;(void)b;(void)n;(void)o;(void)flags;return -ENOTSUP;
}
static void select_peer(unsigned int i) {
    active=&peers[i];profile=(int)i;endpoint=(struct zmk_endpoint_instance){.transport=ZMK_TRANSPORT_BLE,.ble={.profile_index=i}};
}
static void init(void) {
    for(unsigned int i=0;i<3;++i) {
        peers[i]=(struct bt_conn){.state=BT_CONN_STATE_CONNECTED,.refs=1,.subscribed=true};
        peers[i].dst.a.val[0]=(uint8_t)i;
    }
    select_peer(0);
    assert(bbp_exp_transport_connected(&peers[0],0)==0);
    assert(bbp_exp_transport_connected(&peers[1],0)==0);
}
static void current_button(void) { current=bbp_gamepad_neutral();current.buttons_lo=1;current.x=127;current.lt=255; }
static void suspend_primary(void) { bbp_exp_set_suspended(&peers[0],true); }
static void flip_route(void) { endpoint.transport=ZMK_TRANSPORT_USB; }
static void cancel_context(void) { bbp_gamepad_transport_invalidate(); }
static void replace_active(void) { active=&peers[2]; }
static void reconnect_primary(void) {
    bbp_exp_transport_disconnected(&peers[0]);
    assert(bbp_exp_transport_connected(&peers[0],0)==0);
}
static void disconnect_during_registration(void) {
    peers[2].state=BT_CONN_STATE_DISCONNECTED;bbp_exp_transport_disconnected(&peers[2]);
}
/* Pinned SDK ordering model: managed callback before commit, changed only if
 * max of all peers changes. NOT a complete ATT or persistence implementation.
 */
static int ccc_transition(unsigned int peer,uint16_t value) {
    const struct bt_gatt_attr *a=bbp_exp_hids.attrs+9;
    struct _bt_gatt_ccc *ccc=a->user_data;
    int ret=ccc->cfg_write?(int)ccc->cfg_write(&peers[peer],a,value):2;
    if(ret<0) { return ret; }
    assert(ret==2);ccc->cfg[peer].value=value;peers[peer].subscribed=value==1;
    uint16_t aggregate=0;
    for(size_t i=0;i<BT_GATT_CCC_MAX;++i) {
        if(ccc->cfg[i].value>aggregate) { aggregate=ccc->cfg[i].value; }
    }
    if(aggregate!=ccc->value) { ccc->value=aggregate;ccc->cfg_changed(a,aggregate); }
    return ret;
}
static void test_ccc_cycles(void) {
    select_peer(0);current_button();struct bbp_gamepad_report_body body=current;
    struct _bt_gatt_ccc *ccc=bbp_exp_hids.attrs[9].user_data;
    ccc->cfg[0].value=ccc->cfg[1].value=ccc->value=1;
    peers[0].subscribed=peers[1].subscribed=true;
    uint32_t old_epoch=epoch,ticket;assert(!bbp_exp_peer_ticket(&peers[0],&ticket));
    uint32_t aggregate_events=bbp_exp_diag.counts[BBP_EXP_CCC_AGGREGATE];
    assert(ccc_transition(0,0)==2);assert(ccc_transition(0,1)==2);
    assert(ccc->value==1 && aggregate_events==bbp_exp_diag.counts[BBP_EXP_CCC_AGGREGATE]);
    assert(epoch!=old_epoch); /* The aggregate-only counterfactual fails HERE. */
    assert(!bbp_exp_ticket_valid(&peers[0],ticket));
    assert(bbp_exp_transport_send(&peers[0],&body,old_epoch)==-ESTALE);
    old_epoch=epoch;assert(ccc_transition(0,1)==2 && epoch!=old_epoch);
    old_epoch=epoch;assert(ccc_transition(0,2)==-0x13 && epoch==old_epoch && ccc->value==1);
    assert(bbp_exp_ccc_write(&peers[2],bbp_exp_hids.attrs+9,1)==-BT_ATT_ERR_UNLIKELY);
    assert(bbp_exp_ccc_write(NULL,bbp_exp_hids.attrs+9,1)==-BT_ATT_ERR_UNLIKELY);
}
static void assert_neutral(struct bbp_gamepad_report_body *out) { assert(bbp_gamepad_is_neutral(out)); }
static void test_capture(void) {
    struct bbp_gamepad_report_body out;
    current_button();assert(bbp_exp_capture_report(&peers[0],&out) && out.buttons_lo==1);
    assert(!bbp_exp_capture_report(&peers[1],&out));assert_neutral(&out);
    assert(!bbp_exp_capture_report(NULL,&out));assert_neutral(&out);
    assert(!bbp_exp_capture_report(&peers[0],NULL));
    get_hook=cancel_context;current_button();assert(!bbp_exp_capture_report(&peers[0],&out));assert_neutral(&out);
    get_hook=suspend_primary;current_button();assert(!bbp_exp_capture_report(&peers[0],&out));assert_neutral(&out);
    bbp_exp_set_suspended(&peers[0],false);
    active_hook=flip_route;current_button();assert(!bbp_exp_capture_report(&peers[0],&out));assert_neutral(&out);select_peer(0);
    get_hook=replace_active;current_button();assert(!bbp_exp_capture_report(&peers[0],&out));assert_neutral(&out);select_peer(0);
}
static void test_send(void) {
    current_button();struct bbp_gamepad_report_body body=current;
    assert(!bbp_exp_transport_send(&peers[0],&body,epoch));
    const uint8_t golden[10]={15,4,64,255,128,128,128,255,0,0};
    assert(!memcmp(delivered,golden,10) && delivered_to==&peers[0]);
    unsigned int before=notifications;
    assert(bbp_exp_transport_send(NULL,&body,epoch)==-ENOTCONN);
    assert(bbp_exp_transport_send(&peers[0],NULL,epoch)==-EINVAL);
    assert(bbp_exp_transport_send(&peers[2],&body,epoch)==-ENOTCONN);
    peers[0].subscribed=false;assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-EAGAIN);peers[0].subscribed=true;
    assert(bbp_exp_transport_send(&peers[0],&body,epoch-1)==-ESTALE);
    assert(bbp_exp_transport_send(&peers[1],&body,epoch)==-ESTALE);
    body.hat=9;assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-EINVAL);body.hat=8;
    subscribe_hook=cancel_context;assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-ESTALE);
    active_hook=flip_route;assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-ESTALE);select_peer(0);
    subscribe_hook=suspend_primary;assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-ESTALE);
    assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-EAGAIN);bbp_exp_set_suspended(&peers[0],false);
    info_hook_at=info_calls+4;info_hook=cancel_context;
    assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-ESTALE && !info_hook);
    info_hook_at=info_calls+4;info_hook=suspend_primary;
    assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-ESTALE && !info_hook);
    bbp_exp_set_suspended(&peers[0],false);
    info_hook_at=info_calls+4;info_hook=flip_route;
    assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-ESTALE && !info_hook);select_peer(0);
    assert(notifications==before);
    uint32_t pre_submit_epoch=epoch;
    notify_hook=cancel_context;
    assert(!bbp_exp_transport_send(&peers[0],&body,pre_submit_epoch));
    assert(notifications==before+1 && epoch!=pre_submit_epoch);
    assert(bbp_exp_transport_send(&peers[0],&body,pre_submit_epoch)==-ESTALE);
    /* Neutral releases stay with OLD captured peer, never broadcast to new peer. */
    struct bbp_gamepad_report_body neutral=bbp_gamepad_neutral();
    select_peer(1);assert(!bbp_exp_transport_send(&peers[0],&neutral,epoch-1));assert(delivered_to==&peers[0]);
    bbp_exp_set_suspended(&peers[0],true);assert(bbp_exp_transport_send(&peers[0],&neutral,epoch)==-EAGAIN);
    bbp_exp_set_suspended(&peers[0],false);select_peer(0);
    subscribe_hook=reconnect_primary;before=notifications;
    assert(bbp_exp_transport_send(&peers[0],&neutral,epoch)==-ESTALE);assert(notifications==before);
    notify_error=-ENOMEM;assert(bbp_exp_transport_send(&peers[0],&body,epoch)==-ENOMEM);notify_error=0;
}
static void test_lifecycle(void) {
    int refs=peers[0].refs;
    assert(!bbp_exp_transport_connected(&peers[0],0) && peers[0].refs==refs);
    assert(bbp_exp_transport_connected(&peers[2],0)==-ENOMEM && peers[2].refs==1);
    assert(bbp_exp_transport_connected(&peers[2],1)==-ENOTCONN);
    bbp_exp_transport_disconnected(&peers[1]);
    ref_hook=disconnect_during_registration;
    assert(bbp_exp_transport_connected(&peers[2],0)==-ENOTCONN && peers[2].refs==1);
    peers[2].state=BT_CONN_STATE_CONNECTED;
    assert(!bbp_exp_transport_connected(&peers[1],0));
    uint32_t ticket;assert(!bbp_exp_peer_ticket(&peers[0],&ticket));
    bbp_exp_set_suspended(&peers[1],true);uint32_t other;
    assert(!bbp_exp_peer_ticket(&peers[0],&other) && other==ticket);
    assert(bbp_exp_peer_ticket(&peers[1],&other)==-EAGAIN);
    /* Move a reconnected object to a different free slot; old ticket must fail. */
    bbp_exp_transport_disconnected(&peers[0]);
    assert(!bbp_exp_transport_connected(&peers[2],0));
    bbp_exp_transport_disconnected(&peers[1]);
    assert(!bbp_exp_transport_connected(&peers[0],0));
    assert(!bbp_exp_ticket_valid(&peers[0],ticket));
    bbp_exp_transport_disconnected(&peers[2]);
    assert(!bbp_exp_transport_connected(&peers[1],0));
    assert(!bbp_exp_peer_ticket(&peers[1],&other));
    for(unsigned int i=0;i<10000;++i) {
        bbp_exp_set_suspended(&peers[0],true);bbp_exp_set_suspended(&peers[0],false);
        bbp_exp_transport_disconnected(&peers[0]);assert(!bbp_exp_transport_connected(&peers[0],0));
    }
    struct bbp_exp_diagnostics d;
    bbp_exp_observe(BBP_EXP_MAP_READ,17,22);bbp_exp_read_diagnostics(&d);
    assert(d.last_offset[BBP_EXP_MAP_READ]==17 && d.last_result[BBP_EXP_MAP_READ]==22);
    bbp_exp_diag.counts[BBP_EXP_MAP_READ]=UINT32_MAX;
    bbp_exp_observe(BBP_EXP_MAP_READ,18,-7);bbp_exp_read_diagnostics(&d);
    assert(d.counts[BBP_EXP_MAP_READ]==UINT32_MAX && d.last_result[BBP_EXP_MAP_READ]==-7);
    uint32_t old=epoch;bbp_exp_ccc_changed(bbp_exp_hids.attrs+9,1);assert(epoch!=old);
}
int main(int argc,char **argv) {
    init();
    if(argc==2 && !strcmp(argv[1],"--aggregate-only")) {
        struct _bt_gatt_ccc *ccc=bbp_exp_hids.attrs[9].user_data;
        ccc->cfg_write=NULL;test_ccc_cycles();return 0;
    }
    test_capture();test_send();test_ccc_cycles();test_lifecycle();
    for(unsigned int i=0;i<3;++i) { bbp_exp_transport_disconnected(&peers[i]);assert(peers[i].refs==1); }
    assert(ref_calls==unref_calls && !lock_depth);
    puts("PASS pure transport adapter: peer refs balanced; capture/send races; suspend/resume; old-peer neutral release; reconnect/cross-slot generation; bounded metadata diagnostics; 10000 lifecycles; HOST ONLY");
    return 0;
}
