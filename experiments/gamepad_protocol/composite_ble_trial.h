/* SPDX-License-Identifier: MIT
 * Explicit device-trial adapter, NOT a default production configuration.
 * One stock HOG is extended with the reviewed 380-byte composite descriptor.
 * Standard managed CCC writer is retained for identity/settings/lifecycle.
 * The added gate represents the last valid per-peer CCC request, combined with
 * the actual committed SDK subscription and runtime context at app claim time.
 * OFF denies new claims immediately in cfg_write, before SDK commit. ON never
 * bypasses the actual subscription. This is NOT an ATT commit barrier/ACK.
 * One call already claimed by the single runtime worker may enter or return
 * from notify after OFF; previously queued ATT PDUs cannot be retracted.
 * No spinlock crosses SDK calls. notify may block without a finite deadline.
 * Stock Control Point semantics are retained, not full HOG suspend support.
 */
#pragma once
#if !defined(BBP9981_COMPOSITE_DEVICE_TRIAL) || BBP9981_COMPOSITE_DEVICE_TRIAL != 1
#error "composite_ble_trial.h requires BBP9981_COMPOSITE_DEVICE_TRIAL=1"
#endif
#ifdef bt_gatt_attr_read
#error "candidate must replace, not accompany, another HOG read preinclude"
#endif
#include <errno.h>
#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include "../../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h"
#include "stadia_descriptor.h"
#include "stadia_codec.h"

#if !IS_ENABLED(CONFIG_ZMK_POINTING) || !IS_ENABLED(CONFIG_ZMK_HID_INDICATORS) || \
    IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
#error "composite candidate requires pointing/indicators ON and smooth scrolling OFF"
#endif
#define BBP_CANDIDATE_STOCK_SIZE 196U
#define BBP_CANDIDATE_INPUT_ID 4U
#define BBP_CANDIDATE_OUTPUT_ID 5U
#define BBP_CANDIDATE_MAP_SIZE (BBP_CANDIDATE_STOCK_SIZE + sizeof(bbp_exp_report_map) + 2U)
_Static_assert(sizeof(zmk_hid_report_desc) == BBP_CANDIDATE_STOCK_SIZE, "candidate needs pinned stock map");
_Static_assert(sizeof(bbp_exp_report_map) == 182U && BBP_EXP_INPUT_ID == 3U && BBP_EXP_OUTPUT_ID == 5U &&
               BBP_CANDIDATE_INPUT_ID == BBP_GAMEPAD_REPORT_ID,
               "candidate report identifiers or pure descriptor changed");
_Static_assert(BBP_EXP_INPUT_SIZE == BBP_EXPERIMENT_STADIA_BODY_SIZE && BBP_EXP_INPUT_SIZE == 10U &&
               BBP_EXP_OUTPUT_SIZE == 4U && sizeof(struct bbp_gamepad_report_body) == 9U,
               "candidate report body ABI changed");
_Static_assert(BBP_CANDIDATE_MAP_SIZE == 380U && BBP_CANDIDATE_MAP_SIZE <= UINT16_MAX,
               "candidate map size changed");

/* SHA-256 5c7e4106019e5baf9027c8a24e334c58f5e09c65cd3ff46b338c551c61da4ee3.
 * Exact comparison fails closed on same-length but semantically different maps. */
static const uint8_t bbp_candidate_stock_reference[BBP_CANDIDATE_STOCK_SIZE] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,
    0x29,0xe7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x05,0x07,0x75,0x08,0x95,0x01,0x81,0x03,0x05,0x08,0x19,0x01,
    0x29,0x05,0x75,0x01,0x95,0x05,0x91,0x02,0x05,0x08,0x75,0x03,
    0x95,0x01,0x91,0x03,0x05,0x07,0x15,0x00,0x26,0xff,0x00,0x19,
    0x00,0x29,0xff,0x75,0x08,0x95,0x06,0x81,0x00,0xc0,0x05,0x0c,
    0x09,0x01,0xa1,0x01,0x85,0x02,0x05,0x0c,0x15,0x00,0x26,0xff,
    0x0f,0x19,0x00,0x2a,0xff,0x0f,0x75,0x10,0x95,0x06,0x81,0x00,
    0xc0,0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x03,0x09,0x01,0xa1,
    0x00,0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x75,
    0x01,0x95,0x05,0x81,0x02,0x75,0x03,0x95,0x01,0x81,0x03,0x05,
    0x01,0x09,0x30,0x09,0x31,0x16,0x00,0x80,0x26,0xff,0x7f,0x75,
    0x10,0x95,0x02,0x81,0x06,0xa1,0x02,0x09,0x38,0x16,0x00,0x80,
    0x26,0xff,0x7f,0x35,0x00,0x45,0x00,0x75,0x10,0x95,0x01,0x81,
    0x06,0xc0,0xa1,0x02,0x05,0x0c,0x0a,0x38,0x02,0x16,0x00,0x80,
    0x26,0xff,0x7f,0x35,0x00,0x45,0x00,0x75,0x10,0x95,0x01,0x81,
    0x06,0xc0,0xc0,0xc0,
};
static bool bbp_candidate_map_valid(void) {
    static const uint8_t prefix[] = {0x05,0x01,0x09,0x05,0xa1,0x01,0x85,0x03};
    return !memcmp(zmk_hid_report_desc, bbp_candidate_stock_reference, sizeof(bbp_candidate_stock_reference)) &&
           !memcmp(bbp_exp_report_map, prefix, sizeof(prefix));
}
static bool bbp_candidate_overlap(const void *a, size_t an, const void *b, size_t bn) {
    if (!an || !bn) { return false; }
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    return x<=y ? y-x<an : x-y<bn;
}
extern const struct bt_gatt_service_static hog_svc;
static ssize_t bbp_candidate_attr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                      void *buf, uint16_t len, uint16_t offset,
                                      const void *value, uint16_t value_len) {
    if (attr != &hog_svc.attrs[4] || value != (const void *)zmk_hid_report_desc ||
        value_len != sizeof(zmk_hid_report_desc)) {
        return bt_gatt_attr_read(conn,attr,buf,len,offset,value,value_len);
    }
    if (!bbp_candidate_map_valid()) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    if ((size_t)offset > BBP_CANDIDATE_MAP_SIZE) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    size_t count=BBP_CANDIDATE_MAP_SIZE-offset;
    if (count>len) { count=len; }
    if ((!buf && count) || bbp_candidate_overlap(buf,count,zmk_hid_report_desc,sizeof(zmk_hid_report_desc)) ||
        bbp_candidate_overlap(buf,count,bbp_exp_report_map,sizeof(bbp_exp_report_map)) ||
        bbp_candidate_overlap(buf,count,bbp_candidate_stock_reference,sizeof(bbp_candidate_stock_reference))) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    uint8_t *out=buf;
    for(size_t i=0;i<count;++i) {
        size_t pos=(size_t)offset+i;
        if(pos<BBP_CANDIDATE_STOCK_SIZE) { out[i]=zmk_hid_report_desc[pos]; }
        else {
            size_t p=pos-BBP_CANDIDATE_STOCK_SIZE;
            if(p<7U) { out[i]=bbp_exp_report_map[p]; }
            else if(p==7U) { out[i]=BBP_CANDIDATE_INPUT_ID; }
            else if(p==8U) { out[i]=0x15; }
            else if(p==9U) { out[i]=0; }
            else { out[i]=bbp_exp_report_map[p-2U]; }
        }
    }
    return (ssize_t)count;
}
static bool bbp_candidate_connected(struct bt_conn *conn) {
    struct bt_conn_info info;
    return conn && !bt_conn_get_info(conn,&info) && info.state==BT_CONN_STATE_CONNECTED;
}
static bool bbp_candidate_route_matches(struct bt_conn *conn) {
    if (!conn) { return false; }
    struct zmk_endpoint_instance endpoint=zmk_endpoints_selected();
    int profile=zmk_ble_active_profile_index();
    const bt_addr_le_t *dst=bt_conn_get_dst(conn);
    return endpoint.transport==ZMK_TRANSPORT_BLE && profile>=0 &&
           endpoint.ble.profile_index==profile && dst && zmk_ble_profile_index(dst)==profile;
}
static bool bbp_candidate_current(struct bt_conn *conn, uint32_t epoch) {
    if (!bbp_candidate_connected(conn) || !bbp_gamepad_context_valid(epoch) ||
        !bbp_candidate_route_matches(conn)) { return false; }
    struct bt_conn *active=zmk_ble_active_profile_conn();
    bool same=active==conn;
    if(active) { bt_conn_unref(active); }
    return same && bbp_candidate_connected(conn) && bbp_candidate_route_matches(conn) &&
           bbp_gamepad_context_valid(epoch);
}

/* One storage instance in the force-included HOG translation unit. The separate
 * lifecycle TU calls only the exported reset API; it must not include this file.
 * Index reuse is reset on connection events without erasing the revision. */
struct bbp_trial_peer_gate { uint32_t generation, claim_generation; bool denied, busy; };
static struct bbp_trial_peer_gate bbp_trial_gate[CONFIG_BT_MAX_CONN];
static struct k_spinlock bbp_trial_gate_lock;
_Static_assert(CONFIG_BT_MAX_CONN > 0, "trial needs at least one connection slot");
static int bbp_trial_index(struct bt_conn *conn) {
    if (!conn) { return -1; }
    unsigned int index = bt_conn_index(conn);
    return index < ARRAY_SIZE(bbp_trial_gate) ? (int)index : -1;
}
static bool bbp_trial_begin(unsigned int index, uint32_t *generation) {
    k_spinlock_key_t key = k_spin_lock(&bbp_trial_gate_lock);
    *generation = bbp_trial_gate[index].generation;
    bool allowed = !bbp_trial_gate[index].denied && !bbp_trial_gate[index].busy;
    k_spin_unlock(&bbp_trial_gate_lock, key);
    return allowed;
}
static void bbp_trial_request(unsigned int index, bool on) {
    k_spinlock_key_t key = k_spin_lock(&bbp_trial_gate_lock);
    bbp_trial_gate[index].denied = !on;
    ++bbp_trial_gate[index].generation;
    k_spin_unlock(&bbp_trial_gate_lock, key);
}
static bool bbp_trial_claim(unsigned int index, uint32_t generation, uint32_t context) {
    k_spinlock_key_t key = k_spin_lock(&bbp_trial_gate_lock);
    /* context_valid is only an atomic_get, no state_lock or SDK call. */
    bool allowed = !bbp_trial_gate[index].denied && !bbp_trial_gate[index].busy &&
        bbp_trial_gate[index].generation == generation && bbp_gamepad_context_valid(context);
    if (allowed) {
        bbp_trial_gate[index].busy = true;
        bbp_trial_gate[index].claim_generation = generation;
    }
    k_spin_unlock(&bbp_trial_gate_lock, key);
    return allowed;
}
static void bbp_trial_retire(unsigned int index, uint32_t claim_generation) {
    k_spinlock_key_t key = k_spin_lock(&bbp_trial_gate_lock);
    /* OFF/reset never retires another invocation's in-flight slot. */
    if (bbp_trial_gate[index].busy && bbp_trial_gate[index].claim_generation == claim_generation) {
        bbp_trial_gate[index].busy = false;
    }
    k_spin_unlock(&bbp_trial_gate_lock, key);
}
void bbp_trial_connection_reset(struct bt_conn *conn, bool connected) {
    int index = bbp_trial_index(conn);
    if (index < 0) { return; }
    k_spinlock_key_t key = k_spin_lock(&bbp_trial_gate_lock);
    bbp_trial_gate[index].denied = true;
    uint32_t reset_generation = ++bbp_trial_gate[index].generation;
    k_spin_unlock(&bbp_trial_gate_lock, key);
    bbp_gamepad_transport_invalidate();
    key = k_spin_lock(&bbp_trial_gate_lock);
    /* A concurrent new request or second reset owns the newer state. */
    if (connected && bbp_trial_gate[index].generation == reset_generation) {
        bbp_trial_gate[index].denied = false;
        ++bbp_trial_gate[index].generation;
    }
    k_spin_unlock(&bbp_trial_gate_lock, key);
}

static uint8_t bbp_candidate_input_ref[2]={BBP_CANDIDATE_INPUT_ID,1U};
static uint8_t bbp_candidate_output_ref[2]={BBP_CANDIDATE_OUTPUT_ID,2U};
static ssize_t bbp_candidate_read_ref(struct bt_conn *c,const struct bt_gatt_attr *a,
                                      void *buf,uint16_t len,uint16_t off) {
    return bt_gatt_attr_read(c,a,buf,len,off,a->user_data,2U);
}
static ssize_t bbp_candidate_read_input(struct bt_conn *c,const struct bt_gatt_attr *a,
                                        void *buf,uint16_t len,uint16_t off) {
    if (!bbp_candidate_map_valid()) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    struct bbp_gamepad_report_body body=bbp_gamepad_neutral();
    uint32_t epoch=bbp_gamepad_context_epoch();
    if (bbp_candidate_current(c,epoch)) {
        bbp_gamepad_get_report(ZMK_TRANSPORT_BLE,&body);
        if (!bbp_candidate_current(c,epoch)) { body=bbp_gamepad_neutral(); }
    }
    uint8_t encoded[BBP_EXP_INPUT_SIZE];
    (void)bbp_experiment_stadia_encode(&body,encoded); /* Invalid state -> neutral. */
    return bt_gatt_attr_read(c,a,buf,len,off,encoded,sizeof(encoded));
}
static ssize_t bbp_candidate_read_output(struct bt_conn *c,const struct bt_gatt_attr *a,
                                         void *buf,uint16_t len,uint16_t off) {
    if (!bbp_candidate_map_valid()) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    const uint8_t zero[BBP_EXP_OUTPUT_SIZE]={0};
    return bt_gatt_attr_read(c,a,buf,len,off,zero,sizeof(zero));
}
static ssize_t bbp_candidate_write_output(struct bt_conn *c,const struct bt_gatt_attr *a,
                                          const void *buf,uint16_t len,uint16_t off,uint8_t flags) {
    (void)c; (void)a;
    if (!bbp_candidate_map_valid()) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    if (flags & (uint8_t)~0x02U) { return BT_GATT_ERR(0x06); }
    if (off) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    if (len != BBP_EXP_OUTPUT_SIZE) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN); }
    if (!buf) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    return len; /* Accept and ignore. No rumble implementation or stored payload. */
}
static void bbp_candidate_ccc_changed(const struct bt_gatt_attr *a,uint16_t value) {
    (void)a; (void)value;
    /* Aggregate restore/disconnect transitions; never used as peer permission. */
    bbp_gamepad_transport_invalidate();
}
static ssize_t bbp_candidate_ccc_write(struct bt_conn *conn,const struct bt_gatt_attr *a,uint16_t value) {
    (void)a;
    if (!conn) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    if (value>BT_GATT_CCC_NOTIFY) { return BT_GATT_ERR(0x13); }
    int index = bbp_trial_index(conn);
    if (index < 0) { return BT_GATT_ERR(0x11); }
    /* OFF immediately closes new app claims while the SDK still sees old ON.
     * ON is only requested permission: send also queries actual subscription.
     * No lock is held while the SDK commits or callbacks invalidate runtime. */
    bbp_trial_request((unsigned int)index, value == BT_GATT_CCC_NOTIFY);
    bbp_gamepad_transport_invalidate();
    return sizeof(uint16_t);
}
static const struct bt_gatt_attr *bbp_candidate_value(const struct bt_gatt_service_static *svc) {
    if (!svc || !svc->attrs || svc->attr_count<7U) { return NULL; }
    const struct bt_gatt_attr *value=&svc->attrs[svc->attr_count-3U];
    const struct bt_gatt_attr *ccc=&svc->attrs[svc->attr_count-2U];
    const struct bt_gatt_attr *ref=&svc->attrs[svc->attr_count-1U];
    if (value->read!=bbp_candidate_read_input || !(value->perm&BT_GATT_PERM_READ_ENCRYPT) ||
        ccc->read!=bt_gatt_attr_read_ccc || ccc->write!=bt_gatt_attr_write_ccc || !ccc->user_data ||
        (ccc->perm & (BT_GATT_PERM_READ_ENCRYPT|BT_GATT_PERM_WRITE_ENCRYPT)) !=
            (BT_GATT_PERM_READ_ENCRYPT|BT_GATT_PERM_WRITE_ENCRYPT) ||
        ((struct _bt_gatt_ccc *)ccc->user_data)->cfg_write!=bbp_candidate_ccc_write ||
        ((struct _bt_gatt_ccc *)ccc->user_data)->cfg_changed!=bbp_candidate_ccc_changed ||
        ((struct _bt_gatt_ccc *)ccc->user_data)->cfg_match!=NULL ||
        ref->read!=bbp_candidate_read_ref || ref->user_data!=bbp_candidate_input_ref ||
        bbp_candidate_input_ref[0]!=BBP_CANDIDATE_INPUT_ID || bbp_candidate_input_ref[1]!=1U ||
        !(ref->perm&BT_GATT_PERM_READ_ENCRYPT)) { return NULL; }
    return value;
}
/* Same synchronous return-code ABI as the existing runtime. Caller retains its captured connection ref;
 * do not broadcast, request security, retain caller data, retry or schedule here. */
int bbp_gamepad_ble_send(struct bt_conn *captured_conn,const struct bbp_gamepad_report_body *body,
                         uint32_t expected_context_epoch) {
    if (!body) { return -EINVAL; }
    if (!bbp_candidate_connected(captured_conn)) { return -ENOTCONN; }
    int trial_index = bbp_trial_index(captured_conn);
    if (trial_index < 0) { return -EINVAL; }
    uint32_t trial_generation;
    if (!bbp_trial_begin((unsigned int)trial_index, &trial_generation)) { return -EAGAIN; }
    if (!bbp_candidate_map_valid()) { return -EINVAL; }
    const struct bt_gatt_attr *value=bbp_candidate_value(&hog_svc);
    if (!value) { return -EINVAL; }
    uint32_t admission_epoch=bbp_gamepad_context_epoch();
    if (!bt_gatt_is_subscribed(captured_conn,value,BT_GATT_CCC_NOTIFY)) { return -EAGAIN; }
    struct bbp_gamepad_report_body frozen=*body;
    uint8_t encoded[BBP_EXP_INPUT_SIZE];
    if (!bbp_experiment_stadia_encode(&frozen,encoded)) { return -EINVAL; }
    bool neutral=bbp_gamepad_is_neutral(&frozen);
    if (!neutral && !bbp_candidate_current(captured_conn,expected_context_epoch)) { return -ESTALE; }
    if (!bbp_candidate_connected(captured_conn)) { return -ENOTCONN; }
    if (!bt_gatt_is_subscribed(captured_conn,value,BT_GATT_CCC_NOTIFY)) { return -EAGAIN; }
    if (neutral) {
        /* Old caller epoch/route are intentionally allowed only for a neutral
         * release to this same subscribed captured peer. A change DURING this
         * admission is retryable; the runtime owns release retries. */
        if (!bbp_gamepad_context_valid(admission_epoch)) { return -EAGAIN; }
    } else if (!bbp_candidate_current(captured_conn,expected_context_epoch)) { return -ESTALE; }
    /* Pinned gatt.c copies params->data into an ATT buffer before returning,
     * including the multi-notify path. This condition must be rechecked against
     * the ACTUAL built SDK; the ordinary guard checks GATT dirtiness, not ATT.
     * The isolated stager additionally locks the three BLE source bytes; it is
     * not proof of an ARM build. No completion callback: it may arrive early
     * or never on disconnect. */
    /* The actual queue-owning runtime has exactly one transport worker. Claim
     * is the app admission point, not ATT enqueue; no SDK call is under this lock.
     * Non-neutral old frames keep their captured epoch, not a newly minted token. */
    uint32_t trial_context = neutral ? admission_epoch : expected_context_epoch;
    if (!bbp_trial_claim((unsigned int)trial_index, trial_generation, trial_context)) {
        return neutral ? -EAGAIN : -ESTALE;
    }
    struct bt_gatt_notify_params params={.attr=value,.data=encoded,.len=sizeof(encoded)};
    int result = bt_gatt_notify_cb(captured_conn,&params);
    bbp_trial_retire((unsigned int)trial_index, trial_generation);
    /* Subscription can disappear after claim. Preserve finite existing neutral
     * retries rather than treating this specific race as a permanent ABI error. */
    if (result == -EINVAL && !bt_gatt_is_subscribed(captured_conn,value,BT_GATT_CCC_NOTIFY)) {
        return -EAGAIN;
    }
    return result;
}

/* Preserve all fixture stock args and indices, output before input so the input
 * value, CCC and ref remain at tail -3/-2/-1. Standard CCC write pointer is
 * essential to pinned Zephyr restore/disconnect processing. */
#undef BT_GATT_SERVICE_DEFINE
#define BT_GATT_SERVICE_DEFINE(_name, ...) \
    const struct bt_gatt_attr attr_##_name[] = { \
        __VA_ARGS__, \
        BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, \
            BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, \
            BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, \
            bbp_candidate_read_output, bbp_candidate_write_output, NULL), \
        BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, \
            bbp_candidate_read_ref, NULL, bbp_candidate_output_ref), \
        BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
            BT_GATT_PERM_READ_ENCRYPT, bbp_candidate_read_input, NULL, NULL), \
        BT_GATT_CCC_MANAGED(((struct _bt_gatt_ccc[]) { \
            BT_GATT_CCC_INITIALIZER(bbp_candidate_ccc_changed,bbp_candidate_ccc_write,NULL)}), \
            BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT), \
        BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, \
            bbp_candidate_read_ref, NULL, bbp_candidate_input_ref) \
    }; \
    const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, _name) = BT_GATT_SERVICE(attr_##_name)
#define bt_gatt_attr_read bbp_candidate_attr_read
