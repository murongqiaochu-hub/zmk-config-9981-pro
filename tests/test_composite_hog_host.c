/* SPDX-License-Identifier: MIT
 * One actual pinned HOG translation unit + pinned GATT macro mock. The ONLY
 * service is that fixture's hog_svc, augmented by the host-only preinclude.
 * Deterministic CCC/ATT/notification stand-ins below are NOT Zephyr behavior.
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
#define CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_LOG_LEVEL 0
#include "../experiments/gamepad_protocol/composite_hog_host.h"
#include "gamepad_ble_mocks/sdk_v0_3_0_hog.c"
#undef bt_gatt_attr_read
#undef BT_GATT_SERVICE_DEFINE

#define ATTR_MAP 4U
#define ATTR_LED 18U
#define ATTR_CTRL 21U
#define ATTR_OUT 23U
#define ATTR_IN 26U
#define ATTR_CCC 27U
static uint8_t stock[BBP_COMPOSITE_STOCK_MAP_SIZE];
static struct bt_conn peer[3];
static struct bt_conn *active;
static struct bbp_gamepad_report_body reports[2];
static bool available[2];
static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static struct zmk_hid_mouse_report mouse;
static unsigned int raw_reads, led_calls, security_calls, submit_calls;
static uint8_t last_led;
static int last_led_profile;
static int notify_error;
struct notification { struct bt_conn *conn; const struct bt_gatt_attr *attr; uint16_t len; uint8_t bytes[32]; };
static struct notification sent[16];
static size_t sent_count;
struct k_work_q k_sys_work_q;

bool bbp_composite_hog_capture(struct bt_conn *conn, struct bbp_gamepad_report_body *out) {
    for (size_t i = 0; i < 2; ++i) {
        if (conn == &peer[i]) { *out = reports[i]; return available[i]; }
    }
    return false;
}
struct bt_conn *bt_conn_ref(struct bt_conn *c) { assert(c && c->refs > 0); ++c->refs; return c; }
void bt_conn_unref(struct bt_conn *c) { assert(c && c->refs > 1); --c->refs; }
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *c) { assert(c); return &c->dst; }
int bt_conn_get_info(const struct bt_conn *c, struct bt_conn_info *info) {
    if (!c || !info) { return -EINVAL; }
    if (c->info_error) { return c->info_error; }
    info->state = c->state; return 0;
}
int bt_conn_set_security(struct bt_conn *c, bt_security_t security) {
    assert(c && security == BT_SECURITY_L2); ++security_calls; return 0;
}
int zmk_ble_profile_index(const bt_addr_le_t *addr) {
    assert(addr); return addr->a.val[5] == 255U ? -1 : addr->a.val[5];
}
int zmk_ble_active_profile_index(void) { return active ? active->dst.a.val[5] : -1; }
struct bt_conn *zmk_ble_active_profile_conn(void) { return active ? bt_conn_ref(active) : NULL; }
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) { return &keyboard; }
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) { return &consumer; }
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void) { return &mouse; }
void zmk_hid_indicators_process_report(struct zmk_hid_led_report_body *report,
                                       struct zmk_endpoint_instance endpoint) {
    assert(report && endpoint.transport == ZMK_TRANSPORT_BLE);
    ++led_calls; last_led = report->leds; last_led_profile = endpoint.ble.profile_index;
}
ssize_t bt_gatt_attr_read(struct bt_conn *c, const struct bt_gatt_attr *a, void *buf,
                          uint16_t len, uint16_t off, const void *value, uint16_t size) {
    (void)c; (void)a; ++raw_reads;
    if (off > size) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    size_t n = size - off;
    if (n > len) { n = len; }
    if (n && (!buf || !value)) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    if (n) { memcpy(buf, (const uint8_t *)value + off, n); }
    return (ssize_t)n;
}
ssize_t bt_gatt_attr_read_service(struct bt_conn *c, const struct bt_gatt_attr *a, void *buf,
                                  uint16_t len, uint16_t off) {
    const struct bt_uuid *uuid = a->user_data;
    return bt_gatt_attr_read(c, a, buf, len, off, &uuid->val, sizeof(uuid->val));
}
ssize_t bt_gatt_attr_read_chrc(struct bt_conn *c, const struct bt_gatt_attr *a, void *buf,
                               uint16_t len, uint16_t off) {
    const struct bt_gatt_chrc *chrc = a->user_data;
    return bt_gatt_attr_read(c, a, buf, len, off, &chrc->properties, 1U);
}

/* Deliberately small test model: slots keyed by full address, independent for
 * each CCC object. id=1 marks an occupied slot, not a Zephyr settings identity.
 * Aggregate value is an OR; neither it nor cfg_changed identifies a peer. No
 * security, settings persistence, connection lifetime or real ATT is modeled. */
static bool same_addr(const bt_addr_le_t *a, const bt_addr_le_t *b) {
    return a->type == b->type && !memcmp(a->a.val, b->a.val, sizeof(a->a.val));
}
static struct bt_gatt_ccc_cfg *mock_slot(struct _bt_gatt_ccc *ccc, const struct bt_conn *c,
                                         bool create) {
    if (!ccc || !c) { return NULL; }
    struct bt_gatt_ccc_cfg *free_slot = NULL;
    for (size_t i = 0; i < BT_GATT_CCC_MAX; ++i) {
        struct bt_gatt_ccc_cfg *slot = &ccc->cfg[i];
        if (slot->id && same_addr(&slot->peer, &c->dst)) { return slot; }
        if (!slot->id && !free_slot) { free_slot = slot; }
    }
    if (create && free_slot) { free_slot->id = 1U; free_slot->peer = c->dst; return free_slot; }
    return NULL;
}
ssize_t bt_gatt_attr_read_ccc(struct bt_conn *c, const struct bt_gatt_attr *a, void *buf,
                              uint16_t len, uint16_t off) {
    if (!c || !a || !a->user_data) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    struct bt_gatt_ccc_cfg *slot = mock_slot(a->user_data, c, false);
    uint16_t value = slot ? slot->value : 0;
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return bt_gatt_attr_read(c, a, buf, len, off, bytes, sizeof(bytes));
}
ssize_t bt_gatt_attr_write_ccc(struct bt_conn *c, const struct bt_gatt_attr *a, const void *buf,
                               uint16_t len, uint16_t off, uint8_t flags) {
    if (flags & (uint8_t)~0x02U) { return BT_GATT_ERR(0x06); }
    if (off) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    if (len != 2) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN); }
    if (!c || !a || !a->user_data || !buf) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    const uint8_t *bytes = buf;
    uint16_t value = (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
    if (value > BT_GATT_CCC_NOTIFY) { return BT_GATT_ERR(0x13); }
    struct _bt_gatt_ccc *ccc = a->user_data;
    struct bt_gatt_ccc_cfg *slot = mock_slot(ccc, c, true);
    if (!slot) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    uint16_t old = ccc->value;
    slot->value = value;
    ccc->value = 0;
    for (size_t i = 0; i < BT_GATT_CCC_MAX; ++i) { ccc->value |= ccc->cfg[i].value; }
    if (old != ccc->value && ccc->cfg_changed) { ccc->cfg_changed(a, ccc->value); }
    return len;
}
bool bt_gatt_is_subscribed(struct bt_conn *c, const struct bt_gatt_attr *value, uint16_t type) {
    if (!c || !value || type != BT_GATT_CCC_NOTIFY) { return false; }
    for (size_t i = 0; i + 1U < hog_svc.attr_count; ++i) {
        if (&hog_svc.attrs[i] == value && hog_svc.attrs[i+1U].uuid->val == 0x2902) {
            struct bt_gatt_ccc_cfg *slot = mock_slot(hog_svc.attrs[i+1U].user_data, c, false);
            return slot && (slot->value & type);
        }
    }
    return false;
}
int bt_gatt_notify_cb(struct bt_conn *c, struct bt_gatt_notify_params *p) {
    assert(c && p && p->attr && p->data && p->len <= sizeof(sent[0].bytes));
    assert(sent_count < ARRAY_SIZE(sent));
    struct notification *out = &sent[sent_count++];
    *out = (struct notification){.conn=c,.attr=p->attr,.len=p->len};
    memcpy(out->bytes, p->data, p->len); /* SYNCHRONOUS COPY in this mock only. */
    return notify_error;
}
int k_msgq_put(struct k_msgq *q, const void *data, k_timeout_t timeout) {
    assert(timeout == K_MSEC(100));
    if (q->used == q->capacity) { return -EAGAIN; }
    memcpy(q->buffer + q->write_pos*q->item_size, data, q->item_size);
    q->write_pos = (q->write_pos+1U)%q->capacity; ++q->used; return 0;
}
int k_msgq_get(struct k_msgq *q, void *data, k_timeout_t timeout) {
    assert(timeout == K_NO_WAIT);
    if (!q->used) { return -ENOMSG; }
    memcpy(data, q->buffer + q->read_pos*q->item_size, q->item_size);
    q->read_pos = (q->read_pos+1U)%q->capacity; --q->used; return 0;
}
int k_work_submit_to_queue(struct k_work_q *q, struct k_work *work) {
    assert(q == &hog_work_q && q->started && work); ++submit_calls; return 1;
}
void k_work_queue_start(struct k_work_q *q, void *stack, size_t size, int priority,
                        const struct k_work_queue_config *config) {
    assert(q == &hog_work_q && stack && size && priority == CONFIG_ZMK_BLE_THREAD_PRIORITY);
    assert(config && !strcmp(config->name, "HID Over GATT Send Work")); q->started = true;
}

static void setup(void) {
    for (size_t i=0; i<ARRAY_SIZE(peer); ++i) {
        peer[i] = (struct bt_conn){.state=BT_CONN_STATE_CONNECTED,.refs=1};
        peer[i].dst.type = 1;
        peer[i].dst.a.val[0] = 0x45; /* Same first address byte must NOT alias. */
        peer[i].dst.a.val[5] = (uint8_t)(i+1U);
    }
    active = &peer[0];
    keyboard.body = (struct zmk_hid_keyboard_report_body){.modifiers=8,.keys={4,5}};
    consumer.body = (struct zmk_hid_consumer_report_body){.keys={0x123}};
    mouse.body = (struct zmk_hid_mouse_report_body){.buttons=3,.d_x=-12,.d_y=24,
                                                   .d_scroll_y=-2,.d_scroll_x=1};
    for (size_t i=0; i<2; ++i) { reports[i] = bbp_gamepad_neutral(); available[i] = true; }
    reports[0].buttons_lo = 1; reports[0].x = 127; reports[0].lt = 255;
    reports[1].hat = 2; reports[1].x = -127; reports[1].y = 127;
    reports[1].rz = -73; reports[1].rt = 255;
    for (size_t i=0; i<hog_svc.attr_count; ++i) {
        if (hog_svc.attrs[i].uuid->val == 0x2902) {
            struct _bt_gatt_ccc *ccc = hog_svc.attrs[i].user_data;
            memset(ccc->cfg, 0, sizeof(ccc->cfg)); ccc->value = 0;
        }
    }
    host_requests_notification = false;
    ctrl_point = 0;
    raw_reads = led_calls = security_calls = submit_calls = 0;
    last_led = 0; last_led_profile = -1; sent_count = 0; notify_error = 0;
    hog_work_q = (struct k_work_q){0};
}
static void chrc(size_t i, uint16_t uuid, uint8_t props, uint16_t perm,
                 bt_gatt_attr_read_func_t rd, bt_gatt_attr_write_func_t wr) {
    const struct bt_gatt_attr *a = hog_svc.attrs;
    assert(a[i].uuid->val == 0x2803 && a[i].perm == BT_GATT_PERM_READ &&
           a[i].read == bt_gatt_attr_read_chrc && !a[i].write && a[i].user_data);
    const struct bt_gatt_chrc *d = a[i].user_data;
    assert(d->uuid->val == uuid && d->properties == props && d->value_handle == 0);
    assert(a[i+1].uuid->val == uuid && a[i+1].perm == perm && a[i+1].read == rd &&
           a[i+1].write == wr && !a[i+1].handle);
}
static void ccc(size_t i, void (*changed)(const struct bt_gatt_attr *, uint16_t)) {
    const struct bt_gatt_attr *a = hog_svc.attrs+i;
    assert(a->uuid->val == 0x2902 && a->perm == (BT_GATT_PERM_READ_ENCRYPT|BT_GATT_PERM_WRITE_ENCRYPT));
    assert(a->read == bt_gatt_attr_read_ccc && a->write == bt_gatt_attr_write_ccc);
    const struct _bt_gatt_ccc *storage = a->user_data;
    assert(storage && storage->cfg_changed == changed && !storage->cfg_write && !storage->cfg_match);
}
static void ref(size_t i, uint8_t id, uint8_t type, bt_gatt_attr_read_func_t callback,
                const void *identity) {
    const struct bt_gatt_attr *a = hog_svc.attrs+i;
    assert(a->uuid->val == 0x2908 && a->perm == BT_GATT_PERM_READ_ENCRYPT &&
           a->read == callback && !a->write && a->user_data == identity);
    uint8_t out[4];
    for (uint16_t off=0; off<=3; ++off) {
        memset(out,0xa5,sizeof(out));
        ssize_t got=a->read(&peer[0],a,out,sizeof(out),off);
        if (off>2) { assert(got == -BT_ATT_ERR_INVALID_OFFSET && out[0]==0xa5); }
        else {
            const uint8_t expected[2]={id,type}; size_t n=2U-off;
            assert(got==(ssize_t)n && !memcmp(out,expected+off,n) && out[n]==0xa5);
        }
    }
    memset(out,0xa5,sizeof(out));
    assert(a->read(&peer[0],a,out,sizeof(out),UINT16_MAX)==-BT_ATT_ERR_INVALID_OFFSET && out[0]==0xa5);
}
static void test_structure(void) {
    const struct bt_gatt_attr *a=hog_svc.attrs;
    const uint8_t in=BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY;
    const uint8_t out=BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE|BT_GATT_CHRC_WRITE_WITHOUT_RESP;
    const uint16_t rw=BT_GATT_PERM_READ_ENCRYPT|BT_GATT_PERM_WRITE_ENCRYPT;
    assert(hog_svc.attrs==attr_hog_svc && hog_svc.attr_count==29U);
    size_t primary_count=0;
    for(size_t i=0;i<hog_svc.attr_count;++i) { if(a[i].uuid->val==0x2800) { ++primary_count; } }
    assert(primary_count==1U);
    assert(a[0].uuid->val==0x2800 && a[0].perm==BT_GATT_PERM_READ &&
           a[0].read==bt_gatt_attr_read_service && !a[0].write &&
           ((const struct bt_uuid *)a[0].user_data)->val==0x1812);
    chrc(1,0x2a4a,BT_GATT_CHRC_READ,BT_GATT_PERM_READ,read_hids_info,NULL);
    assert(a[2].user_data==&info);
    chrc(3,0x2a4b,BT_GATT_CHRC_READ,BT_GATT_PERM_READ_ENCRYPT,read_hids_report_map,NULL);
    chrc(5,0x2a4d,in,BT_GATT_PERM_READ_ENCRYPT,read_hids_input_report,NULL);
    ccc(7,input_ccc_changed); ref(8,1,1,read_hids_report_ref,&input);
    chrc(9,0x2a4d,in,BT_GATT_PERM_READ_ENCRYPT,read_hids_consumer_input_report,NULL);
    ccc(11,input_ccc_changed); ref(12,2,1,read_hids_report_ref,&consumer_input);
    chrc(13,0x2a4d,in,BT_GATT_PERM_READ_ENCRYPT,read_hids_mouse_input_report,NULL);
    ccc(15,input_ccc_changed); ref(16,3,1,read_hids_report_ref,&mouse_input);
    chrc(17,0x2a4d,out,rw,NULL,write_hids_leds_report);
    ref(19,1,2,read_hids_report_ref,&led_indicators);
    chrc(20,0x2a4c,BT_GATT_CHRC_WRITE_WITHOUT_RESP,BT_GATT_PERM_WRITE,NULL,write_ctrl_point);
    assert(a[ATTR_CTRL].user_data==&ctrl_point);
    chrc(22,0x2a4d,out,rw,bbp_composite_hog_read_output,bbp_composite_hog_write_output);
    ref(24,5,2,bbp_composite_hog_read_ref,bbp_composite_output_ref);
    chrc(25,0x2a4d,in,BT_GATT_PERM_READ_ENCRYPT,bbp_composite_hog_read_input,NULL);
    ccc(27,bbp_composite_hog_ccc_changed); ref(28,4,1,bbp_composite_hog_read_ref,bbp_composite_input_ref);
    assert(&a[hog_svc.attr_count-3]==&a[ATTR_IN] && &a[hog_svc.attr_count-2]==&a[ATTR_CCC]);
    const size_t indices[]={7,11,15,27};
    for (size_t i=0;i<ARRAY_SIZE(indices);++i) for(size_t j=i+1;j<ARRAY_SIZE(indices);++j)
        assert(a[indices[i]].user_data!=a[indices[j]].user_data);
}

/* Validate references by actually walking declarations, values and adjacent
 * descriptors, not by trusting a separately constructed list of constants. */
static bool valid_refs(const struct bt_gatt_service_static *svc) {
    const uint8_t expected[][2]={{1,1},{2,1},{3,1},{1,2},{5,2},{4,1}};
    bool found[ARRAY_SIZE(expected)]={0}; size_t count=0;
    if (!svc || !svc->attrs) { return false; }
    for (size_t i=0;i<svc->attr_count;++i) {
        const struct bt_gatt_attr *a=&svc->attrs[i];
        if (a->uuid->val != 0x2803) { continue; }
        const struct bt_gatt_chrc *d=a->user_data;
        if (!d || d->uuid->val!=0x2a4d) { continue; }
        bool input=(d->properties & BT_GATT_CHRC_NOTIFY)!=0;
        size_t r=i+(input?3U:2U);
        if (r>=svc->attr_count || svc->attrs[i+1].uuid->val!=0x2a4d ||
            (input && svc->attrs[i+2].uuid->val!=0x2902)) { return false; }
        const struct bt_gatt_attr *rattr=&svc->attrs[r]; uint8_t data[2];
        if (rattr->uuid->val!=0x2908 || !rattr->read ||
            rattr->read(&peer[0],rattr,data,2,0)!=2 || data[1]!=(input?1:2) ||
            count>=ARRAY_SIZE(expected) || memcmp(data,expected[count],2)!=0) { return false; }
        size_t match=ARRAY_SIZE(expected);
        for(size_t j=0;j<ARRAY_SIZE(expected);++j)
            if (data[0]==expected[j][0] && data[1]==expected[j][1]) { match=j; break; }
        if (match==ARRAY_SIZE(expected) || found[match]) { return false; }
        found[match]=true; ++count;
    }
    return count==ARRAY_SIZE(expected);
}
static void test_bad_refs(void) {
    assert(valid_refs(&hog_svc) && !valid_refs(NULL));
    struct bt_gatt_attr copy[29];
    struct bt_gatt_service_static bad={.attrs=copy,.attr_count=ARRAY_SIZE(copy)};
    uint8_t wrong[][2]={{3,1},{4,2},{5,1},{6,1},{1,1}};
    for(size_t i=0;i<ARRAY_SIZE(wrong);++i) {
        memcpy(copy,hog_svc.attrs,sizeof(copy)); copy[28].user_data=wrong[i];
        assert(!valid_refs(&bad));
    }
    memcpy(copy,hog_svc.attrs,sizeof(copy)); copy[24].user_data=wrong[2]; assert(!valid_refs(&bad));
    /* Swaps preserve the exact same SET of references but attach them to the
     * wrong report values: neither a set-only check nor a map parser catches it. */
    memcpy(copy,hog_svc.attrs,sizeof(copy)); copy[8].user_data=&consumer_input; copy[12].user_data=&input;
    assert(!valid_refs(&bad));
    memcpy(copy,hog_svc.attrs,sizeof(copy)); copy[8].user_data=(void *)bbp_composite_input_ref; copy[28].user_data=&input;
    assert(!valid_refs(&bad));
    memcpy(copy,hog_svc.attrs,sizeof(copy)); copy[19].user_data=(void *)bbp_composite_output_ref; copy[24].user_data=&led_indicators;
    assert(!valid_refs(&bad));
    memcpy(copy,hog_svc.attrs,sizeof(copy)); copy[19].uuid=BT_UUID_DECLARE_16(0x2901); assert(!valid_refs(&bad));
    memcpy(copy,hog_svc.attrs,sizeof(copy)); copy[27].uuid=BT_UUID_DECLARE_16(0x2901); assert(!valid_refs(&bad));
    memcpy(copy,hog_svc.attrs,sizeof(copy)); bad.attr_count=28; assert(!valid_refs(&bad));
}
static void make_expected(uint8_t *out) {
    memcpy(out,stock,sizeof(stock)); memcpy(out+sizeof(stock),bbp_exp_report_map,8);
    out[203]=4; out[204]=0x15; out[205]=0;
    memcpy(out+206,bbp_exp_report_map+8,sizeof(bbp_exp_report_map)-8);
}
static void test_map(void) {
    uint8_t expected[BBP_COMPOSITE_MAP_SIZE], result[512]; make_expected(expected);
    const struct bt_gatt_attr *a=&hog_svc.attrs[ATTR_MAP];
    const uint16_t lengths[]={0,1,2,7,22,64,380,511};
    for (uint16_t off=0;off<=BBP_COMPOSITE_MAP_SIZE+1U;++off) {
        for(size_t j=0;j<ARRAY_SIZE(lengths);++j) {
            memset(result,0xa5,sizeof(result));
            ssize_t n=a->read(&peer[off%2],a,result,lengths[j],off);
            if(off>BBP_COMPOSITE_MAP_SIZE) { assert(n==-BT_ATT_ERR_INVALID_OFFSET && result[0]==0xa5); }
            else {
                size_t want=BBP_COMPOSITE_MAP_SIZE-off;
                if(want>lengths[j]) { want=lengths[j]; }
                assert(n==(ssize_t)want && !memcmp(result,expected+off,want));
                for(size_t k=want;k<sizeof(result);++k) { assert(result[k]==0xa5); }
            }
        }
    }
    assert(a->read(&peer[0],a,result,1,UINT16_MAX)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a->read(&peer[0],a,NULL,0,380)==0);
    assert(a->read(&peer[0],a,NULL,0,0)==0);
    assert(a->read(&peer[0],a,bbp_composite_hog_stock+10,0,0)==0);
    assert(a->read(&peer[0],a,NULL,1,0)==-BT_ATT_ERR_UNLIKELY);
    /* An output/source collision must fail without changing either map. */
    assert(a->read(&peer[0],a,bbp_composite_hog_stock+10,10,0)==-BT_ATT_ERR_UNLIKELY);
    assert(a->read(&peer[0],a,bbp_composite_hog_stock+195,1,0)==-BT_ATT_ERR_UNLIKELY);
    assert(a->read(&peer[0],a,(void *)bbp_exp_report_map,1,0)==-BT_ATT_ERR_UNLIKELY);
    assert(!memcmp(bbp_composite_hog_stock,stock,sizeof(stock)));
    uint8_t joined[3][BBP_COMPOSITE_MAP_SIZE]={{0}};
    size_t pos[3]={0}, chunks[3]={20,22,37}; bool pending=true;
    while(pending) {
        pending=false;
        for(size_t p=0;p<3;++p) {
            if(pos[p]==BBP_COMPOSITE_MAP_SIZE) { continue; }
            pending=true;
            ssize_t n=a->read(&peer[p],a,joined[p]+pos[p],(uint16_t)chunks[p],(uint16_t)pos[p]);
            assert(n>0); pos[p]+=(size_t)n;
        }
    }
    for(size_t p=0;p<3;++p) { assert(!memcmp(joined[p],expected,sizeof(expected))); }
    uint8_t copied[sizeof(zmk_hid_report_desc)]; memcpy(copied,zmk_hid_report_desc,sizeof(copied));
    unsigned int before=raw_reads;
    assert(bbp_composite_hog_attr_read(&peer[0],a,result,512,0,copied,sizeof(copied))==sizeof(copied));
    assert(!memcmp(result,copied,sizeof(copied)));
    assert(bbp_composite_hog_attr_read(&peer[0],a,result,512,0,zmk_hid_report_desc,sizeof(copied)-1)==sizeof(copied)-1);
    assert(bbp_composite_hog_attr_read(&peer[0],&hog_svc.attrs[6],result,512,0,zmk_hid_report_desc,sizeof(copied))==sizeof(copied));
    assert(raw_reads==before+3); /* No descriptor-pointer/size/attribute collisions. */
}
static void read_slices(const struct bt_gatt_attr *a, struct bt_conn *c,
                        const uint8_t *expected, uint16_t size) {
    uint8_t result[32]; const uint16_t lengths[]={0,1,2,7,10,31};
    for(uint16_t off=0;off<=size+1U;++off) for(size_t j=0;j<ARRAY_SIZE(lengths);++j) {
        memset(result,0xa5,sizeof(result)); ssize_t n=a->read(c,a,result,lengths[j],off);
        if(off>size) { assert(n==-BT_ATT_ERR_INVALID_OFFSET && result[0]==0xa5); }
        else {
            size_t want=size-off; if(want>lengths[j]) { want=lengths[j]; }
            assert(n==(ssize_t)want && !memcmp(result,expected+off,want));
            for(size_t k=want;k<sizeof(result);++k) { assert(result[k]==0xa5); }
        }
    }
    assert(a->read(c,a,result,sizeof(result),UINT16_MAX)==-BT_ATT_ERR_INVALID_OFFSET);
}
static void test_reads_and_output(void) {
    const uint8_t p0[10]={15,4,64,255,128,128,128,255,0,0};
    const uint8_t p1[10]={2,8,0,1,255,128,55,0,255,0};
    const uint8_t neutral[10]={15,0,0,128,128,128,128,0,0,0};
    const uint8_t zero[4]={0}; uint8_t got[32], output[6]={1,2,3,4,5,6};
    const struct bt_gatt_attr *in=&hog_svc.attrs[ATTR_IN], *out=&hog_svc.attrs[ATTR_OUT];
    read_slices(in,&peer[0],p0,10); read_slices(in,&peer[1],p1,10);
    read_slices(out,&peer[0],zero,4);
    assert(in->read(NULL,in,got,32,0)==10 && !memcmp(got,neutral,10));
    assert(in->read(&peer[2],in,got,32,0)==10 && !memcmp(got,neutral,10));
    available[0]=false;
    assert(in->read(&peer[0],in,got,32,0)==10 && !memcmp(got,neutral,10));
    available[0]=true;
    reports[0]=bbp_gamepad_neutral();
    assert(in->read(&peer[0],in,got,32,0)==10 && !memcmp(got,neutral,10));
    for(unsigned int which=0;which<5;++which) {
        reports[0]=bbp_gamepad_neutral();
        if(which==0) { reports[0].hat=9; }
        if(which==1) { reports[0].x=INT8_MIN; }
        if(which==2) { reports[0].y=INT8_MIN; }
        if(which==3) { reports[0].z=INT8_MIN; }
        if(which==4) { reports[0].rz=INT8_MIN; }
        assert(in->read(&peer[0],in,got,32,0)==10 && !memcmp(got,neutral,10));
        assert(in->read(&peer[1],in,got,32,0)==10 && !memcmp(got,p1,10));
    }
    for(uint16_t n=0;n<=6;++n)
        assert(out->write(&peer[0],out,output,n,0,0)==(n==4?4:-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN));
    assert(out->write(&peer[0],out,output,4,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(out->write(&peer[0],out,NULL,4,0,0)==-BT_ATT_ERR_UNLIKELY);
    for(unsigned int flags=0;flags<256;++flags) {
        bool allowed=flags==0 || flags==2;
        assert(out->write(&peer[1],out,output,4,0,(uint8_t)flags)==(allowed?4:-0x06));
    }
    assert(out->read(&peer[0],out,got,32,0)==4 && !memcmp(got,zero,4));
    assert(out->read(&peer[1],out,got,32,0)==4 && !memcmp(got,zero,4));
    assert(!led_calls && ctrl_point==0); /* Output does not change stock state. */
}
static void test_stock_reads_led_and_control(void) {
    const struct bt_gatt_attr *a=hog_svc.attrs; uint8_t got[32];
    assert(a[2].read(&peer[0],a+2,got,32,0)==sizeof(info) && !memcmp(got,&info,sizeof(info)));
    assert(a[6].read(&peer[0],a+6,got,32,0)==sizeof(keyboard.body) && !memcmp(got,&keyboard.body,sizeof(keyboard.body)));
    assert(a[10].read(&peer[0],a+10,got,32,1)==sizeof(consumer.body)-1 && !memcmp(got,(uint8_t *)&consumer.body+1,sizeof(consumer.body)-1));
    assert(a[14].read(&peer[1],a+14,got,32,0)==sizeof(mouse.body) && !memcmp(got,&mouse.body,sizeof(mouse.body)));
    uint8_t led=0x15;
    assert(a[ATTR_LED].write(&peer[0],a+ATTR_LED,&led,1,0,0)==1);
    assert(led_calls==1 && last_led==led && last_led_profile==1);
    led=0x03; assert(a[ATTR_LED].write(&peer[1],a+ATTR_LED,&led,1,0,0)==1);
    assert(led_calls==2 && last_led==led && last_led_profile==2);
    assert(a[ATTR_LED].write(&peer[0],a+ATTR_LED,&led,1,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a[ATTR_LED].write(&peer[0],a+ATTR_LED,&led,0,0,0)==-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    assert(a[ATTR_LED].write(&peer[0],a+ATTR_LED,&led,2,0,0)==-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    peer[2].dst.a.val[5]=255;
    assert(a[ATTR_LED].write(&peer[2],a+ATTR_LED,&led,1,0,0)==-BT_ATT_ERR_UNLIKELY && led_calls==2);
    uint8_t value=0x7d;
    assert(a[ATTR_CTRL].write(&peer[0],a+ATTR_CTRL,&value,1,0,0)==1 && ctrl_point==value);
    assert(a[ATTR_CTRL].write(&peer[1],a+ATTR_CTRL,&value,1,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a[ATTR_CTRL].write(&peer[1],a+ATTR_CTRL,&value,2,0,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(ctrl_point==0x7d); /* Stock storage only: no modeled suspend semantics. */
}
static ssize_t set_ccc(const struct bt_gatt_attr *a, struct bt_conn *c, uint8_t value) {
    const uint8_t b[2]={value,0}; return a->write(c,a,b,2,0,0);
}
static void check_ccc(const struct bt_gatt_attr *a, struct bt_conn *c, uint8_t want) {
    uint8_t b[3]={0xff,0xff,0xff};
    assert(a->read(c,a,b,3,0)==2 && b[0]==want && b[1]==0 && b[2]==0xff);
}
/* Test-only synchronous delivery wrapper; explicitly not a queue/transport. */
static int mock_send(struct bt_conn *c, const struct bt_gatt_attr *value, const uint8_t *bytes, uint16_t n) {
    if (!c || !bt_gatt_is_subscribed(c,value,BT_GATT_CCC_NOTIFY)) { return -EAGAIN; }
    struct bt_gatt_notify_params p={.attr=value,.data=bytes,.len=n}; return bt_gatt_notify_cb(c,&p);
}
static void test_ccc_per_peer_and_isolation(void) {
    const struct bt_gatt_attr *a=hog_svc.attrs;
    const size_t cs[]={7,11,15,27}, vs[]={6,10,14,26};
    uint8_t payload[10]={15,4,64,255,128,128,128,255,0,0}, bad[2]={2,0};
    for(size_t i=0;i<4;++i) for(size_t p=0;p<2;++p) check_ccc(a+cs[i],&peer[p],0);
    assert(set_ccc(a+27,&peer[1],1)==2);
    assert(!host_requests_notification); /* Gamepad aggregate must not modify stock global. */
    assert(!bt_gatt_is_subscribed(&peer[0],a+26,1) && bt_gatt_is_subscribed(&peer[1],a+26,1));
    assert(mock_send(&peer[0],a+26,payload,10)==-EAGAIN && !sent_count);
    assert(mock_send(&peer[1],a+26,payload,10)==0 && sent_count==1 && sent[0].conn==&peer[1]);
    assert(sent[0].attr==a+26 && sent[0].len==10 && !memcmp(sent[0].bytes,payload,10));
    assert(set_ccc(a+7,&peer[0],1)==2 && host_requests_notification);
    assert(!bt_gatt_is_subscribed(&peer[0],a+26,1));
    assert(!bt_gatt_is_subscribed(&peer[1],a+6,1));
    assert(set_ccc(a+11,&peer[1],1)==2);
    assert(set_ccc(a+15,&peer[0],1)==2);
    for(size_t i=0;i<4;++i) for(size_t p=0;p<2;++p) {
        uint8_t expected=(uint8_t)((i==0 && p==0)||(i==1 && p==1)||
                                   (i==2 && p==0)||(i==3 && p==1));
        check_ccc(a+cs[i],&peer[p],expected);
        assert(bt_gatt_is_subscribed(&peer[p],a+vs[i],1)==(expected!=0));
    }
    /* Preserve the stock callback as-is: its global flag can become false when
     * one CCC is disabled even while a different stock CCC remains enabled. */
    assert(set_ccc(a+7,&peer[0],0)==2 && !host_requests_notification);
    assert(bt_gatt_is_subscribed(&peer[1],a+10,1) && bt_gatt_is_subscribed(&peer[0],a+14,1));
    assert(bt_gatt_is_subscribed(&peer[1],a+26,1));
    assert(set_ccc(a+27,&peer[0],1)==2 && set_ccc(a+27,&peer[1],0)==2);
    assert(!host_requests_notification);
    assert(bt_gatt_is_subscribed(&peer[0],a+26,1) && !bt_gatt_is_subscribed(&peer[1],a+26,1));
    assert(mock_send(&peer[1],a+26,payload,10)==-EAGAIN && sent_count==1);
    assert(mock_send(&peer[0],a+26,payload,10)==0 && sent[1].conn==&peer[0]);
    /* Same address bytes with a different address type are distinct mock peers. */
    peer[2].dst=peer[0].dst; peer[2].dst.type=2;
    assert(!bt_gatt_is_subscribed(&peer[2],a+26,1));
    assert(set_ccc(a+27,&peer[2],1)==2 && set_ccc(a+27,&peer[0],0)==2);
    assert(!bt_gatt_is_subscribed(&peer[0],a+26,1) && bt_gatt_is_subscribed(&peer[2],a+26,1));
    assert(set_ccc(a+27,&peer[0],1)==2);
    assert(a[27].write(&peer[0],a+27,bad,2,0,0)==-0x13);
    assert(a[27].write(&peer[0],a+27,bad,1,0,0)==-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    assert(a[27].write(&peer[0],a+27,bad,2,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a[27].write(NULL,a+27,bad,2,0,0)==-BT_ATT_ERR_UNLIKELY);
    check_ccc(a+27,&peer[0],1); check_ccc(a+27,&peer[1],0);
    assert(!bt_gatt_is_subscribed(NULL,a+26,1) && mock_send(NULL,a+26,payload,10)==-EAGAIN);
    assert(!bt_gatt_is_subscribed(&peer[0],a+23,1)); /* Output has no CCC. */
}
static void test_stock_senders(void) {
    assert(gamepad_mock_hog_init()==0);
    struct zmk_hid_keyboard_report_body kb=keyboard.body;
    struct zmk_hid_consumer_report_body co=consumer.body;
    struct zmk_hid_mouse_report_body mo=mouse.body;
    assert(zmk_hog_send_keyboard_report(&kb)==0 && zmk_hog_send_consumer_report(&co)==0 &&
           zmk_hog_send_mouse_report(&mo)==0 && submit_calls==3);
    memset(&kb,0,sizeof(kb)); memset(&co,0,sizeof(co)); memset(&mo,0,sizeof(mo));
    send_keyboard_report_callback(&hog_keyboard_work);
    send_consumer_report_callback(&hog_consumer_work);
    send_mouse_report_callback(&hog_mouse_work);
    const size_t idx[]={5,9,13}; const size_t sizes[]={sizeof(keyboard.body),sizeof(consumer.body),sizeof(mouse.body)};
    const void *values[]={&keyboard.body,&consumer.body,&mouse.body};
    assert(sent_count==3);
    for(size_t i=0;i<3;++i) assert(sent[i].conn==&peer[0] && sent[i].attr==&hog_svc.attrs[idx[i]] &&
                                     sent[i].len==sizes[i] && !memcmp(sent[i].bytes,values[i],sizes[i]));
    notify_error=-EPERM;
    assert(zmk_hog_send_keyboard_report(&keyboard.body)==0);
    send_keyboard_report_callback(&hog_keyboard_work);
    assert(security_calls==1 && peer[0].refs==1);
}
int main(int argc, char **argv) {
    if(fread(stock,1,sizeof(stock),stdin)!=sizeof(stock) || getchar()!=EOF) { return 2; }
    setup(); uint8_t out[BBP_COMPOSITE_MAP_SIZE];
    assert(hog_svc.attrs[4].read(&peer[0],&hog_svc.attrs[4],out,sizeof(out),0)==-BT_ATT_ERR_UNLIKELY);
    assert(bbp_composite_hog_load_stock(NULL,sizeof(stock))==-EINVAL);
    assert(bbp_composite_hog_load_stock(stock,sizeof(stock)-1)==-EINVAL);
    assert(bbp_composite_hog_load_stock(stock,sizeof(stock))==0);
    if(argc==2 && !strcmp(argv[1],"--map")) {
        assert(hog_svc.attrs[4].read(&peer[0],&hog_svc.attrs[4],out,sizeof(out),0)==sizeof(out));
        return fwrite(out,1,sizeof(out),stdout)==sizeof(out)?0:3;
    }
    if(argc==2 && !strcmp(argv[1],"--pure")) {
        return fwrite(bbp_exp_report_map,1,sizeof(bbp_exp_report_map),stdout)==sizeof(bbp_exp_report_map)?0:6;
    }
    if(argc==2 && !strcmp(argv[1],"--refs")) {
        for(size_t i=0;i<hog_svc.attr_count;++i) if(hog_svc.attrs[i].uuid->val==0x2908) {
            uint8_t r[2]; assert(hog_svc.attrs[i].read(&peer[0],&hog_svc.attrs[i],r,2,0)==2);
            printf("%u %u %zu\n",r[0],r[1],i);
        }
        return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--sample")) {
        assert(hog_svc.attrs[ATTR_IN].read(&peer[0],&hog_svc.attrs[ATTR_IN],out,10,0)==10);
        return fwrite(out,1,10,stdout)==10?0:4;
    }
    if(argc!=1) { return 5; }
#define RUN(fn) do { setup(); fn(); puts("PASS " #fn); } while(0)
    RUN(test_structure); RUN(test_bad_refs); RUN(test_map); RUN(test_reads_and_output);
    RUN(test_stock_reads_led_and_control); RUN(test_ccc_per_peer_and_isolation); RUN(test_stock_senders);
    puts("PASS one pinned HOG service + host-only appended reports; mock CCC/ATT/notifications, NOT real Zephyr or radio");
    return 0;
}
