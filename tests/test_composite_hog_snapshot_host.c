/* SPDX-License-Identifier: MIT
 * HOST ONLY: actual snapshot preinclude -> host composite preinclude -> fixed
 * sdk_v0_3_0_hog.c. No production source or mock fixture is changed. The
 * scheduler, locks, connection, ATT and synchronous notify copy below are
 * deliberately tiny models, NOT real Zephyr or radio/host ACK evidence.
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "host oracle requires enabled assertions"
#endif

#define CONFIG_ZMK_POINTING 1
#define CONFIG_ZMK_POINTING_SMOOTH_SCROLLING 0
#define CONFIG_ZMK_HID_INDICATORS 1
#define CONFIG_ZMK_BLE_THREAD_STACK_SIZE 512
#define CONFIG_ZMK_BLE_THREAD_PRIORITY 5
#define CONFIG_ZMK_BLE_INIT_PRIORITY 50
#ifndef CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE
#define CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE 4
#endif
#ifndef CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE
#define CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE 4
#endif
#ifndef CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE
#define CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE 4
#endif
#define CONFIG_ZMK_LOG_LEVEL 0
/* The runner force-includes the real snapshot first, then the host composite.
 * A temporary compiler prelude declares the existing k_msgq_peek API missing
 * from the frozen mock; neither the production header nor the fixture is edited. */
#ifndef BBP9981_TRACKPAD_HOG_SNAPSHOT_H
#error "real snapshot force-include is required"
#endif
#ifdef BBP9981_HOG_SNAPSHOT_API_ONLY
#error "snapshot implementation, not declarations alone, is required"
#endif
#ifndef zmk_hog_send_mouse_report
#error "composite preinclude must preserve the snapshot mouse rename"
#endif
#ifndef bt_gatt_attr_read
#error "composite preinclude must install the map read wrapper"
#endif
#include "gamepad_ble_mocks/sdk_v0_3_0_hog.c"
#undef bt_gatt_attr_read
#undef BT_GATT_SERVICE_DEFINE
#undef zmk_hog_send_mouse_report
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
#undef zmk_hog_send_keyboard_report
#endif

static uint8_t stock[BBP_COMPOSITE_STOCK_MAP_SIZE];
static struct bt_conn peer[2], *active;
static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static struct zmk_hid_mouse_report mouse;
static struct bbp_gamepad_report_body pad;
static unsigned int ref_calls, unref_calls, lock_depth, led_calls, security_calls;
static unsigned int submit_calls, schedule_calls, raw_reads;
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
static unsigned int diag_keyboard_notes, diag_tx_calls, diag_tx_kind[MD_QUALITY + 1U];
static unsigned int diag_enqueue_success, diag_enqueue_stale;
void bbp_menu_diag_note(uint8_t kind, int32_t a, int32_t b, int32_t c, int32_t d) {
    (void)b; (void)c; (void)d;
    if(kind==MD_KBD) { assert(a==8); ++diag_keyboard_notes; }
}
void bbp_menu_diag_tx(uint8_t kind, uint8_t buttons, int result, unsigned outstanding) {
    (void)buttons; (void)outstanding;
    assert(kind<=MD_QUALITY); ++diag_tx_calls; ++diag_tx_kind[kind];
    if(kind==MD_ENQUEUE && result==0) { ++diag_enqueue_success; }
    if(kind==MD_ENQUEUE && result==-ESTALE) { ++diag_enqueue_stale; }
}
#endif
static uint8_t led_value;
static int led_profile;
static int64_t now_ms;
struct record {
    struct bt_conn *conn;
    const struct bt_gatt_attr *attr;
    uint16_t len;
    uint8_t bytes[32];
};
static struct record records[CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE + 8U];
static size_t record_count;
struct k_work_q k_sys_work_q;

bool bbp_composite_hog_capture(struct bt_conn *conn, struct bbp_gamepad_report_body *out) {
    if (conn != &peer[0]) { return false; }
    *out = pad; return true;
}
struct bt_conn *bt_conn_ref(struct bt_conn *c) {
    assert(c && c->refs >= 1); ++ref_calls; ++c->refs; return c;
}
void bt_conn_unref(struct bt_conn *c) {
    assert(c && c->refs > 1 && !lock_depth); ++unref_calls; --c->refs;
}
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *c) { return c ? &c->dst : NULL; }
int bt_conn_get_info(const struct bt_conn *c, struct bt_conn_info *info) {
    if (!c || !info) { return -EINVAL; }
    if (c->info_error) { return c->info_error; }
    info->state = c->state; return 0;
}
int bt_conn_set_security(struct bt_conn *c, bt_security_t security) {
    assert(c && security == BT_SECURITY_L2 && !lock_depth); ++security_calls; return 0;
}
int zmk_ble_active_profile_index(void) { return active ? active->dst.a.val[5] : -1; }
int zmk_ble_profile_index(const bt_addr_le_t *addr) { assert(addr); return addr->a.val[5]; }
struct bt_conn *zmk_ble_active_profile_conn(void) { return active ? bt_conn_ref(active) : NULL; }
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) { return &keyboard; }
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) { return &consumer; }
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void) { return &mouse; }
void zmk_hid_indicators_process_report(struct zmk_hid_led_report_body *report,
                                       struct zmk_endpoint_instance endpoint) {
    assert(report && endpoint.transport == ZMK_TRANSPORT_BLE);
    ++led_calls; led_value = report->leds; led_profile = endpoint.ble.profile_index;
}
ssize_t bt_gatt_attr_read(struct bt_conn *c, const struct bt_gatt_attr *a, void *buf,
                          uint16_t len, uint16_t off, const void *value, uint16_t size) {
    (void)c; (void)a; ++raw_reads;
    if (off > size) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    size_t count = size - off; if (count > len) { count = len; }
    if (count && (!buf || !value)) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    if (count) { memcpy(buf, (const uint8_t *)value + off, count); }
    return (ssize_t)count;
}
ssize_t bt_gatt_attr_read_service(struct bt_conn *c, const struct bt_gatt_attr *a,
                                  void *buf, uint16_t len, uint16_t off) {
    const struct bt_uuid *uuid = a->user_data;
    return bt_gatt_attr_read(c,a,buf,len,off,&uuid->val,sizeof(uuid->val));
}
ssize_t bt_gatt_attr_read_chrc(struct bt_conn *c, const struct bt_gatt_attr *a,
                               void *buf, uint16_t len, uint16_t off) {
    const struct bt_gatt_chrc *chrc = a->user_data;
    return bt_gatt_attr_read(c,a,buf,len,off,&chrc->properties,1);
}
ssize_t bt_gatt_attr_read_ccc(struct bt_conn *c, const struct bt_gatt_attr *a,
                              void *buf, uint16_t len, uint16_t off) {
    (void)c; (void)a; (void)buf; (void)len; (void)off; return -ENOTSUP;
}
ssize_t bt_gatt_attr_write_ccc(struct bt_conn *c, const struct bt_gatt_attr *a,
                               const void *buf, uint16_t len, uint16_t off, uint8_t flags) {
    (void)c; (void)a; (void)buf; (void)len; (void)off; (void)flags; return -ENOTSUP;
}
bool bt_gatt_is_subscribed(struct bt_conn *c, const struct bt_gatt_attr *a, uint16_t type) {
    (void)c; (void)a; (void)type; return false; /* No CCC behavior is simulated here. */
}
int bt_gatt_notify_cb(struct bt_conn *c, struct bt_gatt_notify_params *p) {
    assert(c && p && p->attr && p->data && p->len <= sizeof(records[0].bytes) && !lock_depth);
    assert(record_count < ARRAY_SIZE(records));
    struct record *r = &records[record_count++];
    *r = (struct record){.conn=c,.attr=p->attr,.len=p->len};
    memcpy(r->bytes,p->data,p->len); /* Synchronous copy only in this mock. */
    return 0;
}
k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(lock && !lock->held); lock->held=true; return lock_depth++;
}
void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock && lock->held && lock_depth==key+1U); lock->held=false; --lock_depth;
}
int64_t k_uptime_get(void) { return now_ms; }
int k_msgq_put(struct k_msgq *q, const void *data, k_timeout_t timeout) {
    assert(q && data && (timeout==K_NO_WAIT || timeout==K_MSEC(100)));
    if (q->used==q->capacity) { return -EAGAIN; }
    memcpy(q->buffer+q->write_pos*q->item_size,data,q->item_size);
    q->write_pos=(q->write_pos+1U)%q->capacity; ++q->used; return 0;
}
int k_msgq_get(struct k_msgq *q, void *data, k_timeout_t timeout) {
    assert(q && data && timeout==K_NO_WAIT);
    if (!q->used) { return -ENOMSG; }
    memcpy(data,q->buffer+q->read_pos*q->item_size,q->item_size);
    q->read_pos=(q->read_pos+1U)%q->capacity; --q->used; return 0;
}
int k_msgq_peek(struct k_msgq *q, void *data) {
    assert(q && data);
    if (!q->used) { return -ENOMSG; }
    memcpy(data,q->buffer+q->read_pos*q->item_size,q->item_size); return 0;
}
unsigned int k_msgq_num_used_get(struct k_msgq *q) { assert(q); return q->used; }
int k_work_submit_to_queue(struct k_work_q *q, struct k_work *work) {
    assert(q==&hog_work_q && q->started && work); ++submit_calls; return 1;
}
int k_work_schedule_for_queue(struct k_work_q *q, struct k_work_delayable *work,
                               k_timeout_t delay) {
    assert(q && work && delay>=0); ++schedule_calls;
    if (!q->started) { return -ENODEV; }
    if (q->plugged) { return -EBUSY; }
    work->queue=q; work->due=now_ms+delay; work->queued=true; return 1;
}
void k_work_queue_start(struct k_work_q *q, void *stack, size_t size, int priority,
                        const struct k_work_queue_config *config) {
    assert(q==&hog_work_q && stack && size && priority==CONFIG_ZMK_BLE_THREAD_PRIORITY);
    assert(config && !strcmp(config->name,"HID Over GATT Send Work")); q->started=true;
}

static void setup(void) {
    assert(!lock_depth && ref_calls==unref_calls);
    assert(!bbp9981_hog_mouse_stats.outstanding && !bbp9981_hog_mouse_fifo.used);
    for(size_t i=0;i<ARRAY_SIZE(peer);++i) {
        peer[i]=(struct bt_conn){.state=BT_CONN_STATE_CONNECTED,.refs=1};
        peer[i].dst.type=1; peer[i].dst.a.val[5]=(uint8_t)i;
    }
    active=&peer[0];
    keyboard.body=(struct zmk_hid_keyboard_report_body){.modifiers=8,.keys={4,5}};
    consumer.body=(struct zmk_hid_consumer_report_body){.keys={0x123}};
    mouse.body=(struct zmk_hid_mouse_report_body){.buttons=2,.d_x=-12,.d_y=24};
    pad=bbp_gamepad_neutral(); pad.buttons_lo=1; pad.x=127; pad.lt=255;
    memset(&bbp9981_hog_mouse_stats,0,sizeof(bbp9981_hog_mouse_stats));
    memset(&bbp9981_hog_mouse_head,0,sizeof(bbp9981_hog_mouse_head));
    bbp9981_hog_mouse_fifo.read_pos=bbp9981_hog_mouse_fifo.write_pos=0;
    bbp9981_hog_mouse_work.queued=false; bbp9981_hog_mouse_dispatch.queued=false;
    hog_work_q=(struct k_work_q){0}; k_sys_work_q=(struct k_work_q){.started=true};
    ref_calls=unref_calls=led_calls=security_calls=0;
    submit_calls=schedule_calls=raw_reads=0; record_count=0; now_ms=0;
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
    diag_keyboard_notes=diag_tx_calls=0; diag_enqueue_success=diag_enqueue_stale=0;
    memset(diag_tx_kind,0,sizeof(diag_tx_kind));
#endif
    led_value=0; led_profile=-1; ctrl_point=0;
    assert(gamepad_mock_hog_init()==0);
}
static void balanced(void) {
    struct bbp9981_hog_mouse_stats s;
    assert(bbp9981_ble_hid_mouse_report_stats_get(&s)==0);
    assert(!lock_depth && ref_calls==unref_calls && peer[0].refs==1 && peer[1].refs==1);
    assert(!s.outstanding && !s.pending_head && !s.sending && !bbp9981_hog_mouse_fifo.used);
}
static void run_snapshot_worker(void) {
    /* Explicit direct invocation: this is NOT a Zephyr workqueue scheduler. */
    bbp9981_hog_mouse_work.queued=false;
    bbp9981_hog_mouse_worker(&bbp9981_hog_mouse_work.work);
}
static void check_record(size_t n, struct bt_conn *c, size_t index,
                         const void *data, size_t size) {
    assert(n<record_count && records[n].conn==c && records[n].attr==&hog_svc.attrs[index]);
    assert(records[n].len==size && !memcmp(records[n].bytes,data,size));
}
static void test_table(void) {
    const struct bt_gatt_attr *a=hog_svc.attrs;
    assert(a==attr_hog_svc && hog_svc.attr_count==29);
    size_t primaries=0;
    for(size_t i=0;i<hog_svc.attr_count;++i) { if(a[i].uuid->val==0x2800) { ++primaries; } }
    assert(primaries==1 && ((struct bt_uuid *)a[0].user_data)->val==0x1812);
    const size_t decls[]={5,9,13};
    const bt_gatt_attr_read_func_t reads[]={read_hids_input_report,read_hids_consumer_input_report,
                                            read_hids_mouse_input_report};
    for(size_t i=0;i<ARRAY_SIZE(decls);++i) {
        size_t d=decls[i]; const struct bt_gatt_chrc *chrc=a[d].user_data;
        assert(a[d].uuid->val==0x2803 && chrc->uuid->val==0x2a4d &&
               chrc->properties==(BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY));
        assert(a[d+1].uuid->val==0x2a4d && a[d+1].perm==BT_GATT_PERM_READ_ENCRYPT && a[d+1].read==reads[i]);
        assert(a[d+2].uuid->val==0x2902 && ((struct _bt_gatt_ccc *)a[d+2].user_data)->cfg_changed==input_ccc_changed);
    }
    assert(a[4].read==read_hids_report_map && a[18].write==write_hids_leds_report);
    assert(a[21].write==write_ctrl_point && a[21].user_data==&ctrl_point);
    assert(a[23].read==bbp_composite_hog_read_output && a[23].write==bbp_composite_hog_write_output);
    assert(a[26].read==bbp_composite_hog_read_input && a[27].uuid->val==0x2902);
    assert(a[hog_svc.attr_count-3].read==bbp_composite_hog_read_input);
    const size_t indexes[]={8,12,16,19,24,28};
    const uint8_t refs[][2]={{1,1},{2,1},{3,1},{1,2},{5,2},{4,1}};
    for(size_t i=0;i<ARRAY_SIZE(indexes);++i) {
        const struct bt_gatt_attr *r=a+indexes[i]; uint8_t out[3]={0,0,0xa5};
        assert(r->uuid->val==0x2908 && r->perm==BT_GATT_PERM_READ_ENCRYPT && r->read);
        assert(r->read(&peer[0],r,out,2,0)==2 && !memcmp(out,refs[i],2) && out[2]==0xa5);
    }
    assert(a[27].user_data!=a[15].user_data);
    assert(zmk_hog_send_mouse_report != bbp9981_stock_hog_send_mouse_report);
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
    assert(zmk_hog_send_keyboard_report != bbp9981_stock_hog_send_keyboard_report);
#endif
    balanced();
}
static void test_snapshot_and_composite_interleaved(void) {
    const struct bt_gatt_attr *a=hog_svc.attrs;
    struct zmk_hid_mouse_report_body down={.buttons=3,.d_x=100,.d_y=-50,.d_scroll_y=6,.d_scroll_x=-7};
    struct zmk_hid_mouse_report_body m1={.buttons=3,.d_x=2,.d_y=-4};
    struct zmk_hid_mouse_report_body m2={.buttons=3,.d_x=3,.d_y=-6};
    struct zmk_hid_mouse_report_body up={.buttons=0,.d_x=-22,.d_y=11,.d_scroll_y=4};
    const struct zmk_hid_mouse_report_body down_original=down, m1_original=m1, m2_original=m2, up_original=up;
    const struct zmk_hid_mouse_report_body down_wire={.buttons=3}, up_wire={.buttons=0};
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(NULL,0)==-EINVAL);
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&down,1)==-ESTALE);
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&down,0)==0);
    assert(zmk_hog_send_mouse_report(&m1)==0 && zmk_hog_send_mouse_report(&m2)==0);
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&up,0)==0);
    assert(!memcmp(&down,&down_original,sizeof(down)) && !memcmp(&up,&up_original,sizeof(up)));
    assert(!memcmp(&m1,&m1_original,sizeof(m1)) && !memcmp(&m2,&m2_original,sizeof(m2)));
    assert(bbp9981_hog_mouse_stats.accepted==4 && bbp9981_hog_mouse_stats.outstanding==4);
    assert(bbp9981_hog_mouse_fifo.used==4 && peer[0].refs==5 && !record_count);

    /* Exercise the new report and old controls while four snapshots/movements
     * are still queued. These are sequential test calls, not a concurrency test. */
    const uint8_t expected_pad[10]={15,4,64,255,128,128,128,255,0,0};
    const uint8_t neutral[10]={15,0,0,128,128,128,128,0,0,0};
    uint8_t buffer[32], output[4]={1,2,3,4}, led=0x16, ctrl=0x7d;
    assert(a[26].read(&peer[0],a+26,buffer,32,0)==10 && !memcmp(buffer,expected_pad,10));
    assert(a[26].read(&peer[1],a+26,buffer,32,0)==10 && !memcmp(buffer,neutral,10));
    assert(a[23].write(&peer[0],a+23,output,4,0,0)==4);
    assert(a[18].write(&peer[0],a+18,&led,1,0,0)==1 && led_calls==1 && led_value==led && led_profile==0);
    assert(a[21].write(&peer[0],a+21,&ctrl,1,0,0)==1 && ctrl_point==ctrl);
    assert(a[4].read(&peer[0],a+4,buffer,16,190)==16);
    assert(!memcmp(buffer,stock+190,6) && buffer[6]==0x05 && buffer[13]==4);
    assert(bbp9981_hog_mouse_stats.outstanding==4 && bbp9981_hog_mouse_fifo.used==4);

    struct zmk_hid_keyboard_report_body kb=keyboard.body;
    struct zmk_hid_consumer_report_body co=consumer.body;
    assert(zmk_hog_send_keyboard_report(&kb)==0 && zmk_hog_send_consumer_report(&co)==0);
    send_keyboard_report_callback(&hog_keyboard_work); send_consumer_report_callback(&hog_consumer_work);
    check_record(0,&peer[0],5,&keyboard.body,sizeof(keyboard.body));
    check_record(1,&peer[0],9,&consumer.body,sizeof(consumer.body));
    memset(&down,0xcc,sizeof(down)); memset(&m1,0xcc,sizeof(m1));
    memset(&m2,0xcc,sizeof(m2)); memset(&up,0xcc,sizeof(up));
    run_snapshot_worker();
    check_record(2,&peer[0],13,&down_wire,sizeof(down_wire));
    run_snapshot_worker();
#ifdef CONFIG_A320_CURSOR_STEADY
    const struct zmk_hid_mouse_report_body merged={.buttons=3,.d_x=5,.d_y=-10};
    check_record(3,&peer[0],13,&merged,sizeof(merged));
    run_snapshot_worker(); check_record(4,&peer[0],13,&up_wire,sizeof(up_wire));
    assert(record_count==5 && bbp9981_hog_mouse_stats.notified==3 && bbp9981_hog_mouse_stats.coalesced==1);
#else
    check_record(3,&peer[0],13,&m1_original,sizeof(m1_original));
    run_snapshot_worker(); check_record(4,&peer[0],13,&m2_original,sizeof(m2_original));
    run_snapshot_worker(); check_record(5,&peer[0],13,&up_wire,sizeof(up_wire));
    assert(record_count==6 && bbp9981_hog_mouse_stats.notified==4);
#endif
    assert(submit_calls==2 && schedule_calls>0 && !security_calls);
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
    /* First diag-on test run incorrectly expected four enqueue diagnostics and
     * failed: the wrong-profile snapshot emits a fifth, with -ESTALE. Corrected
     * the oracle to four successes plus that one rejection; no source was fixed. */
    assert(diag_keyboard_notes==2 && diag_tx_kind[MD_ENQUEUE]==5);
    assert(diag_enqueue_success==4 && diag_enqueue_stale==1);
    assert(diag_tx_kind[MD_BEGIN]==bbp9981_hog_mouse_stats.notified &&
           diag_tx_kind[MD_NOTIFY]==bbp9981_hog_mouse_stats.notified && diag_tx_calls>0);
#endif
    balanced();
}
static void test_captured_connection(void) {
    struct zmk_hid_mouse_report_body body={.buttons=1,.d_x=33,.d_y=-22};
    struct zmk_hid_mouse_report_body wire={.buttons=1};
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&body,0)==0);
    active=&peer[1]; body.buttons=0;
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&body,1)==0);
    active=&peer[0];
    assert(peer[0].refs==2 && peer[1].refs==2);
    run_snapshot_worker(); check_record(0,&peer[0],13,&wire,sizeof(wire));
    wire.buttons=0;
    run_snapshot_worker(); check_record(1,&peer[1],13,&wire,sizeof(wire));
    assert(record_count==2 && bbp9981_hog_mouse_stats.notified==2);
    balanced(); /* Captured mock connections, not epoch/ABA or real RTOS proof. */
}
static void test_snapshot_fifo_capacity(void) {
    const unsigned int capacity=CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE;
    for(unsigned int i=0;i<capacity;++i) {
        struct zmk_hid_mouse_report_body source={.buttons=(uint8_t)(i&1U),.d_x=42,.d_y=-22};
        assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&source,0)==0);
    }
    struct zmk_hid_mouse_report_body extra={.buttons=2,.d_x=10};
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&extra,0)==-EAGAIN);
    assert(bbp9981_hog_mouse_stats.accepted==capacity && bbp9981_hog_mouse_stats.outstanding==capacity);
    assert(bbp9981_hog_mouse_stats.queue_full==1 && bbp9981_hog_mouse_fifo.used==capacity);
    assert(peer[0].refs==(int)capacity+1 && !record_count);
    for(unsigned int i=0;i<capacity;++i) {
        struct zmk_hid_mouse_report_body expected={.buttons=(uint8_t)(i&1U)};
        run_snapshot_worker(); check_record(i,&peer[0],13,&expected,sizeof(expected));
    }
    assert(record_count==capacity && bbp9981_hog_mouse_stats.notified==capacity);
#ifdef CONFIG_A320_CURSOR_STEADY
    assert(bbp9981_hog_mouse_stats.coalesced==0); /* snapshots are barriers */
#endif
    balanced();
}
int main(int argc, char **argv) {
    if(fread(stock,1,sizeof(stock),stdin)!=sizeof(stock) || getchar()!=EOF) { return 2; }
    setup(); assert(bbp_composite_hog_load_stock(stock,sizeof(stock))==0);
    if(argc==2 && !strcmp(argv[1],"--map")) {
        uint8_t map[BBP_COMPOSITE_MAP_SIZE];
        assert(hog_svc.attrs[4].read(&peer[0],hog_svc.attrs+4,map,sizeof(map),0)==sizeof(map));
        return fwrite(map,1,sizeof(map),stdout)==sizeof(map)?0:3;
    }
    if(argc==2 && !strcmp(argv[1],"--pure")) {
        return fwrite(bbp_exp_report_map,1,sizeof(bbp_exp_report_map),stdout)==sizeof(bbp_exp_report_map)?0:5;
    }
    if(argc==2 && !strcmp(argv[1],"--refs")) {
        for(size_t i=0;i<hog_svc.attr_count;++i) if(hog_svc.attrs[i].uuid->val==0x2908) {
            uint8_t ref[2]; assert(hog_svc.attrs[i].read(&peer[0],hog_svc.attrs+i,ref,2,0)==2);
            printf("%u %u %zu\n",ref[0],ref[1],i);
        }
        return 0;
    }
    if(argc!=1) { return 4; }
    test_table(); puts("PASS actual snapshot preinclude + composite + pinned HOG: one 29-attribute service and six attached refs");
    setup(); test_snapshot_and_composite_interleaved();
    puts("PASS stock keyboard/consumer, button snapshots and normal mouse via actual snapshot FIFO while composite values are accessed");
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
    puts("PASS diag-ON keyboard wrapper and mock diagnostics: four accepted enqueue plus one ESTALE; begin/notify counts");
#endif
    setup(); test_captured_connection();
    puts("PASS sequential mock captured-connection switch; NOT Zephyr scheduling, async races, epoch or OS/radio evidence");
    setup(); test_snapshot_fifo_capacity();
    printf("PASS snapshot FIFO reaches capacity %u and rejects one more; mock notifications only\n",
           (unsigned int)CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE);
    return 0;
}
