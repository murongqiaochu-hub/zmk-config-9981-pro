/* SPDX-License-Identifier: MIT
 * Host seams only. No real Bluetooth controller, settings store or device I/O.
 */
#include "test_env.h"
#include <stdarg.h>
#include <zmk/ble.h>

const bt_addr_le_t test_addr_any={0};
const bt_addr_le_t test_addr_none={.type=0xff,.a={{0xff,0xff,0xff,0xff,0xff,0xff}}};
struct test_metrics test_metrics;
struct bt_conn test_connections[CONFIG_BT_MAX_CONN];
unsigned test_conn_callback_count,test_init_count,test_listener_count;
unsigned test_irq_depth,test_sched_depth;
int test_select_on_sched_lock=-1,test_select_on_settings_block=-1;
bool test_snapshot_getter_armed;
unsigned test_snapshot_getter_hits;
bt_addr_le_t test_update_profile_addr;
static int deferred_select=-1,deferred_profile=-1;
static bt_addr_le_t deferred_profile_addr;
int test_adv_start_result,test_adv_stop_result,test_work_result;
static struct bt_conn_cb *conn_callbacks[16];
static const struct bt_conn_auth_cb *auth_callbacks[8];
static struct bt_conn_auth_info_cb *auth_info_callbacks[8];
static unsigned auth_count,auth_info_count;
static struct { const char *name; int (*fn)(void); unsigned priority; } init_calls[16];
static struct { const char *name; int (*fn)(const zmk_event_t *); } listeners[16];
static struct settings_handler *settings_handlers[8];
static unsigned settings_count;
static struct k_work *work_items[64];
static unsigned work_count;
static uint64_t now_ms,work_order;
static unsigned next_conn_serial=1;
static struct bt_bond_info bonds[CONFIG_BT_MAX_PAIRED+4];
static unsigned bond_count;

void test_log(const char *fmt,...) { (void)fmt;test_metrics.logs++; }
unsigned int irq_lock(void) { return test_irq_depth++; }
void irq_unlock(unsigned int key) { assert(test_irq_depth==key+1);test_irq_depth--; }
k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);lock->held=1;return irq_lock();
}
void k_spin_unlock(struct k_spinlock *lock,k_spinlock_key_t key) {
    assert(lock->held);lock->held=0;irq_unlock(key);
}
int64_t k_uptime_get(void) { return (int64_t)now_ms; }
uint32_t k_uptime_get_32(void) { return (uint32_t)now_ms; }
void *k_current_get(void) { return NULL; }
static void perform_select(int index) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    assert(test_sched_depth==0);assert(zmk_ble_prof_select((uint8_t)index)==0);
#else
    (void)index;assert(false&&"BLE-OFF cannot schedule profile selection");
#endif
}
/* Only the policy TU's external public getter calls are macro-bridged here.
 * The frozen SDK and test TU call the unmodified real getter. The oracle arms
 * AFTER reconcile is queued, so this is the first worker target getter, not an
 * unrelated bookkeeping scheduler lock. A competing RAM writer runs only at
 * the outer unlock, after target_snapshot has copied its peer. */
bt_addr_le_t *test_policy_profile_address(uint8_t index) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    bt_addr_le_t *peer=zmk_ble_profile_address(index);
    if(test_snapshot_getter_armed){
        test_snapshot_getter_armed=false;test_snapshot_getter_hits++;
        assert(test_sched_depth > 0 && "target_snapshot getter must hold scheduler lock");
        deferred_profile=index;deferred_profile_addr=test_update_profile_addr;
    }
    return peer;
#else
    (void)index;assert(!test_snapshot_getter_armed);return NULL;
#endif
}
void k_sched_lock(void) {
    test_sched_depth++;test_metrics.sched_locks++;
    test_metrics.sched_max_depth=MAX(test_metrics.sched_max_depth,test_sched_depth);
    if(test_select_on_sched_lock>=0){
        deferred_select=test_select_on_sched_lock;test_select_on_sched_lock=-1;
    }
}
void k_sched_unlock(void) {
    assert(test_sched_depth>0);test_sched_depth--;test_metrics.sched_unlocks++;
    if(!test_sched_depth&&deferred_profile>=0){
        int index=deferred_profile;deferred_profile=-1;
        test_load_profile((uint8_t)index,&deferred_profile_addr);
    }
    if(!test_sched_depth&&deferred_select>=0){
        int index=deferred_select;deferred_select=-1;perform_select(index);
    }
}

static int schedule(struct k_work *w,int64_t delay,bool replace) {
    assert(w && w->handler);
    test_metrics.work_schedules++;
    if(test_work_result<0)return test_work_result;
    if(w->pending&&!replace)return 0;
    bool found=false;
    for(unsigned i=0;i<work_count;i++)if(work_items[i]==w)found=true;
    if(!found){assert(work_count<ARRAY_SIZE(work_items));work_items[work_count++]=w;}
    bool was_pending=w->pending;
    w->pending=true;w->due=now_ms+(uint64_t)MAX(delay,0);w->order=++work_order;
    return was_pending?0:1;
}
int k_work_submit(struct k_work *w) { return schedule(w,0,false); }
int k_work_schedule(struct k_work_delayable *w,k_timeout_t delay) { return schedule(&w->work,delay,false); }
int k_work_reschedule(struct k_work_delayable *w,k_timeout_t delay) { return schedule(&w->work,delay,true); }
int k_work_cancel(struct k_work *w) { bool pending=w->pending;w->pending=false;return pending?1:0; }
int k_work_cancel_delayable(struct k_work_delayable *w) { return k_work_cancel(&w->work); }
bool k_work_is_pending(const struct k_work *w) { return w->pending; }
int k_work_busy_get(const struct k_work *w) { return w->pending?K_WORK_SCHEDULED:0; }
void k_work_init(struct k_work *w,void (*handler)(struct k_work *)) { *w=(struct k_work){.handler=handler}; }
void k_work_init_delayable(struct k_work_delayable *w,void (*handler)(struct k_work *)) { k_work_init(&w->work,handler); }
static struct k_work *next_work(uint64_t through) {
    struct k_work *next=NULL;
    for(unsigned i=0;i<work_count;i++){
        struct k_work *w=work_items[i];
        if(!w->pending||w->due>through)continue;
        if(!next||w->due<next->due||(w->due==next->due&&w->order<next->order))next=w;
    }
    return next;
}
void test_run_ready(void) {
    unsigned guard=0;struct k_work *w;
    while((w=next_work(now_ms))!=NULL){
        assert(++guard<10000 && "unbounded immediate work loop");
        w->pending=false;test_metrics.work_runs++;w->handler(w);
    }
}
void test_advance(uint64_t elapsed) {
    uint64_t target=now_ms+elapsed;unsigned guard=0;struct k_work *w;
    while((w=next_work(target))!=NULL){
        assert(++guard<10000 && "unbounded delayed work loop");
        now_ms=MAX(now_ms,w->due);test_run_ready();
    }
    now_ms=target;
}
unsigned test_pending_work(void) {
    unsigned result=0;for(unsigned i=0;i<work_count;i++)result+=work_items[i]->pending;return result;
}
void test_register_init(const char *name,int (*fn)(void),unsigned priority) {
    assert(test_init_count<ARRAY_SIZE(init_calls));
    init_calls[test_init_count].name=name;init_calls[test_init_count].fn=fn;
    init_calls[test_init_count++].priority=priority;
}
void test_run_inits(void) {
    bool called[16]={0};
    for(unsigned n=0;n<test_init_count;n++){
        unsigned next=UINT_MAX;
        for(unsigned i=0;i<test_init_count;i++)if(!called[i]&&(next==UINT_MAX||init_calls[i].priority<init_calls[next].priority))next=i;
        assert(next!=UINT_MAX);called[next]=true;assert(init_calls[next].fn()==0);
    }
}
void test_register_listener(const char *name,int (*fn)(const zmk_event_t *)) {
    assert(test_listener_count<ARRAY_SIZE(listeners));
    listeners[test_listener_count].name=name;listeners[test_listener_count++].fn=fn;
}
int test_raise_event(const char *type,const void *data) {
    assert(test_irq_depth==0);test_metrics.events++;
    zmk_event_t event={.type=type,.data=data};
    for(unsigned i=0;i<test_listener_count;i++)listeners[i].fn(&event);
    return 0;
}

int bt_addr_le_to_str(const bt_addr_le_t *addr,char *out,size_t length) {
    return snprintf(out,length,"%02x:%02x:%02x:%02x:%02x:%02x(%u)",addr->a.val[5],addr->a.val[4],addr->a.val[3],addr->a.val[2],addr->a.val[1],addr->a.val[0],addr->type);
}
bt_addr_le_t test_address(unsigned seed,uint8_t type) {
    bt_addr_le_t addr={.type=type,.a={{(uint8_t)seed,(uint8_t)(seed>>8),0x99,0x81,0x23,0xc0}}};
    return addr;
}
void bt_conn_cb_register(struct bt_conn_cb *cb) {
    assert(test_irq_depth==0&&test_conn_callback_count<ARRAY_SIZE(conn_callbacks));
    for(unsigned i=0;i<test_conn_callback_count;i++)assert(conn_callbacks[i]!=cb);
    conn_callbacks[test_conn_callback_count++]=cb;
}
int bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb) {
    assert(test_irq_depth==0&&auth_count<ARRAY_SIZE(auth_callbacks));auth_callbacks[auth_count++]=cb;return 0;
}
int bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb) {
    assert(test_irq_depth==0&&auth_info_count<ARRAY_SIZE(auth_info_callbacks));auth_info_callbacks[auth_info_count++]=cb;return 0;
}
struct bt_conn *bt_conn_ref(struct bt_conn *conn) {
    assert(test_irq_depth==0&&conn&&conn->used&&conn->refs>0);conn->refs++;test_metrics.conn_refs++;return conn;
}
void bt_conn_unref(struct bt_conn *conn) {
    assert(test_irq_depth==0&&conn&&conn->used&&conn->refs>1);conn->refs--;test_metrics.conn_unrefs++;
}
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn) { assert(test_irq_depth==0);return &conn->dst; }
uint8_t bt_conn_index(const struct bt_conn *conn) { return (uint8_t)conn->index; }
int bt_conn_get_info(const struct bt_conn *conn,struct bt_conn_info *info) {
    static const bt_addr_le_t local={0};
    assert(test_irq_depth==0&&conn&&info);
    *info=(struct bt_conn_info){.type=conn->type,.role=conn->role,.id=conn->id,.state=conn->state,
        .le={.src=&local,.dst=&conn->dst,.local=&local,.remote=&conn->rpa},.security={.level=BT_SECURITY_L2}};
    /* Stock v0.3.0 ignores this return in a few callbacks; leave a deterministic
     * info value while still requiring the policy to respect the error. */
    return conn->info_error;
}
struct bt_conn *bt_conn_lookup_addr_le(uint8_t id,const bt_addr_le_t *addr) {
    assert(test_irq_depth==0);
    for(unsigned i=0;i<ARRAY_SIZE(test_connections);i++){
        struct bt_conn *c=&test_connections[i];
        if(c->used&&c->id==id&&c->state!=BT_CONN_STATE_DISCONNECTED&&!bt_addr_le_cmp(&c->dst,addr))return bt_conn_ref(c);
    }
    return NULL;
}
void bt_conn_foreach(int type,void (*cb)(struct bt_conn *,void *),void *data) {
    assert(test_irq_depth==0);
    for(unsigned i=0;i<ARRAY_SIZE(test_connections);i++){
        struct bt_conn *c=&test_connections[i];
        if(c->used&&(c->type&type))cb(c,data);
    }
}
int bt_conn_disconnect(struct bt_conn *conn,uint8_t reason) {
    assert(test_irq_depth==0&&reason==BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    conn->disconnect_calls++;test_metrics.disconnects++;
    if(!conn->disconnect_result)conn->state=BT_CONN_STATE_DISCONNECTING;
    return conn->disconnect_result;
}
struct bt_conn *test_seed_conn(unsigned index,const bt_addr_le_t *addr,uint8_t role,uint8_t id) {
    assert(index<ARRAY_SIZE(test_connections));
    struct bt_conn *c=&test_connections[index];assert(!c->used||c->refs==1);
    *c=(struct bt_conn){.used=true,.resolved=!bt_addr_le_is_rpa(addr),.serial=next_conn_serial++,
        .refs=1,.index=index,.type=BT_CONN_TYPE_LE,.role=role,.id=id,.state=BT_CONN_STATE_CONNECTED,
        .dst=*addr,.rpa=*addr,.identity=*addr};
    return c;
}
void test_emit_connected(struct bt_conn *conn,uint8_t err) {
    conn->state=err?BT_CONN_STATE_DISCONNECTED:BT_CONN_STATE_CONNECTED;
    for(unsigned i=0;i<test_conn_callback_count;i++)if(conn_callbacks[i]->connected)conn_callbacks[i]->connected(conn,err);
}
void test_emit_disconnected(struct bt_conn *conn,uint8_t reason) {
    conn->state=BT_CONN_STATE_DISCONNECTED;
    for(unsigned i=0;i<test_conn_callback_count;i++)if(conn_callbacks[i]->disconnected)conn_callbacks[i]->disconnected(conn,reason);
}
void test_emit_identity(struct bt_conn *conn,const bt_addr_le_t *identity) {
    bt_addr_le_t rpa=conn->rpa;conn->identity=*identity;conn->dst=*identity;conn->resolved=true;
    for(unsigned i=0;i<test_conn_callback_count;i++)if(conn_callbacks[i]->identity_resolved)conn_callbacks[i]->identity_resolved(conn,&rpa,identity);
}
void test_emit_security(struct bt_conn *conn,bt_security_t level,enum bt_security_err err) {
    for(unsigned i=0;i<test_conn_callback_count;i++)if(conn_callbacks[i]->security_changed)conn_callbacks[i]->security_changed(conn,level,err);
}
enum bt_security_err test_pair_accept(struct bt_conn *conn) {
    struct bt_conn_pairing_feat feat={0};enum bt_security_err result=BT_SECURITY_ERR_SUCCESS;
    for(unsigned i=0;i<auth_count;i++)if(auth_callbacks[i]->pairing_accept){
        enum bt_security_err next=auth_callbacks[i]->pairing_accept(conn,&feat);
        if(next!=BT_SECURITY_ERR_SUCCESS)result=next;
    }
    return result;
}
void test_pair_complete(struct bt_conn *conn,bool bonded) {
    if(bonded)test_add_bond(&conn->dst);
    for(unsigned i=0;i<auth_info_count;i++)if(auth_info_callbacks[i]->pairing_complete)auth_info_callbacks[i]->pairing_complete(conn,bonded);
}
void test_pair_cancel(struct bt_conn *conn) {
    for(unsigned i=0;i<auth_count;i++)if(auth_callbacks[i]->cancel)auth_callbacks[i]->cancel(conn);
}
void test_pair_failed(struct bt_conn *conn,enum bt_security_err reason) {
    for(unsigned i=0;i<auth_info_count;i++)if(auth_info_callbacks[i]->pairing_failed)auth_info_callbacks[i]->pairing_failed(conn,reason);
}
void test_add_bond(const bt_addr_le_t *addr) {
    for(unsigned i=0;i<bond_count;i++)if(!bt_addr_le_cmp(&bonds[i].addr,addr))return;
    assert(bond_count<ARRAY_SIZE(bonds));bonds[bond_count++].addr=*addr;
}
void bt_foreach_bond(uint8_t id,void (*cb)(const struct bt_bond_info *,void *),void *data) {
    assert(test_irq_depth==0);if(id==BT_ID_DEFAULT)for(unsigned i=0;i<bond_count;i++)cb(&bonds[i],data);
}
int bt_enable(void (*ready)(int)) { if(ready)ready(0);return 0; }
int bt_unpair(uint8_t id,const bt_addr_le_t *addr) { (void)id;(void)addr;assert(test_irq_depth==0);test_metrics.unpairs++;return 0; }
int bt_set_name(const char *name) { (void)name;return 0; }
int bt_le_adv_start(const struct bt_le_adv_param *param,const struct bt_data *ad,size_t adlen,const struct bt_data *sd,size_t sdlen) {
    (void)param;(void)ad;(void)adlen;(void)sd;(void)sdlen;
    assert(test_irq_depth==0);test_metrics.adv_starts++;return test_adv_start_result;
}
int bt_le_adv_stop(void) { assert(test_irq_depth==0);test_metrics.adv_stops++;return test_adv_stop_result; }

int settings_register(struct settings_handler *handler) {
    assert(test_irq_depth==0&&settings_count<ARRAY_SIZE(settings_handlers));settings_handlers[settings_count++]=handler;return 0;
}
int settings_name_steq(const char *name,const char *key,const char **next) {
    size_t len=strlen(key);if(strncmp(name,key,len)|| (name[len]&&name[len]!='/'))return 0;
    *next=name[len]=='/'?name+len+1:NULL;return 1;
}
int settings_save_one(const char *name,const void *data,size_t length) {
    assert(test_irq_depth==0&&data&&length);
    assert(!strncmp(name,"ble/",4)&&"policy must not introduce another settings namespace");
    test_metrics.settings_saves++;
    if(test_select_on_settings_block>=0&&!strncmp(name,"ble/profiles/",13)){
        /* Model an explicitly BLOCKING backend after native set_profile_address
         * captured its index/name and copied the profile. A scheduler-locked
         * thread can block; another thread may now select without rewriting the
         * already captured destination. Never inject before this safe point. */
        int index=test_select_on_settings_block;test_select_on_settings_block=-1;
        unsigned held=test_sched_depth;test_sched_depth=0;perform_select(index);test_sched_depth=held;
    }
    return 0;
}
int settings_delete(const char *name) { (void)name;assert(test_irq_depth==0);test_metrics.settings_deletes++;return 0; }
static int settings_copy(void *source,void *destination,size_t length) { memcpy(destination,source,length);return (int)length; }
static struct settings_handler *ble_handler(void) {
    for(unsigned i=0;i<settings_count;i++)if(!strcmp(settings_handlers[i]->name,"ble"))return settings_handlers[i];
    assert(false&&"real SDK must register BLE settings handler");return NULL;
}
void test_load_profile(uint8_t index,const bt_addr_le_t *addr) {
    struct zmk_ble_profile profile={.peer=*addr};char name[32];
    snprintf(name,sizeof(name),"profiles/%u",index);
    assert(ble_handler()->h_set(name,sizeof(profile),settings_copy,&profile)==0);test_add_bond(addr);
}
void test_load_active(uint8_t index) { assert(ble_handler()->h_set("active_profile",sizeof(index),settings_copy,&index)==0); }
void test_commit_settings(void) {
    for(unsigned i=0;i<settings_count;i++)if(settings_handlers[i]->h_commit)assert(settings_handlers[i]->h_commit()==0);
}
void test_reset_metrics(void) { memset(&test_metrics,0,sizeof(test_metrics)); }
void test_assert_balanced(void) {
    for(unsigned i=0;i<ARRAY_SIZE(test_connections);i++)if(test_connections[i].used)assert(test_connections[i].refs==1);
    assert(test_irq_depth==0&&test_sched_depth==0);
    assert(test_metrics.sched_locks==test_metrics.sched_unlocks);
    assert(!test_metrics.settings_deletes&&!test_metrics.unpairs&&!test_metrics.usb_mutations);
}
