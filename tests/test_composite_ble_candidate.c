/* SPDX-License-Identifier: MIT
 * Host test for a gated BLE candidate, actual pinned HOG TU and snapshot header.
 * Runner uses a temporary COPY of the fixed HID mock with its representative
 * 45-byte map replaced by the SHA-verified 196-byte fixture. This is not an SDK
 * image. All CCC/ATT/connection/runtime/workqueue behaviors below are mocks.
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "candidate tests require assertions"
#endif
#ifndef BBP9981_TRACKPAD_HOG_SNAPSHOT_H
#error "actual snapshot preinclude is required"
#endif
#ifndef zmk_hog_send_mouse_report
#error "candidate must preserve snapshot mouse rename"
#endif
#ifndef bt_gatt_attr_read
#error "candidate read preinclude is required"
#endif
#include "gamepad_ble_mocks/sdk_v0_3_0_hog.c"
#undef bt_gatt_attr_read
#undef BT_GATT_SERVICE_DEFINE
#undef zmk_hog_send_mouse_report
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
#undef zmk_hog_send_keyboard_report
#endif

#define VALUE 26U
#define CCC 27U
static struct bt_conn peer[4], *active;
static struct zmk_endpoint_instance endpoint;
static int profile;
static bool no_dst;
static uint32_t epoch;
static struct bbp_gamepad_report_body current;
static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static struct zmk_hid_mouse_report mouse;
static unsigned int refs, unrefs, lock_depth, invalidations, raw_reads;
static unsigned int cfg_calls, aggregate_calls, subscription_calls, info_calls;
static unsigned int led_calls, security_calls, submit_calls, schedule_calls;
static uint8_t led_value; static int led_profile, notify_error;
static void (*subscription_hook)(void), (*active_hook)(void), (*report_hook)(void);
static void (*info_hook)(void), (*notify_hook)(void), (*precommit_hook)(void);
static unsigned int subscription_hook_at, info_hook_at;
struct notification {
    struct bt_conn *conn; const struct bt_gatt_attr *attr; uint16_t len; uint8_t data[32];
};
static struct notification sent[64];
static size_t sent_count;
struct k_work_q k_sys_work_q;
static void call_hook(void (**slot)(void)) { void (*fn)(void)=*slot; *slot=NULL; if(fn) { fn(); } }

#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
void bbp_menu_diag_note(uint8_t kind,int32_t a,int32_t b,int32_t c,int32_t d) {
    (void)kind;(void)a;(void)b;(void)c;(void)d;
}
void bbp_menu_diag_tx(uint8_t kind,uint8_t buttons,int result,unsigned outstanding) {
    (void)kind;(void)buttons;(void)result;(void)outstanding;
}
#endif
struct bt_conn *bt_conn_ref(struct bt_conn *c) {
    assert(!lock_depth && c && c->refs>0); ++c->refs; ++refs; return c;
}
void bt_conn_unref(struct bt_conn *c) {
    assert(!lock_depth && c && c->refs>1); --c->refs; ++unrefs;
}
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *c) {
    assert(!lock_depth && c); return no_dst?NULL:&c->dst;
}
int bt_conn_get_info(const struct bt_conn *c,struct bt_conn_info *out) {
    assert(!lock_depth && c && out); out->state=c->state; ++info_calls;
    if(info_calls==info_hook_at) { call_hook(&info_hook); }
    return c->info_error;
}
int bt_conn_set_security(struct bt_conn *c,bt_security_t level) {
    assert(!lock_depth && c && level==BT_SECURITY_L2); ++security_calls; return 0;
}
int zmk_ble_active_profile_index(void) { assert(!lock_depth); return profile; }
int zmk_ble_profile_index(const bt_addr_le_t *dst) {
    assert(!lock_depth && dst); return dst->a.val[5]==255U ? -1 : dst->a.val[5];
}
struct bt_conn *zmk_ble_active_profile_conn(void) {
    assert(!lock_depth); struct bt_conn *result=active?bt_conn_ref(active):NULL;
    call_hook(&active_hook); return result;
}
struct zmk_endpoint_instance zmk_endpoints_selected(void) { assert(!lock_depth); return endpoint; }
uint32_t bbp_gamepad_context_epoch(void) { return epoch; }
bool bbp_gamepad_context_valid(uint32_t value) { return value==epoch; }
void bbp_gamepad_transport_invalidate(void) {
    assert(!lock_depth); ++epoch; ++invalidations; current=bbp_gamepad_neutral();
}
void bbp_gamepad_get_report(uint8_t transport,struct bbp_gamepad_report_body *out) {
    assert(!lock_depth && transport==ZMK_TRANSPORT_BLE && out); *out=current; call_hook(&report_hook);
}
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) { return &keyboard; }
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) { return &consumer; }
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void) { return &mouse; }
void zmk_hid_indicators_process_report(struct zmk_hid_led_report_body *r,
                                       struct zmk_endpoint_instance e) {
    assert(r && e.transport==ZMK_TRANSPORT_BLE); ++led_calls; led_value=r->leds; led_profile=e.ble.profile_index;
}
ssize_t bt_gatt_attr_read(struct bt_conn *c,const struct bt_gatt_attr *a,void *buf,
                          uint16_t len,uint16_t off,const void *value,uint16_t size) {
    (void)c;(void)a; assert(!lock_depth); ++raw_reads;
    if(off>size) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    size_t n=size-off; if(n>len) { n=len; }
    if(n && (!buf || !value)) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    if(n) { memcpy(buf,(const uint8_t *)value+off,n); } return (ssize_t)n;
}
ssize_t bt_gatt_attr_read_service(struct bt_conn *c,const struct bt_gatt_attr *a,
                                  void *buf,uint16_t len,uint16_t off) {
    const struct bt_uuid *u=a->user_data; return bt_gatt_attr_read(c,a,buf,len,off,&u->val,2);
}
ssize_t bt_gatt_attr_read_chrc(struct bt_conn *c,const struct bt_gatt_attr *a,
                               void *buf,uint16_t len,uint16_t off) {
    const struct bt_gatt_chrc *chrc=a->user_data;
    return bt_gatt_attr_read(c,a,buf,len,off,&chrc->properties,1);
}
/* Pinned CCC ordering MODEL, not Zephyr. id==1 denotes an occupied host slot;
 * real Zephyr uses a local identity/address and has settings/security handling.
 * It accepts 1 or 2 bytes, returns early on absent-entry zero, calls cfg_write
 * before commit, and calls cfg_changed only when the aggregate max changes. */
static bool same_addr(const bt_addr_le_t *a,const bt_addr_le_t *b) {
    return a->type==b->type && !memcmp(a->a.val,b->a.val,sizeof(a->a.val));
}
static struct bt_gatt_ccc_cfg *find_slot(struct _bt_gatt_ccc *ccc,const struct bt_conn *c,bool create) {
    struct bt_gatt_ccc_cfg *free_slot=NULL;
    if(!ccc || !c) { return NULL; }
    for(size_t i=0;i<BT_GATT_CCC_MAX;++i) {
        struct bt_gatt_ccc_cfg *p=&ccc->cfg[i];
        if(p->id && same_addr(&p->peer,&c->dst)) { return p; }
        if(!p->id && !free_slot) { free_slot=p; }
    }
    if(create && free_slot) { free_slot->id=1; free_slot->peer=c->dst; return free_slot; }
    return NULL;
}
ssize_t bt_gatt_attr_read_ccc(struct bt_conn *c,const struct bt_gatt_attr *a,
                              void *buf,uint16_t len,uint16_t off) {
    if(!c || !a || !a->user_data) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    struct bt_gatt_ccc_cfg *slot=find_slot(a->user_data,c,false);
    uint16_t value=slot?slot->value:0; uint8_t bytes[2]={(uint8_t)value,(uint8_t)(value>>8)};
    return bt_gatt_attr_read(c,a,buf,len,off,bytes,2);
}
ssize_t bt_gatt_attr_write_ccc(struct bt_conn *c,const struct bt_gatt_attr *a,
                               const void *buf,uint16_t len,uint16_t off,uint8_t flags) {
    (void)flags; assert(!lock_depth);
    if(off) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    if(!len || len>2) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN); }
    if(!c || !a || !a->user_data || !buf) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    const uint8_t *b=buf; uint16_t value=(uint16_t)(b[0] | (len==2 ? (uint16_t)b[1]<<8 : 0));
    struct _bt_gatt_ccc *ccc=a->user_data;
    struct bt_gatt_ccc_cfg *slot=find_slot(ccc,c,false);
    if(!slot && !value) { return len; } /* Actual pinned early return. */
    if(!slot) { slot=find_slot(ccc,c,true); }
    if(!slot) { return BT_GATT_ERR(0x11); }
    if(ccc->cfg_write) {
        ++cfg_calls; ssize_t n=ccc->cfg_write(c,a,value);
        if(n<0) { return n; }
        if(n!=1 && n!=(ssize_t)sizeof(uint16_t)) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    }
    call_hook(&precommit_hook); /* Explicit point BEFORE SDK cfg->value commit. */
    slot->value=value;
    uint16_t max=0;
    for(size_t i=0;i<BT_GATT_CCC_MAX;++i) { if(ccc->cfg[i].value>max) { max=ccc->cfg[i].value; } }
    if(max!=ccc->value) {
        ccc->value=max;
        if(ccc->cfg_changed) { ++aggregate_calls; ccc->cfg_changed(a,max); }
    }
    if(!value) { memset(slot,0,sizeof(*slot)); }
    return len;
}
bool bt_gatt_is_subscribed(struct bt_conn *c,const struct bt_gatt_attr *a,uint16_t type) {
    assert(!lock_depth && c && a==&hog_svc.attrs[VALUE] && type==BT_GATT_CCC_NOTIFY);
    ++subscription_calls;
    struct bt_gatt_ccc_cfg *slot=find_slot(hog_svc.attrs[CCC].user_data,c,false);
    bool yes=c->state==BT_CONN_STATE_CONNECTED && slot && (slot->value&type);
    if(subscription_calls==subscription_hook_at) { call_hook(&subscription_hook); }
    return yes;
}
int bt_gatt_notify_cb(struct bt_conn *c,struct bt_gatt_notify_params *p) {
    assert(!lock_depth && c && p && p->attr && p->data && p->len<=sizeof(sent[0].data));
    assert(!p->uuid && !p->func && !p->user_data);
    if(p->attr==&hog_svc.attrs[VALUE]) {
        assert(p->len==10 && p->attr->perm==BT_GATT_PERM_READ_ENCRYPT);
        call_hook(&notify_hook); /* Event after final gate; this submission may happen. */
    } else {
        assert(p->attr==&hog_svc.attrs[5] || p->attr==&hog_svc.attrs[9] || p->attr==&hog_svc.attrs[13]);
    }
    assert(sent_count<ARRAY_SIZE(sent)); struct notification *r=&sent[sent_count++];
    *r=(struct notification){.conn=c,.attr=p->attr,.len=p->len};
    memcpy(r->data,p->data,p->len); /* Synchronous mock only; no real ATT. */
    return notify_error;
}
k_spinlock_key_t k_spin_lock(struct k_spinlock *l) { assert(l && !l->held); l->held=true; return lock_depth++; }
void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t key) { assert(l && l->held && lock_depth==key+1); l->held=false; --lock_depth; }
int64_t k_uptime_get(void) { return 0; }
int k_msgq_put(struct k_msgq *q,const void *data,k_timeout_t timeout) {
    assert(q && data && (timeout==K_NO_WAIT || timeout==K_MSEC(100)));
    if(q->used==q->capacity) { return -EAGAIN; }
    memcpy(q->buffer+q->write_pos*q->item_size,data,q->item_size);
    q->write_pos=(q->write_pos+1)%q->capacity; ++q->used; return 0;
}
int k_msgq_get(struct k_msgq *q,void *data,k_timeout_t timeout) {
    assert(q && data && timeout==K_NO_WAIT); if(!q->used) { return -ENOMSG; }
    memcpy(data,q->buffer+q->read_pos*q->item_size,q->item_size);
    q->read_pos=(q->read_pos+1)%q->capacity; --q->used; return 0;
}
int k_msgq_peek(struct k_msgq *q,void *data) {
    assert(q && data); if(!q->used) { return -ENOMSG; }
    memcpy(data,q->buffer+q->read_pos*q->item_size,q->item_size); return 0;
}
unsigned int k_msgq_num_used_get(struct k_msgq *q) { return q->used; }
int k_work_submit_to_queue(struct k_work_q *q,struct k_work *w) {
    assert(q==&hog_work_q && q->started && w); ++submit_calls; return 1;
}
int k_work_schedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,k_timeout_t delay) {
    assert(q && w && delay>=0); ++schedule_calls;
    if(!q->started) { return -ENODEV; } if(q->plugged) { return -EBUSY; }
    w->queue=q; w->due=delay; w->queued=true; return 1;
}
void k_work_queue_start(struct k_work_q *q,void *stack,size_t size,int priority,
                        const struct k_work_queue_config *config) {
    assert(q==&hog_work_q && stack && size && priority==CONFIG_ZMK_BLE_THREAD_PRIORITY && config);
    q->started=true;
}
static void choose(unsigned int p) {
    assert(p<ARRAY_SIZE(peer)); active=&peer[p]; profile=peer[p].dst.a.val[5];
    endpoint=(struct zmk_endpoint_instance){.transport=ZMK_TRANSPORT_BLE,.ble={.profile_index=profile}};
}
static void setup(void) {
    assert(!lock_depth && refs==unrefs && !bbp9981_hog_mouse_stats.outstanding && !bbp9981_hog_mouse_fifo.used);
    for(size_t i=0;i<ARRAY_SIZE(peer);++i) {
        peer[i]=(struct bt_conn){.state=BT_CONN_STATE_CONNECTED,.refs=1};
        peer[i].dst.type=1; peer[i].dst.a.val[0]=0x55; peer[i].dst.a.val[5]=(uint8_t)i;
    }
    choose(0); no_dst=false; epoch=10; current=bbp_gamepad_neutral();
    keyboard.body=(struct zmk_hid_keyboard_report_body){.modifiers=8,.keys={4,5}};
    consumer.body=(struct zmk_hid_consumer_report_body){.keys={0x123}};
    mouse.body=(struct zmk_hid_mouse_report_body){.buttons=3,.d_x=-12,.d_y=24};
    for(size_t i=0;i<hog_svc.attr_count;++i) if(hog_svc.attrs[i].uuid->val==0x2902) {
        struct _bt_gatt_ccc *c=hog_svc.attrs[i].user_data; memset(c->cfg,0,sizeof(c->cfg)); c->value=0;
    }
    host_requests_notification=false; ctrl_point=0;
    memset(&bbp9981_hog_mouse_stats,0,sizeof(bbp9981_hog_mouse_stats));
    memset(&bbp9981_hog_mouse_head,0,sizeof(bbp9981_hog_mouse_head));
    bbp9981_hog_mouse_fifo.read_pos=bbp9981_hog_mouse_fifo.write_pos=0;
    bbp9981_hog_mouse_work.queued=false; bbp9981_hog_mouse_dispatch.queued=false;
    hog_work_q=(struct k_work_q){0}; k_sys_work_q=(struct k_work_q){.started=true};
    refs=unrefs=invalidations=raw_reads=cfg_calls=aggregate_calls=subscription_calls=info_calls=0;
    led_calls=security_calls=submit_calls=schedule_calls=0; led_value=0; led_profile=-1;
    notify_error=0; sent_count=0;
    subscription_hook=active_hook=report_hook=info_hook=notify_hook=precommit_hook=NULL;
    subscription_hook_at=info_hook_at=0;
    assert(gamepad_mock_hog_init()==0);
}
static void balanced(void) {
    assert(!lock_depth && refs==unrefs);
    for(size_t i=0;i<ARRAY_SIZE(peer);++i) { assert(peer[i].refs==1); }
    assert(!bbp9981_hog_mouse_stats.outstanding && !bbp9981_hog_mouse_fifo.used);
}
static ssize_t write_ccc(unsigned int p,uint16_t value,uint16_t size) {
    uint8_t bytes[2]={(uint8_t)value,(uint8_t)(value>>8)};
    return hog_svc.attrs[CCC].write(&peer[p],&hog_svc.attrs[CCC],bytes,size,0,0);
}
static int send_to(struct bt_conn *c,const struct bbp_gamepad_report_body *body,uint32_t e) {
    if(!c) { return bbp_gamepad_ble_send(NULL,body,e); }
    int old=c->refs; bt_conn_ref(c);
    int ret=bbp_gamepad_ble_send(c,body,e);
    assert(c->refs==old+1); bt_conn_unref(c); return ret;
}
static struct bbp_gamepad_report_body known(void) {
    struct bbp_gamepad_report_body b=bbp_gamepad_neutral(); b.buttons_lo=1; b.x=127; b.lt=255; return b;
}
static void ctx_invalidate(void) { bbp_gamepad_transport_invalidate(); }
static void flip_route(void) { endpoint.transport=ZMK_TRANSPORT_USB; }
static void replace_active(void) { active=&peer[2]; }
static void disconnect_active(void) { peer[0].state=BT_CONN_STATE_DISCONNECTED; }
static void off_on_primary(void) { assert(write_ccc(0,0,2)==2); assert(write_ccc(0,1,2)==2); }
static void off_primary(void) { assert(write_ccc(0,0,2)==2); }
static void allow_two(void) { assert(write_ccc(0,1,2)==2 && write_ccc(1,1,2)==2); }
static void check_characteristic(size_t i,uint16_t uuid,uint8_t props,uint16_t perm,
                                 bt_gatt_attr_read_func_t rd,bt_gatt_attr_write_func_t wr) {
    const struct bt_gatt_attr *a=hog_svc.attrs;
    assert(a[i].uuid->val==0x2803 && a[i].perm==BT_GATT_PERM_READ &&
           a[i].read==bt_gatt_attr_read_chrc && !a[i].write && a[i].user_data);
    const struct bt_gatt_chrc *d=a[i].user_data;
    assert(d->uuid->val==uuid && d->properties==props && !d->value_handle);
    assert(a[i+1].uuid->val==uuid && a[i+1].perm==perm && a[i+1].read==rd &&
           a[i+1].write==wr && !a[i+1].handle);
}
static void check_ccc(size_t i,void (*changed)(const struct bt_gatt_attr *,uint16_t),
                      ssize_t (*write_cb)(struct bt_conn *,const struct bt_gatt_attr *,uint16_t)) {
    const struct bt_gatt_attr *a=hog_svc.attrs+i;
    assert(a->uuid->val==0x2902 && a->perm==(BT_GATT_PERM_READ_ENCRYPT|BT_GATT_PERM_WRITE_ENCRYPT));
    assert(a->read==bt_gatt_attr_read_ccc && a->write==bt_gatt_attr_write_ccc && a->user_data);
    struct _bt_gatt_ccc *c=a->user_data;
    assert(c->cfg_changed==changed && c->cfg_write==write_cb && !c->cfg_match);
}
static void test_structure_and_map(void) {
    const struct bt_gatt_attr *a=hog_svc.attrs;
    assert(a==attr_hog_svc && hog_svc.attr_count==29 && bbp_candidate_map_valid());
    size_t primaries=0; for(size_t i=0;i<hog_svc.attr_count;++i) if(a[i].uuid->val==0x2800) { ++primaries; }
    assert(primaries==1 && a[0].perm==BT_GATT_PERM_READ && a[0].read==bt_gatt_attr_read_service &&
           !a[0].write && ((struct bt_uuid *)a[0].user_data)->val==0x1812);
    const uint8_t in=BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY;
    const uint8_t out=BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE|BT_GATT_CHRC_WRITE_WITHOUT_RESP;
    const uint16_t rw=BT_GATT_PERM_READ_ENCRYPT|BT_GATT_PERM_WRITE_ENCRYPT;
    check_characteristic(1,0x2a4a,BT_GATT_CHRC_READ,BT_GATT_PERM_READ,read_hids_info,NULL);
    assert(a[2].user_data==&info);
    check_characteristic(3,0x2a4b,BT_GATT_CHRC_READ,BT_GATT_PERM_READ_ENCRYPT,read_hids_report_map,NULL);
    check_characteristic(5,0x2a4d,in,BT_GATT_PERM_READ_ENCRYPT,read_hids_input_report,NULL);
    check_ccc(7,input_ccc_changed,NULL);
    check_characteristic(9,0x2a4d,in,BT_GATT_PERM_READ_ENCRYPT,read_hids_consumer_input_report,NULL);
    check_ccc(11,input_ccc_changed,NULL);
    check_characteristic(13,0x2a4d,in,BT_GATT_PERM_READ_ENCRYPT,read_hids_mouse_input_report,NULL);
    check_ccc(15,input_ccc_changed,NULL);
    check_characteristic(17,0x2a4d,out,rw,NULL,write_hids_leds_report);
    check_characteristic(20,0x2a4c,BT_GATT_CHRC_WRITE_WITHOUT_RESP,BT_GATT_PERM_WRITE,NULL,write_ctrl_point);
    assert(a[21].user_data==&ctrl_point);
    check_characteristic(22,0x2a4d,out,rw,bbp_candidate_read_output,bbp_candidate_write_output);
    check_characteristic(25,0x2a4d,in,BT_GATT_PERM_READ_ENCRYPT,bbp_candidate_read_input,NULL);
    check_ccc(CCC,bbp_candidate_ccc_changed,bbp_candidate_ccc_write);
    struct _bt_gatt_ccc *ccc=a[CCC].user_data;
    assert(ccc!=a[7].user_data && ccc!=a[11].user_data && ccc!=a[15].user_data);
    const size_t ri[]={8,12,16,19,24,28}; const uint8_t expected[][2]={{1,1},{2,1},{3,1},{1,2},{5,2},{4,1}};
    const void *ref_data[]={&input,&consumer_input,&mouse_input,&led_indicators,
                            bbp_candidate_output_ref,bbp_candidate_input_ref};
    for(size_t i=0;i<ARRAY_SIZE(ri);++i) {
        const struct bt_gatt_attr *r=a+ri[i];
        assert(r->uuid->val==0x2908 && r->perm==BT_GATT_PERM_READ_ENCRYPT && !r->write &&
               r->read==(i<4 ? read_hids_report_ref : bbp_candidate_read_ref) && r->user_data==ref_data[i]);
        for(uint16_t off=0;off<=3;++off) {
            uint8_t b[3]={0xa5,0xa5,0xa5}; ssize_t n=r->read(&peer[0],r,b,3,off);
            if(off>2) { assert(n==-BT_ATT_ERR_INVALID_OFFSET && b[0]==0xa5); }
            else { size_t want=2-off; assert(n==(ssize_t)want && !memcmp(b,expected[i]+off,want) && b[want]==0xa5); }
        }
        uint8_t b[3]={0xa5,0xa5,0xa5};
        assert(r->read(&peer[0],r,b,3,UINT16_MAX)==-BT_ATT_ERR_INVALID_OFFSET && b[0]==0xa5);
    }
    assert(bbp_candidate_value(&hog_svc)==a+VALUE && !bbp_candidate_value(NULL));
    struct bt_gatt_attr copy[29]; memcpy(copy,a,sizeof(copy));
    struct bt_gatt_service_static bad={.attrs=copy,.attr_count=29};
    copy[VALUE].read=NULL; assert(!bbp_candidate_value(&bad)); copy[VALUE]=a[VALUE];
    copy[CCC].perm=BT_GATT_PERM_WRITE_ENCRYPT; assert(!bbp_candidate_value(&bad)); copy[CCC]=a[CCC];
    copy[CCC].write=NULL; assert(!bbp_candidate_value(&bad)); copy[CCC]=a[CCC];
    copy[28].user_data=bbp_candidate_output_ref; assert(!bbp_candidate_value(&bad)); copy[28]=a[28];
    struct _bt_gatt_ccc wrong_ccc=*(struct _bt_gatt_ccc *)a[CCC].user_data;
    wrong_ccc.cfg_write=NULL; copy[CCC].user_data=&wrong_ccc; assert(!bbp_candidate_value(&bad));
    wrong_ccc=*(struct _bt_gatt_ccc *)a[CCC].user_data; wrong_ccc.cfg_changed=NULL;
    assert(!bbp_candidate_value(&bad));
    uint8_t old_id=bbp_candidate_input_ref[0]; bbp_candidate_input_ref[0]=3;
    assert(!bbp_candidate_value(&hog_svc)); bbp_candidate_input_ref[0]=old_id;
    assert(bbp_candidate_value(&hog_svc)==a+VALUE);
    uint8_t expected_map[BBP_CANDIDATE_MAP_SIZE], b[512];
    memcpy(expected_map,zmk_hid_report_desc,196); memcpy(expected_map+196,bbp_exp_report_map,8);
    expected_map[203]=4; expected_map[204]=0x15; expected_map[205]=0;
    memcpy(expected_map+206,bbp_exp_report_map+8,sizeof(bbp_exp_report_map)-8);
    const uint16_t lengths[]={0,1,2,7,22,64,380,511};
    for(uint16_t off=0;off<=BBP_CANDIDATE_MAP_SIZE+1;++off) for(size_t j=0;j<ARRAY_SIZE(lengths);++j) {
        memset(b,0xa5,sizeof(b)); ssize_t n=a[4].read(&peer[0],a+4,b,lengths[j],off);
        if(off>BBP_CANDIDATE_MAP_SIZE) { assert(n==-BT_ATT_ERR_INVALID_OFFSET && b[0]==0xa5); }
        else {
            size_t want=BBP_CANDIDATE_MAP_SIZE-off; if(want>lengths[j]) { want=lengths[j]; }
            assert(n==(ssize_t)want && !memcmp(b,expected_map+off,want));
            for(size_t k=want;k<sizeof(b);++k) { assert(b[k]==0xa5); }
        }
    }
    assert(a[4].read(&peer[0],a+4,b,1,UINT16_MAX)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a[4].read(&peer[0],a+4,NULL,0,0)==0 && a[4].read(&peer[0],a+4,NULL,0,380)==0);
    assert(a[4].read(&peer[0],a+4,NULL,1,0)==-BT_ATT_ERR_UNLIKELY);
    assert(a[4].read(&peer[0],a+4,(void *)zmk_hid_report_desc,1,0)==-BT_ATT_ERR_UNLIKELY);
    assert(a[4].read(&peer[0],a+4,(void *)bbp_exp_report_map,1,0)==-BT_ATT_ERR_UNLIKELY);
    uint8_t copy_stock[196]; memcpy(copy_stock,zmk_hid_report_desc,196); unsigned int before=raw_reads;
    assert(bbp_candidate_attr_read(&peer[0],a+4,b,196,0,copy_stock,196)==196 && raw_reads==before+1);
    assert(!memcmp(b,copy_stock,196));
    balanced();
}
static void test_reads_output_and_stock(void) {
    const struct bt_gatt_attr *a=hog_svc.attrs;
    const uint8_t n[10]={15,0,0,128,128,128,128,0,0,0}, k[10]={15,4,64,255,128,128,128,255,0,0};
    uint8_t b[32], cmd[6]={1,2,3,4,5,6}; current=known();
    assert(a[VALUE].read(&peer[0],a+VALUE,b,32,0)==10 && !memcmp(b,k,10));
    assert(a[VALUE].read(&peer[1],a+VALUE,b,32,0)==10 && !memcmp(b,n,10));
    assert(a[VALUE].read(NULL,a+VALUE,b,32,0)==10 && !memcmp(b,n,10));
    const uint16_t lens[]={0,1,3,7,10,31};
    for(uint16_t off=0;off<=11;++off) for(size_t j=0;j<ARRAY_SIZE(lens);++j) {
        memset(b,0xa5,sizeof(b)); ssize_t r=a[VALUE].read(&peer[0],a+VALUE,b,lens[j],off);
        if(off>10) { assert(r==-BT_ATT_ERR_INVALID_OFFSET && b[0]==0xa5); }
        else { size_t want=10-off; if(want>lens[j]) { want=lens[j]; }
               assert(r==(ssize_t)want && !memcmp(b,k+off,want) && b[want]==0xa5); }
    }
    report_hook=ctx_invalidate; current=known();
    assert(a[VALUE].read(&peer[0],a+VALUE,b,32,0)==10 && !memcmp(b,n,10));
    current=known(); active_hook=flip_route;
    assert(a[VALUE].read(&peer[0],a+VALUE,b,32,0)==10 && !memcmp(b,n,10)); choose(0);
    current=known(); report_hook=replace_active;
    assert(a[VALUE].read(&peer[0],a+VALUE,b,32,0)==10 && !memcmp(b,n,10)); choose(0);
    current=known(); current.hat=9;
    assert(a[VALUE].read(&peer[0],a+VALUE,b,32,0)==10 && !memcmp(b,n,10));
    for(uint16_t len=0;len<=6;++len)
        assert(a[23].write(&peer[0],a+23,cmd,len,0,0)==(len==4?4:-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN));
    assert(a[23].write(&peer[0],a+23,cmd,4,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a[23].write(&peer[0],a+23,NULL,4,0,0)==-BT_ATT_ERR_UNLIKELY);
    for(unsigned int flags=0;flags<256;++flags)
        assert(a[23].write(&peer[0],a+23,cmd,4,0,(uint8_t)flags)==((flags==0||flags==2)?4:-0x06));
    for(uint16_t off=0;off<=5;++off) for(size_t j=0;j<ARRAY_SIZE(lens);++j) {
        memset(b,0xa5,sizeof(b)); ssize_t got=a[23].read(&peer[1],a+23,b,lens[j],off);
        if(off>4) { assert(got==-BT_ATT_ERR_INVALID_OFFSET && b[0]==0xa5); }
        else {
            size_t want=4-off; if(want>lens[j]) { want=lens[j]; }
            assert(got==(ssize_t)want && !memcmp(b,"\0\0\0\0",want) && b[want]==0xa5);
        }
    }
    assert(a[23].read(&peer[1],a+23,b,32,UINT16_MAX)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a[6].read(&peer[0],a+6,b,32,0)==sizeof(keyboard.body) && !memcmp(b,&keyboard.body,sizeof(keyboard.body)));
    assert(a[10].read(&peer[0],a+10,b,32,0)==sizeof(consumer.body) && !memcmp(b,&consumer.body,sizeof(consumer.body)));
    assert(a[14].read(&peer[0],a+14,b,32,0)==sizeof(mouse.body) && !memcmp(b,&mouse.body,sizeof(mouse.body)));
    uint8_t led=0x15,control=0x7d;
    assert(a[18].write(&peer[0],a+18,&led,1,0,0)==1 && led_calls==1 && led_value==led && led_profile==0);
    assert(a[18].write(&peer[0],a+18,&led,0,0,0)==-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    assert(a[18].write(&peer[0],a+18,&led,1,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    peer[3].dst.a.val[5]=255;
    assert(a[18].write(&peer[3],a+18,&led,1,0,0)==-BT_ATT_ERR_UNLIKELY && led_calls==1);
    assert(a[21].write(&peer[0],a+21,&control,1,0,0)==1 && ctrl_point==control);
    assert(a[21].write(&peer[0],a+21,&control,1,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    balanced();
}
static void test_ccc_cycles_and_early_return(void) {
    const struct bt_gatt_attr *a=hog_svc.attrs+CCC; uint8_t b[3]={0xff,0xff,0xff};
    assert(write_ccc(0,1,1)==1 && write_ccc(1,1,2)==2);
    assert(!host_requests_notification);
    struct _bt_gatt_ccc *c=a->user_data;
    unsigned int before_cfg=cfg_calls,before_agg=aggregate_calls,before_invalid=invalidations;
    uint32_t old=epoch;
    assert(write_ccc(0,0,2)==2 && write_ccc(0,1,2)==2);
    assert(c->value==1 && cfg_calls==before_cfg+2 && aggregate_calls==before_agg && invalidations==before_invalid+2);
    assert(epoch!=old && a->read(&peer[0],a,b,3,0)==2 && b[0]==1 && b[1]==0 && b[2]==0xff);
    struct bbp_gamepad_report_body body=known();
    assert(send_to(&peer[0],&body,old)==-ESTALE && !sent_count);
    before_cfg=cfg_calls; before_agg=aggregate_calls; before_invalid=invalidations;
    uint32_t peer0_old=epoch;
    assert(write_ccc(1,0,1)==1 && write_ccc(1,1,1)==1);
    assert(cfg_calls==before_cfg+2 && invalidations==before_invalid+2 && aggregate_calls==before_agg);
    assert(send_to(&peer[0],&body,peer0_old)==-ESTALE && !sent_count); /* Global conservative invalidation. */
    /* Counterfactual: aggregate-only handling cannot detect off/on while the
     * other peer stays subscribed. This mutates only host-mock CCC storage. */
    ssize_t (*saved_write)(struct bt_conn *,const struct bt_gatt_attr *,uint16_t)=c->cfg_write;
    c->cfg_write=NULL; old=epoch; before_agg=aggregate_calls;
    assert(write_ccc(0,0,2)==2 && write_ccc(0,1,2)==2 && epoch==old && aggregate_calls==before_agg);
    assert(!bbp_candidate_value(&hog_svc)); c->cfg_write=saved_write;
    assert(bbp_candidate_value(&hog_svc)==hog_svc.attrs+VALUE);
    before_cfg=cfg_calls; before_invalid=invalidations;
    assert(write_ccc(0,1,2)==2 && cfg_calls==before_cfg+1 && invalidations==before_invalid+1);
    before_cfg=cfg_calls; before_invalid=invalidations;
    assert(write_ccc(3,0,1)==1 && cfg_calls==before_cfg && invalidations==before_invalid);
    assert(write_ccc(3,0,2)==2 && cfg_calls==before_cfg && invalidations==before_invalid);
    assert(write_ccc(1,2,2)==-0x13 && invalidations==before_invalid);
    uint8_t val[3]={1,0,0};
    assert(a->write(&peer[0],a,val,0,0,0)==-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    assert(a->write(&peer[0],a,val,3,0,0)==-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    assert(a->write(&peer[0],a,val,2,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(bbp_candidate_ccc_write(NULL,a,1)==-BT_ATT_ERR_UNLIKELY);
    balanced();
}
static int precommit_result[2];
static void try_fresh_precommit(void) {
    struct bbp_gamepad_report_body body=known();
    /* cfg_write invalidated epoch, but old peer CCC still reads ON. Multiple
     * sequential calls demonstrate that there is no global one-PDU guarantee. */
    precommit_result[0]=send_to(&peer[0],&body,epoch);
    precommit_result[1]=send_to(&peer[0],&body,epoch);
}
static void test_precommit_window_is_not_atomic(void) {
    allow_two(); size_t before=sent_count; uint32_t old=epoch;
    precommit_result[0]=precommit_result[1]=-999; precommit_hook=try_fresh_precommit;
    assert(write_ccc(0,0,2)==2 && !precommit_hook);
    assert(precommit_result[0]==0 && precommit_result[1]==0 && sent_count==before+2 &&
           sent[before].conn==&peer[0] && sent[before+1].conn==&peer[0]);
    assert(epoch!=old); struct bbp_gamepad_report_body body=known();
    assert(send_to(&peer[0],&body,epoch)==-EAGAIN && sent_count==before+2);
    /* A late enable changes epoch again, so pre-event captured frames stay stale. */
    assert(write_ccc(0,1,2)==2 && send_to(&peer[0],&body,old)==-ESTALE);
    balanced();
}
static void test_encoder_vectors(void) {
    allow_two();
    const uint8_t neutral[10]={15,0,0,128,128,128,128,0,0,0};
    for(uint8_t hat=0;hat<=8;++hat) {
        struct bbp_gamepad_report_body b=bbp_gamepad_neutral(); b.hat=hat;
        assert(send_to(&peer[0],&b,epoch)==0);
        const struct notification *r=&sent[sent_count-1];
        assert(r->len==10 && r->data[0]==(hat==8 ? 15 : hat));
        assert(!memcmp(r->data+1,neutral+1,9));
    }
    const int8_t samples[]={-127,-1,0,1,127};
    for(unsigned int axis=0;axis<4;++axis) for(size_t i=0;i<ARRAY_SIZE(samples);++i) {
        struct bbp_gamepad_report_body b=bbp_gamepad_neutral();
        if(axis==0) { b.x=samples[i]; } else if(axis==1) { b.y=samples[i]; }
        else if(axis==2) { b.z=samples[i]; } else { b.rz=samples[i]; }
        assert(send_to(&peer[0],&b,epoch)==0);
        const struct notification *r=&sent[sent_count-1];
        for(unsigned int n=0;n<10;++n) {
            uint8_t want=n==3+axis ? (uint8_t)((int)samples[i]+128) : neutral[n];
            assert(r->data[n]==want);
        }
    }
    const uint8_t triggers[]={0,64,65,255};
    for(unsigned int side=0;side<2;++side) for(size_t i=0;i<ARRAY_SIZE(triggers);++i) {
        struct bbp_gamepad_report_body b=bbp_gamepad_neutral();
        if(side==0) { b.lt=triggers[i]; } else { b.rt=triggers[i]; }
        assert(send_to(&peer[0],&b,epoch)==0);
        const struct notification *r=&sent[sent_count-1];
        assert(r->data[1]==(triggers[i]>64 ? (side==0 ? 4 : 8) : 0));
        assert(r->data[7+side]==triggers[i] && r->data[9]==0);
    }
    size_t before=sent_count;
    for(unsigned int axis=0;axis<4;++axis) {
        struct bbp_gamepad_report_body b=bbp_gamepad_neutral();
        if(axis==0) { b.x=INT8_MIN; } else if(axis==1) { b.y=INT8_MIN; }
        else if(axis==2) { b.z=INT8_MIN; } else { b.rz=INT8_MIN; }
        assert(send_to(&peer[0],&b,epoch)==-EINVAL && sent_count==before);
    }
    balanced();
}
static void test_send_peer_and_races(void) {
    allow_two(); struct bbp_gamepad_report_body body=known(), saved=body;
    uint32_t old=epoch; const uint8_t bytes[10]={15,4,64,255,128,128,128,255,0,0};
    assert(send_to(&peer[0],&body,old)==0 && sent_count==1 && sent[0].conn==&peer[0]);
    assert(sent[0].attr==hog_svc.attrs+VALUE && sent[0].len==10 && !memcmp(sent[0].data,bytes,10));
    assert(!memcmp(&body,&saved,sizeof(body)));
    assert(send_to(NULL,&body,epoch)==-ENOTCONN && send_to(&peer[0],NULL,epoch)==-EINVAL);
    assert(send_to(&peer[1],&body,epoch)==-ESTALE);
    assert(send_to(&peer[0],&body,epoch-1)==-ESTALE);
    for(unsigned int side=0;side<2;++side) {
        struct bbp_gamepad_report_body trigger_only=bbp_gamepad_neutral();
        if(side==0) { trigger_only.lt=255; } else { trigger_only.rt=255; }
        assert(!bbp_gamepad_is_neutral(&trigger_only));
        assert(send_to(&peer[1],&trigger_only,epoch)==-ESTALE);
        assert(send_to(&peer[0],&trigger_only,epoch-1)==-ESTALE);
        assert(sent_count==1);
    }
    body.hat=9; assert(send_to(&peer[0],&body,epoch)==-EINVAL); body=saved;
    body.x=INT8_MIN; assert(send_to(&peer[0],&body,epoch)==-EINVAL); body=saved;
    endpoint.transport=ZMK_TRANSPORT_USB; assert(send_to(&peer[0],&body,epoch)==-ESTALE); choose(0);
    no_dst=true; assert(send_to(&peer[0],&body,epoch)==-ESTALE); no_dst=false;
    active=&peer[2]; peer[2].dst=peer[0].dst; /* same profile/address, different conn object */
    assert(send_to(&peer[0],&body,epoch)==-ESTALE); choose(0);
    peer[0].state=BT_CONN_STATE_DISCONNECTED; assert(send_to(&peer[0],&body,epoch)==-ENOTCONN);
    peer[0].state=BT_CONN_STATE_CONNECTED; peer[0].info_error=-EIO;
    assert(send_to(&peer[0],&body,epoch)==-ENOTCONN); peer[0].info_error=0;
    size_t before=sent_count;
    subscription_hook=off_on_primary; subscription_hook_at=subscription_calls+1;
    old=epoch; assert(send_to(&peer[0],&body,old)==-ESTALE && !subscription_hook && sent_count==before);
    active_hook=flip_route; old=epoch;
    assert(send_to(&peer[0],&body,old)==-ESTALE && !active_hook && sent_count==before); choose(0);
    info_hook=disconnect_active; info_hook_at=info_calls+2; old=epoch;
    assert(send_to(&peer[0],&body,old)==-ESTALE && !info_hook && sent_count==before);
    peer[0].state=BT_CONN_STATE_CONNECTED;
    const int errors[]={-EPERM,-ENOMEM,-EAGAIN,-EIO}; unsigned int sec=security_calls;
    for(size_t i=0;i<ARRAY_SIZE(errors);++i) {
        notify_error=errors[i]; size_t prior=sent_count;
        assert(send_to(&peer[0],&body,epoch)==errors[i] && security_calls==sec && sent_count==prior+1);
    }
    notify_error=0;
    before=sent_count; old=epoch; notify_hook=ctx_invalidate;
    assert(send_to(&peer[0],&body,old)==0 && !notify_hook && epoch!=old && sent_count==before+1);
    assert(send_to(&peer[0],&body,old)==-ESTALE); /* one invocation passed final gate */
    balanced();
}
static void test_old_peer_neutral(void) {
    allow_two(); uint32_t old=epoch; struct bt_conn *captured=bt_conn_ref(&peer[0]);
    choose(1); bbp_gamepad_transport_invalidate(); endpoint.transport=ZMK_TRANSPORT_USB;
    struct bbp_gamepad_report_body neutral=bbp_gamepad_neutral();
    const uint8_t bytes[10]={15,0,0,128,128,128,128,0,0,0};
    assert(bbp_gamepad_ble_send(captured,&neutral,old)==0 && sent[0].conn==captured &&
           sent[0].len==10 && !memcmp(sent[0].data,bytes,10));
    assert(write_ccc(0,0,2)==2 && send_to(captured,&neutral,old)==-EAGAIN);
    assert(bt_gatt_is_subscribed(&peer[1],hog_svc.attrs+VALUE,BT_GATT_CCC_NOTIFY));
    assert(write_ccc(0,1,2)==2);
    subscription_hook=off_on_primary; subscription_hook_at=subscription_calls+1;
    assert(send_to(captured,&neutral,old)==-EAGAIN && !subscription_hook);
    /* A same-peer disable AFTER the final gate cannot retract this invocation;
     * this is a mock SDK submission, not proof of delivery. */
    size_t before=sent_count; notify_hook=off_primary;
    assert(send_to(captured,&neutral,old)==0 && !notify_hook && sent_count==before+1 &&
           sent[before].conn==captured);
    assert(send_to(captured,&neutral,old)==-EAGAIN);
    assert(write_ccc(0,1,2)==2);
    subscription_hook=off_primary; subscription_hook_at=subscription_calls+1;
    assert(send_to(captured,&neutral,old)==-EAGAIN && !subscription_hook);
    peer[0].state=BT_CONN_STATE_DISCONNECTED; assert(send_to(captured,&neutral,old)==-ENOTCONN);
    peer[0].state=BT_CONN_STATE_CONNECTED; bt_conn_unref(captured); balanced();
}
static void test_snapshot_stock_chain(void) {
    struct zmk_hid_keyboard_report_body kb=keyboard.body;
    struct zmk_hid_consumer_report_body co=consumer.body;
    struct zmk_hid_mouse_report_body m={.buttons=3,.d_x=12,.d_y=-4,.d_scroll_y=5};
    const struct zmk_hid_mouse_report_body expect={.buttons=3};
    assert(zmk_hog_send_keyboard_report(&kb)==0 && zmk_hog_send_consumer_report(&co)==0);
    send_keyboard_report_callback(&hog_keyboard_work); send_consumer_report_callback(&hog_consumer_work);
    assert(sent_count==2 && sent[0].attr==hog_svc.attrs+5 && sent[1].attr==hog_svc.attrs+9);
    assert(sent[0].len==sizeof(kb) && !memcmp(sent[0].data,&kb,sizeof(kb)) &&
           sent[1].len==sizeof(co) && !memcmp(sent[1].data,&co,sizeof(co)));
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&m,0)==0 && peer[0].refs==2);
    assert(zmk_hog_send_mouse_report(&m)==0 && peer[0].refs==3);
    bbp9981_hog_mouse_work.queued=false; bbp9981_hog_mouse_worker(&bbp9981_hog_mouse_work.work);
    assert(sent_count==3 && sent[2].attr==hog_svc.attrs+13 && sent[2].len==sizeof(expect) &&
           !memcmp(sent[2].data,&expect,sizeof(expect)));
    bbp9981_hog_mouse_work.queued=false; bbp9981_hog_mouse_worker(&bbp9981_hog_mouse_work.work);
    assert(sent_count==4 && sent[3].attr==hog_svc.attrs+13 && !memcmp(sent[3].data,&m,sizeof(m)));
    assert(submit_calls==2 && schedule_calls>0); balanced();
}
int main(int argc,char **argv) {
    setup();
    if(argc==2 && !strcmp(argv[1],"--bad-map")) {
        uint8_t b[16], output[4]={0}; struct bbp_gamepad_report_body body=known();
        assert(!bbp_candidate_map_valid());
        assert(hog_svc.attrs[4].read(&peer[0],hog_svc.attrs+4,b,16,0)==-BT_ATT_ERR_UNLIKELY);
        assert(hog_svc.attrs[VALUE].read(&peer[0],hog_svc.attrs+VALUE,b,16,0)==-BT_ATT_ERR_UNLIKELY);
        assert(hog_svc.attrs[23].write(&peer[0],hog_svc.attrs+23,output,4,0,0)==-BT_ATT_ERR_UNLIKELY);
        assert(send_to(&peer[0],&body,epoch)==-EINVAL && !sent_count); balanced();
        puts("PASS same-length wrong stock map fails closed; no real SDK attestation"); return 0;
    }
    assert(bbp_candidate_map_valid());
    if(argc==2 && !strcmp(argv[1],"--map")) {
        uint8_t map[BBP_CANDIDATE_MAP_SIZE];
        assert(hog_svc.attrs[4].read(&peer[0],hog_svc.attrs+4,map,sizeof(map),0)==sizeof(map));
        return fwrite(map,1,sizeof(map),stdout)==sizeof(map)?0:2;
    }
    if(argc==2 && !strcmp(argv[1],"--pure")) {
        return fwrite(bbp_exp_report_map,1,sizeof(bbp_exp_report_map),stdout)==sizeof(bbp_exp_report_map)?0:3;
    }
    if(argc==2 && !strcmp(argv[1],"--refs")) {
        for(size_t i=0;i<hog_svc.attr_count;++i) if(hog_svc.attrs[i].uuid->val==0x2908) {
            uint8_t ref[2]; assert(hog_svc.attrs[i].read(&peer[0],hog_svc.attrs+i,ref,2,0)==2);
            printf("%u %u %zu\n",ref[0],ref[1],i);
        }
        return 0;
    }
    if(argc!=1) { return 4; }
#define RUN(fn) do { setup(); fn(); puts("PASS " #fn); } while(0)
    RUN(test_structure_and_map); RUN(test_reads_output_and_stock); RUN(test_ccc_cycles_and_early_return);
    RUN(test_precommit_window_is_not_atomic); RUN(test_encoder_vectors); RUN(test_send_peer_and_races);
    RUN(test_old_peer_neutral);
    RUN(test_snapshot_stock_chain);
    puts("LIMIT candidate only; mock CCC/ATT/runtime/scheduler, no linearized cancellation, host ACK, ARM or device evidence");
    return 0;
}
