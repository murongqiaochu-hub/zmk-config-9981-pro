/* SPDX-License-Identifier: MIT
 * Host contract tests: the ACTUAL gamepad preinclude followed by the complete,
 * unmodified pinned ZMK v0.3.0 hog.c fixture and faithful pinned GATT macros.
 * No SDK edits, radio, pairing, hardware or Android enumeration are performed.
 *
 * cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
 *    -fsanitize=undefined -fno-sanitize-recover=all \
 *    -Itests/gamepad_ble_mocks tests/test_gamepad_ble.c -o /tmp/test_gamepad_ble
 * /tmp/test_gamepad_ble
 * Repeat with -DBBP_GAMEPAD_TEST_WITH_SNAPSHOT=1 and/or
 * -DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=1 for the integration variants.
 * -Wno-unused-parameter is needed by the UNMODIFIED upstream hog callbacks.
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_ZMK_POINTING
#define CONFIG_ZMK_POINTING 1
#endif
#ifndef CONFIG_ZMK_POINTING_SMOOTH_SCROLLING
#define CONFIG_ZMK_POINTING_SMOOTH_SCROLLING 0
#endif
#ifndef CONFIG_ZMK_HID_INDICATORS
#define CONFIG_ZMK_HID_INDICATORS 1
#endif
#ifndef BBP_GAMEPAD_TEST_WITH_SNAPSHOT
#define BBP_GAMEPAD_TEST_WITH_SNAPSHOT 0
#endif
#define CONFIG_ZMK_BLE_THREAD_STACK_SIZE 512
#define CONFIG_ZMK_BLE_THREAD_PRIORITY 5
#define CONFIG_ZMK_BLE_INIT_PRIORITY 50
#define CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE 4
#define CONFIG_ZMK_LOG_LEVEL 0

#if BBP_GAMEPAD_TEST_WITH_SNAPSHOT
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h"
#endif
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_ble_extension.h"
#include "gamepad_ble_mocks/sdk_v0_3_0_hog.c"
#undef bt_gatt_attr_read
#if BBP_GAMEPAD_TEST_WITH_SNAPSHOT
#undef zmk_hog_send_mouse_report
#endif

static struct bt_conn peers[3]; /* peers[2] is another connection object for profile0. */
static struct bt_conn *active_connection;
static int active_profile;
static struct zmk_endpoint_instance selected;
static bool no_dst, report_available;
static uint32_t context_epoch;
static struct bbp_gamepad_report_body current_report;
static unsigned int ref_calls, unref_calls, active_gets, report_gets, invalidations;
static unsigned int raw_reads, subscription_checks, notify_count, security_calls, lock_depth;
static unsigned int submits, schedules;
static int notify_error;
static void (*report_hook)(void), (*active_hook)(void), (*subscription_hook)(void);
static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static struct zmk_hid_mouse_report mouse;
struct notification {
    struct bt_conn *conn;
    const struct bt_gatt_attr *attr;
    uint16_t len;
    uint8_t bytes[32];
};
static struct notification notifications[64];
struct k_work_q k_sys_work_q;

static const struct bt_gatt_attr *gamepad_value(void) {
    return &hog_svc.attrs[hog_svc.attr_count - 3U];
}
static const struct bt_gatt_attr *gamepad_ccc(void) {
    return &hog_svc.attrs[hog_svc.attr_count - 2U];
}
static void run_hook(void (**slot)(void)) {
    void (*hook)(void) = *slot;
    *slot = NULL;
    if (hook) { hook(); }
}

struct bt_conn *bt_conn_ref(struct bt_conn *conn) {
    assert(conn && conn->refs > 0);
    conn->refs++;
    ref_calls++;
    return conn;
}
void bt_conn_unref(struct bt_conn *conn) {
    assert(conn && conn->refs > 1 && !lock_depth);
    conn->refs--;
    unref_calls++;
}
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn) {
    assert(conn);
    return no_dst ? NULL : &conn->dst;
}
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *out) {
    assert(conn && out);
    if (conn->info_error) { return conn->info_error; }
    out->state = conn->state;
    return 0;
}
int bt_conn_set_security(struct bt_conn *conn, bt_security_t security) {
    assert(conn && security == BT_SECURITY_L2 && !lock_depth);
    security_calls++;
    return 0;
}
int zmk_ble_active_profile_index(void) { return active_profile; }
int zmk_ble_profile_index(const bt_addr_le_t *addr) {
    assert(addr);
    return addr->a.val[0] == 2U ? 0 : addr->a.val[0];
}
struct bt_conn *zmk_ble_active_profile_conn(void) {
    active_gets++;
    struct bt_conn *conn = active_connection;
    if (conn) { bt_conn_ref(conn); }
    run_hook(&active_hook);
    return conn; /* Pinned ZMK getter returns one new reference. */
}
struct zmk_endpoint_instance zmk_endpoints_selected(void) { return selected; }
uint32_t bbp_gamepad_context_epoch(void) { return context_epoch; }
bool bbp_gamepad_context_valid(uint32_t expected) { return expected == context_epoch; }
void bbp_gamepad_transport_invalidate(void) {
    invalidations++;
    context_epoch++;
    report_available = false;
    current_report = bbp_gamepad_neutral();
}
void bbp_gamepad_get_report(uint8_t transport, struct bbp_gamepad_report_body *out) {
    assert(transport == 1U && out); /* Actual ABI: BLE transport1; not report ID4. */
    report_gets++;
    *out = report_available && selected.transport == ZMK_TRANSPORT_BLE
               ? current_report : bbp_gamepad_neutral();
    run_hook(&report_hook);
}

ssize_t bt_gatt_attr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                          uint16_t len, uint16_t offset, const void *value, uint16_t value_len) {
    (void)conn;
    (void)attr;
    raw_reads++;
    if (offset > value_len) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    size_t count = value_len - offset;
    if (count > len) { count = len; }
    if (count) { memcpy(buf, (const uint8_t *)value + offset, count); }
    return (ssize_t)count;
}
ssize_t bt_gatt_attr_read_service(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset) {
    const struct bt_uuid *uuid = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &uuid->val, sizeof(uuid->val));
}
ssize_t bt_gatt_attr_read_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset) {
    const struct bt_gatt_chrc *chrc = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &chrc->properties, 1U);
}
ssize_t bt_gatt_attr_read_ccc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len, uint16_t offset) {
    struct _bt_gatt_ccc *ccc = attr->user_data;
    unsigned int i = conn->dst.a.val[0];
    assert(i < BT_GATT_CCC_MAX);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &ccc->cfg[i].value, 2U);
}
ssize_t bt_gatt_attr_write_ccc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    (void)flags;
    if (offset) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    if (len != 2U) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN); }
    struct _bt_gatt_ccc *ccc = attr->user_data;
    unsigned int i = conn->dst.a.val[0];
    assert(i < BT_GATT_CCC_MAX);
    const uint8_t *bytes = buf;
    uint16_t old_value = ccc->value;
    ccc->cfg[i].value = (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
    ccc->cfg[i].peer = conn->dst;
    ccc->value = 0;
    for (i = 0; i < BT_GATT_CCC_MAX; ++i) { ccc->value |= ccc->cfg[i].value; }
    if (ccc->value != old_value && ccc->cfg_changed) { ccc->cfg_changed(attr, ccc->value); }
    return len;
}
bool bt_gatt_is_subscribed(struct bt_conn *conn, const struct bt_gatt_attr *attr, uint16_t type) {
    assert(conn && attr == gamepad_value() && attr->uuid->val == 0x2a4d);
    assert(type == BT_GATT_CCC_NOTIFY);
    subscription_checks++;
    struct _bt_gatt_ccc *ccc = gamepad_ccc()->user_data;
    bool subscribed = (ccc->cfg[conn->dst.a.val[0]].value & type) != 0U;
    run_hook(&subscription_hook);
    return subscribed;
}
int bt_gatt_notify_cb(struct bt_conn *conn, struct bt_gatt_notify_params *params) {
    assert(conn); /* Fail the test on ANY NULL broadcast. */
    assert(params && params->attr && params->data && params->len <= 32U && !lock_depth);
    assert(!params->uuid && !params->func && !params->user_data);
    if (params->attr == gamepad_value()) {
        assert(params->attr->uuid->val == 0x2a4d);
        assert(params->attr->perm == BT_GATT_PERM_READ_ENCRYPT);
        assert(params->len == 9U); /* Body only; Report Reference carries ID4. */
    } else {
        assert(params->attr == &hog_svc.attrs[5] || params->attr == &hog_svc.attrs[9] ||
               params->attr == &hog_svc.attrs[13]);
    }
    assert(notify_count < ARRAY_SIZE(notifications));
    struct notification *out = &notifications[notify_count++];
    *out = (struct notification){.conn = conn, .attr = params->attr, .len = params->len};
    memcpy(out->bytes, params->data, params->len);
    return notify_error;
}

struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) { return &keyboard; }
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) { return &consumer; }
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void) { return &mouse; }
#if CONFIG_ZMK_HID_INDICATORS
void zmk_hid_indicators_process_report(struct zmk_hid_led_report_body *report,
                                       struct zmk_endpoint_instance endpoint) {
    assert(report && endpoint.transport == ZMK_TRANSPORT_BLE);
}
#endif
#if CONFIG_ZMK_POINTING_SMOOTH_SCROLLING
struct zmk_pointing_resolution_multipliers
zmk_pointing_resolution_multipliers_get_profile(struct zmk_endpoint_instance endpoint) {
    assert(endpoint.transport == ZMK_TRANSPORT_BLE);
    return (struct zmk_pointing_resolution_multipliers){.wheel = 2U, .hor_wheel = 3U};
}
void zmk_pointing_resolution_multipliers_process_report(
    struct zmk_hid_mouse_resolution_feature_report_body *report,
    struct zmk_endpoint_instance endpoint) {
    assert(report && endpoint.transport == ZMK_TRANSPORT_BLE);
}
#endif

k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = true;
    return lock_depth++;
}
void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held && lock_depth == key + 1U);
    lock->held = false;
    lock_depth--;
}
int64_t k_uptime_get(void) { return 0; }
int k_msgq_put(struct k_msgq *q, const void *data, k_timeout_t timeout) {
    assert(timeout == K_NO_WAIT || timeout == K_MSEC(100));
    if (q->used == q->capacity) { return -EAGAIN; }
    memcpy(q->buffer + q->write_pos * q->item_size, data, q->item_size);
    q->write_pos = (q->write_pos + 1U) % q->capacity;
    q->used++;
    return 0;
}
int k_msgq_get(struct k_msgq *q, void *data, k_timeout_t timeout) {
    assert(timeout == K_NO_WAIT);
    if (!q->used) { return -ENOMSG; }
    memcpy(data, q->buffer + q->read_pos * q->item_size, q->item_size);
    q->read_pos = (q->read_pos + 1U) % q->capacity;
    q->used--;
    return 0;
}
unsigned int k_msgq_num_used_get(struct k_msgq *q) { return q->used; }
int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work) {
    assert(queue == &hog_work_q && queue->started && work);
    submits++;
    return 1;
}
int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                               k_timeout_t delay) {
    assert(queue && work && delay >= 0);
    schedules++;
    if (!queue->started) { return -ENODEV; }
    if (queue->plugged) { return -EBUSY; }
    work->queued = true;
    work->queue = queue;
    work->due = delay;
    return 1;
}
void k_work_queue_start(struct k_work_q *queue, void *stack, size_t size, int priority,
                        const struct k_work_queue_config *config) {
    assert(queue == &hog_work_q && stack && size && priority == CONFIG_ZMK_BLE_THREAD_PRIORITY);
    assert(config && strcmp(config->name, "HID Over GATT Send Work") == 0);
    queue->started = true;
}

static void balanced(void) {
    assert(ref_calls == unref_calls && !lock_depth);
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) { assert(peers[i].refs == 1); }
}
static void reset_case(void) {
    balanced();
    ref_calls = unref_calls = active_gets = report_gets = invalidations = 0U;
    raw_reads = subscription_checks = notify_count = security_calls = submits = schedules = 0U;
    notify_error = 0;
    report_hook = active_hook = subscription_hook = NULL;
    no_dst = false;
    context_epoch = 7U;
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) {
        peers[i] = (struct bt_conn){.state = BT_CONN_STATE_CONNECTED, .refs = 1};
        peers[i].dst.a.val[0] = (uint8_t)i;
    }
    active_profile = 0;
    active_connection = &peers[0];
    selected = (struct zmk_endpoint_instance){.transport = ZMK_TRANSPORT_BLE,
                                              .ble = {.profile_index = 0}};
    report_available = true;
    current_report = (struct bbp_gamepad_report_body){.buttons_lo = 0xc3U,
        .buttons_hi = 0x0cU, .hat = 3U, .x = -127, .y = 127, .z = -73, .rz = 42,
        .lt = 255U, .rt = 255U};
    host_requests_notification = false;
    for (size_t i = 0; i < hog_svc.attr_count; ++i) {
        if (hog_svc.attrs[i].uuid->val == 0x2902) {
            struct _bt_gatt_ccc *ccc = hog_svc.attrs[i].user_data;
            memset(ccc->cfg, 0, sizeof(ccc->cfg));
            ccc->value = 0;
        }
    }
    struct _bt_gatt_ccc *ccc = gamepad_ccc()->user_data;
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) {
        ccc->cfg[i].value = BT_GATT_CCC_NOTIFY;
        ccc->cfg[i].peer = peers[i].dst;
    }
    ccc->value = BT_GATT_CCC_NOTIFY;
    hog_work_q = k_sys_work_q = (struct k_work_q){.started = true};
    keyboard = (struct zmk_hid_keyboard_report){.report_id = 1U,
        .body = {.modifiers = 8U, .keys = {4U,5U}}};
    consumer = (struct zmk_hid_consumer_report){.report_id = 2U, .body = {.keys = {0x123U}}};
    mouse = (struct zmk_hid_mouse_report){.report_id = 3U,
        .body = {.buttons = 3U, .d_x = -12, .d_y = 24, .d_scroll_y = -2, .d_scroll_x = 1}};
}
static void check_chrc(size_t index, uint16_t uuid, uint8_t props, uint16_t perm,
                       bt_gatt_attr_read_func_t read, bt_gatt_attr_write_func_t write) {
    const struct bt_gatt_attr *decl = &hog_svc.attrs[index], *value = decl + 1;
    assert(decl->uuid->val == 0x2803 && decl->perm == BT_GATT_PERM_READ);
    assert(decl->read == bt_gatt_attr_read_chrc && !decl->write && decl->user_data);
    const struct bt_gatt_chrc *chrc = decl->user_data;
    assert(chrc->uuid->val == uuid && chrc->properties == props && !chrc->value_handle);
    assert(value->uuid->val == uuid && value->perm == perm && value->read == read);
    assert(value->write == write && !value->handle);
}
static void check_stock_input(size_t index, void *report_ref, bt_gatt_attr_read_func_t read) {
    check_chrc(index, 0x2a4d, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
               BT_GATT_PERM_READ_ENCRYPT, read, NULL);
    const struct bt_gatt_attr *ccc = &hog_svc.attrs[index + 2U], *ref = ccc + 1;
    assert(ccc->uuid->val == 0x2902);
    assert(ccc->perm == (BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));
    assert(ccc->read == bt_gatt_attr_read_ccc && ccc->write == bt_gatt_attr_write_ccc);
    struct _bt_gatt_ccc *storage = ccc->user_data;
    assert(storage && storage->cfg_changed == input_ccc_changed);
    assert(ref->uuid->val == 0x2908 && ref->perm == BT_GATT_PERM_READ_ENCRYPT);
    assert(ref->read == read_hids_report_ref && !ref->write && ref->user_data == report_ref);
}
static struct bbp_gamepad_report_body read_gamepad(struct bt_conn *conn) {
    struct bbp_gamepad_report_body out;
    assert(sizeof(out) == 9U);
    assert(gamepad_value()->read(conn, gamepad_value(), &out, sizeof(out), 0) == 9);
    return out;
}
static void assert_neutral_read(struct bt_conn *conn) {
    struct bbp_gamepad_report_body out = read_gamepad(conn);
    static const uint8_t neutral_bytes[9] = {0U,0U,8U,0U,0U,0U,0U,0U,0U};
    assert(bbp_gamepad_is_neutral(&out) && out.hat == 8U);
    assert(memcmp(&out, neutral_bytes, sizeof(neutral_bytes)) == 0);
}
static void bump_epoch(void) { context_epoch++; }
static void change_route(void) { selected.transport = ZMK_TRANSPORT_USB; }
static void replace_connection(void) { active_connection = &peers[2]; }
static void disconnect_active(void) { peers[0].state = BT_CONN_STATE_DISCONNECTED; }

static void test_pinned_service_old_attrs_and_tail(void) {
    reset_case();
    assert(hog_svc.attrs == attr_hog_svc);
    assert(hog_svc.attrs[0].uuid->val == 0x2800 && hog_svc.attrs[0].perm == BT_GATT_PERM_READ);
    assert(hog_svc.attrs[0].read == bt_gatt_attr_read_service && !hog_svc.attrs[0].write);
    assert(((const struct bt_uuid *)hog_svc.attrs[0].user_data)->val == 0x1812);
    check_chrc(1, 0x2a4a, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_hids_info, NULL);
    assert(hog_svc.attrs[2].user_data == &info);
    check_chrc(3, 0x2a4b, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_map, NULL);
    check_stock_input(5, &input, read_hids_input_report);
    check_stock_input(9, &consumer_input, read_hids_consumer_input_report);
    size_t next = 13U;
#if CONFIG_ZMK_POINTING
    check_stock_input(13, &mouse_input, read_hids_mouse_input_report);
    next += 4U;
#if CONFIG_ZMK_POINTING_SMOOTH_SCROLLING
    check_chrc(next, 0x2a4d, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                read_hids_mouse_feature_report, write_hids_mouse_feature_report);
    assert(hog_svc.attrs[next+2U].user_data == &mouse_feature);
    next += 3U;
#endif
#endif
#if CONFIG_ZMK_HID_INDICATORS
    check_chrc(next, 0x2a4d, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, NULL, write_hids_leds_report);
    assert(hog_svc.attrs[next+2U].user_data == &led_indicators);
    next += 3U;
#endif
    check_chrc(next, 0x2a4c, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                BT_GATT_PERM_WRITE, NULL, write_ctrl_point);
    assert(hog_svc.attrs[next+1U].user_data == &ctrl_point);
    next += 2U;
    assert(hog_svc.attr_count == next + 4U); /* Derived from REAL macro expansion. */
    check_chrc(next, 0x2a4d, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                BT_GATT_PERM_READ_ENCRYPT, bbp_gamepad_ble_read_report, NULL);
    assert(gamepad_value() == &hog_svc.attrs[next+1U]);
    assert(gamepad_ccc()->uuid->val == 0x2902);
    assert(gamepad_ccc()->perm == (BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));
    assert(gamepad_ccc()->read == bt_gatt_attr_read_ccc && gamepad_ccc()->write == bt_gatt_attr_write_ccc);
    struct _bt_gatt_ccc *storage = gamepad_ccc()->user_data;
    assert(storage && storage->cfg_changed == bbp_gamepad_ble_ccc_changed);
    assert(!storage->cfg_write && !storage->cfg_match);
    for (size_t i = 0; i < next; ++i) {
        if (hog_svc.attrs[i].uuid->val == 0x2902) { assert(hog_svc.attrs[i].user_data != storage); }
    }
    const struct bt_gatt_attr *ref = &hog_svc.attrs[next+3U];
    assert(ref->uuid->val == 0x2908 && ref->perm == BT_GATT_PERM_READ_ENCRYPT && !ref->write);
    uint8_t out[3] = {0xaaU,0xaaU,0xaaU};
    assert(ref->read(&peers[0], ref, out, sizeof(out), 0) == 2 && out[0] == 4U && out[1] == 1U);
    assert(out[2] == 0xaaU && ref->read(&peers[0], ref, out, 3, 1) == 1 && out[0] == 1U);
}

static void test_tail_contract_fails_closed(void) {
    reset_case();
    assert(bbp_gamepad_ble_value_attr(&hog_svc) == gamepad_value());
    struct bt_gatt_attr tail[4];
    memcpy(tail, &hog_svc.attrs[hog_svc.attr_count - 4U], sizeof(tail));
    struct bt_gatt_service_static service = {.attrs = tail, .attr_count = 4U};
    assert(bbp_gamepad_ble_value_attr(&service) == &tail[1]);
    assert(!bbp_gamepad_ble_value_attr(NULL));
    for (size_t count = 0; count < 4U; ++count) {
        service.attr_count = count;
        assert(!bbp_gamepad_ble_value_attr(&service));
    }
    service.attr_count = 4U;
    service.attrs = NULL;
    assert(!bbp_gamepad_ble_value_attr(&service));
    service.attrs = tail;

    struct bt_gatt_attr value = tail[1], reference = tail[3];
    tail[1].read = NULL;
    assert(!bbp_gamepad_ble_value_attr(&service));
    tail[1].read = read_hids_input_report; /* A stock value is not our tail. */
    assert(!bbp_gamepad_ble_value_attr(&service));
    tail[1] = value;
    tail[1].perm = BT_GATT_PERM_READ;
    assert(!bbp_gamepad_ble_value_attr(&service));
    tail[1].perm = BT_GATT_PERM_WRITE_ENCRYPT;
    assert(!bbp_gamepad_ble_value_attr(&service));
    tail[1] = value;
    tail[3].read = read_hids_report_ref;
    assert(!bbp_gamepad_ble_value_attr(&service));
    tail[3] = reference;
    uint8_t duplicate_ref[2] = {4U,1U};
    tail[3].user_data = duplicate_ref; /* Equal bytes alone do not prove identity. */
    assert(!bbp_gamepad_ble_value_attr(&service));
    tail[3].user_data = NULL;
    assert(!bbp_gamepad_ble_value_attr(&service));
    tail[3] = reference;
    tail[1].perm |= BT_GATT_PERM_READ; /* Require the encryption bit, not exact equality. */
    assert(bbp_gamepad_ble_value_attr(&service) == &tail[1]);
    assert(!notify_count && !subscription_checks && !active_gets);
}

static void test_report_map_all_offsets_crossing_and_end(void) {
    reset_case();
    uint8_t expected[sizeof(zmk_hid_report_desc) + sizeof(bbp_gamepad_ble_descriptor)];
    memcpy(expected, zmk_hid_report_desc, sizeof(zmk_hid_report_desc));
    memcpy(expected + sizeof(zmk_hid_report_desc), bbp_gamepad_ble_descriptor,
           sizeof(bbp_gamepad_ble_descriptor));
    const uint16_t lengths[] = {0U,1U,2U,7U,22U,45U,255U};
    uint8_t out[512];
    for (size_t offset = 0; offset <= sizeof(expected) + 1U; ++offset) {
        for (size_t l = 0; l < ARRAY_SIZE(lengths); ++l) {
            memset(out, 0xa5, sizeof(out));
            ssize_t got = hog_svc.attrs[4].read(&peers[0], &hog_svc.attrs[4], out,
                                               lengths[l], (uint16_t)offset);
            if (offset > sizeof(expected)) {
                assert(got == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
                assert(out[0] == 0xa5U);
            } else {
                size_t count = sizeof(expected) - offset;
                if (count > lengths[l]) { count = lengths[l]; }
                assert(got == (ssize_t)count && memcmp(out, expected + offset, count) == 0);
                assert(out[count] == 0xa5U);
            }
        }
    }
    assert(hog_svc.attrs[4].read(NULL, &hog_svc.attrs[4], out, 10, UINT16_MAX) ==
           BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
    assert(raw_reads == 0U); /* Actual stock map callback was intercepted, not other reads. */
    assert(hog_svc.attrs[4].read(NULL, &hog_svc.attrs[4], out, sizeof(out), 0) == sizeof(expected));
    assert(memcmp(out, zmk_hid_report_desc, sizeof(zmk_hid_report_desc)) == 0);
    assert(memcmp(out + sizeof(zmk_hid_report_desc), bbp_gamepad_ble_descriptor,
                  sizeof(bbp_gamepad_ble_descriptor)) == 0);
}

static void test_map_identity_and_stock_reads_passthrough(void) {
    reset_case();
    uint8_t copied_desc[sizeof(zmk_hid_report_desc)], out[128];
    memcpy(copied_desc, zmk_hid_report_desc, sizeof(copied_desc));
    assert(bbp_gamepad_ble_attr_read(&peers[0], &hog_svc.attrs[4], out, sizeof(out), 0,
                                    copied_desc, sizeof(copied_desc)) == sizeof(copied_desc));
    assert(raw_reads == 1U && memcmp(out, copied_desc, sizeof(copied_desc)) == 0);
    assert(bbp_gamepad_ble_attr_read(NULL, &hog_svc.attrs[4], out, sizeof(out), 0,
                                    zmk_hid_report_desc, sizeof(zmk_hid_report_desc)-1U) ==
           sizeof(zmk_hid_report_desc)-1U);
    assert(raw_reads == 2U);
    assert(hog_svc.attrs[2].read(&peers[0], &hog_svc.attrs[2], out, sizeof(out), 0) == sizeof(info));
    assert(memcmp(out, &info, sizeof(info)) == 0);
    assert(hog_svc.attrs[6].read(&peers[0], &hog_svc.attrs[6], out, sizeof(out), 0) == sizeof(keyboard.body));
    assert(memcmp(out, &keyboard.body, sizeof(keyboard.body)) == 0);
    assert(hog_svc.attrs[10].read(&peers[0], &hog_svc.attrs[10], out, sizeof(out), 1) == sizeof(consumer.body)-1U);
    assert(memcmp(out, (uint8_t *)&consumer.body + 1, sizeof(consumer.body)-1U) == 0);
#if CONFIG_ZMK_POINTING
    assert(hog_svc.attrs[14].read(&peers[0], &hog_svc.attrs[14], out, sizeof(out), 0) == sizeof(mouse.body));
    assert(memcmp(out, &mouse.body, sizeof(mouse.body)) == 0);
#endif
}

static void test_read_nine_byte_body_and_offsets(void) {
    reset_case();
    const uint8_t expected[9] = {0xc3U,0x0cU,3U,0x81U,0x7fU,0xb7U,0x2aU,0xffU,0xffU};
    assert(sizeof(current_report) == 9U && sizeof(current_report) * 8U == 72U);
    assert(memcmp(&current_report, expected, sizeof(expected)) == 0);
    const uint16_t lengths[] = {0U,1U,2U,7U,8U,9U,11U};
    uint8_t out[12];
    for (uint16_t offset = 0U; offset <= 10U; ++offset) {
        for (size_t l = 0U; l < ARRAY_SIZE(lengths); ++l) {
            memset(out, 0xaa, sizeof(out));
            ssize_t got = gamepad_value()->read(&peers[0], gamepad_value(), out,
                                                lengths[l], offset);
            if (offset > 9U) {
                assert(got == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET) && out[0] == 0xaaU);
            } else {
                size_t count = 9U - offset;
                if (count > lengths[l]) { count = lengths[l]; }
                assert(got == (ssize_t)count && memcmp(out, expected + offset, count) == 0);
                assert(out[count] == 0xaaU);
            }
        }
    }
    memset(out, 0xaa, sizeof(out));
    assert(gamepad_value()->read(&peers[0], gamepad_value(), out, sizeof(out), 0) == 9);
    assert(out[0] == 0xc3U && out[7] == 255U && out[8] == 255U && out[9] == 0xaaU);
    assert(gamepad_value()->read(&peers[0], gamepad_value(), out, 9U, UINT16_MAX) ==
           BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
    /* Raw-wire samples only: physical shoulder triggers emit 0 or 255. */
    current_report.lt = 0x12U; current_report.rt = 0xe7U;
    assert(gamepad_value()->read(&peers[0], gamepad_value(), out, sizeof(out), 7U) == 2);
    assert(out[0] == 0x12U && out[1] == 0xe7U);
    assert(gamepad_value()->read(&peers[0], gamepad_value(), out, 1U, 8U) == 1 && out[0] == 0xe7U);
    report_available = false; /* Equivalent to getter outside MEDIA. */
    assert_neutral_read(&peers[0]);
    balanced();
}

static void test_read_wrong_peer_route_and_reconnected_object(void) {
    reset_case();
    assert_neutral_read(&peers[1]);
    assert_neutral_read(&peers[2]); /* Same profile is not enough: pointer must match. */
    assert_neutral_read(NULL);
    assert(!report_gets);
    selected.transport = ZMK_TRANSPORT_USB;
    assert_neutral_read(&peers[0]);
    selected.transport = ZMK_TRANSPORT_BLE;
    selected.ble.profile_index = 1;
    assert_neutral_read(&peers[0]);
    selected.ble.profile_index = 0;
    active_connection = NULL;
    assert_neutral_read(&peers[0]);
    active_connection = &peers[0];
    peers[0].state = BT_CONN_STATE_DISCONNECTING;
    assert_neutral_read(&peers[0]);
    assert(!report_gets);
    balanced();
}

static void test_read_epoch_and_peer_changes_during_getter(void) {
    void (*hooks[])(void) = {bump_epoch, change_route, replace_connection, disconnect_active};
    for (size_t i = 0; i < ARRAY_SIZE(hooks); ++i) {
        reset_case();
        report_hook = hooks[i];
        assert_neutral_read(&peers[0]);
        assert(report_gets == 1U);
        balanced();
    }
    reset_case();
    active_hook = bump_epoch; /* Epoch changes BEFORE the runtime getter. */
    assert_neutral_read(&peers[0]);
    assert(!report_gets);
    balanced();
}

static void test_ccc_is_independent_and_per_peer(void) {
    reset_case();
    struct _bt_gatt_ccc *ccc = gamepad_ccc()->user_data;
    ccc->value = 0;
    memset(ccc->cfg, 0, sizeof(ccc->cfg));
    uint8_t enable[2] = {1U,0U}, disable[2] = {0U,0U};
    assert(gamepad_ccc()->write(&peers[1], gamepad_ccc(), enable, 2, 0, 0) == 2);
    assert(invalidations == 1U && !host_requests_notification);
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -EAGAIN);
    /* A stock CCC changes the OLD global flag but does not enable gamepad0. */
    const struct bt_gatt_attr *stock_ccc = &hog_svc.attrs[7];
    assert(stock_ccc->write(&peers[0], stock_ccc, enable, 2, 0, 0) == 2);
    assert(host_requests_notification && invalidations == 1U);
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -EAGAIN);
    assert(gamepad_ccc()->write(&peers[1], gamepad_ccc(), disable, 2, 0, 0) == 2);
    assert(host_requests_notification && invalidations == 2U && !notify_count);
}

static void test_send_value_body_and_caller_reference(void) {
    reset_case();
    struct bt_conn *captured = bt_conn_ref(&peers[0]);
    unsigned int before_ref = ref_calls, before_unref = unref_calls;
    struct bbp_gamepad_report_body original = current_report;
    assert(bbp_gamepad_ble_send(captured, &current_report, context_epoch) == 0);
    assert(notify_count == 1U && notifications[0].conn == captured);
    assert(notifications[0].attr == gamepad_value() && notifications[0].len == 9U);
    assert(memcmp(notifications[0].bytes, &original, 9U) == 0);
    assert(memcmp(&current_report, &original, 9U) == 0);
    assert(notifications[0].bytes[7] == 255U && notifications[0].bytes[8] == 255U);
    assert(peers[0].refs == 2); /* Worker reference is untouched. */
    assert(ref_calls == before_ref + 1U && unref_calls == before_unref + 1U);
    assert(!security_calls && !submits && !schedules);
    bt_conn_unref(captured);
    balanced();
}

static void test_send_refuses_unsubscribed_or_not_connected(void) {
    reset_case();
    assert(bbp_gamepad_ble_send(&peers[0], NULL, context_epoch) == -EINVAL);
    assert(bbp_gamepad_ble_send(NULL, &current_report, context_epoch) == -ENOTCONN);
    struct _bt_gatt_ccc *ccc = gamepad_ccc()->user_data;
    ccc->cfg[0].value = 0;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -EAGAIN);
    assert(!active_gets && !notify_count);
    ccc->cfg[0].value = BT_GATT_CCC_NOTIFY;
    peers[0].state = BT_CONN_STATE_DISCONNECTED;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -ENOTCONN);
    peers[0].state = BT_CONN_STATE_CONNECTING;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -ENOTCONN);
    peers[0].state = BT_CONN_STATE_CONNECTED;
    peers[0].info_error = -EIO;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -ENOTCONN);
    assert(!notify_count && !security_calls);
}

static void test_trigger_only_requires_current_context_and_peer(void) {
    /* Only transport probes use intermediate values; hardware shoulders are digital. */
    static const uint8_t trigger_bytes[][2] = {
        {255U,0U}, {0U,255U}, {255U,255U}, {1U,0U}, {0U,128U}, {0x12U,0xe7U},
    };
    for (size_t i = 0U; i < ARRAY_SIZE(trigger_bytes); ++i) {
        reset_case();
        struct bbp_gamepad_report_body body = bbp_gamepad_neutral();
        body.lt = trigger_bytes[i][0]; body.rt = trigger_bytes[i][1];
        assert(!body.buttons_lo && !body.buttons_hi && !body.x && !body.y && !body.z &&
               !body.rz && body.hat == 8U && !bbp_gamepad_is_neutral(&body));
        struct bt_conn *captured = bt_conn_ref(&peers[0]);
        assert(bbp_gamepad_ble_send(captured, &body, context_epoch - 1U) == -ESTALE);
        assert(!notify_count);
        assert(bbp_gamepad_ble_send(captured, &body, context_epoch) == 0);
        assert(notify_count == 1U && notifications[0].conn == captured &&
               notifications[0].len == 9U && !memcmp(notifications[0].bytes, &body, 9U));
        memset(&body, 0xCC, sizeof(body)); /* Notify mock copied the complete caller-owned body. */
        assert(notifications[0].bytes[7] == trigger_bytes[i][0] &&
               notifications[0].bytes[8] == trigger_bytes[i][1]);
        body = bbp_gamepad_neutral();
        body.lt = trigger_bytes[i][0]; body.rt = trigger_bytes[i][1];
        active_connection = &peers[1]; active_profile = selected.ble.profile_index = 1;
        assert(bbp_gamepad_ble_send(captured, &body, context_epoch) == -ESTALE);
        selected.transport = ZMK_TRANSPORT_USB;
        assert(bbp_gamepad_ble_send(captured, &body, context_epoch) == -ESTALE);
        assert(notify_count == 1U && !security_calls && !submits && !schedules);
        bt_conn_unref(captured);
        balanced();
    }
}

static void test_send_rechecks_epoch_and_active_before_submission(void) {
    reset_case();
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch-1U) == -ESTALE);
    subscription_hook = bump_epoch;
    uint32_t epoch = context_epoch;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, epoch) == -ESTALE);
    active_hook = bump_epoch;
    epoch = context_epoch;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, epoch) == -ESTALE);
    active_connection = &peers[2]; /* Same profile/address, different connection object. */
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -ESTALE);
    active_connection = &peers[0];
    selected.transport = ZMK_TRANSPORT_USB;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -ESTALE);
    selected.transport = ZMK_TRANSPORT_BLE;
    selected.ble.profile_index = 1;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -ESTALE);
    selected.ble.profile_index = 0;
    no_dst = true;
    assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == -ESTALE);
    assert(!notify_count && !security_calls);
    balanced();
}

static void test_neutral_releases_only_old_captured_peer(void) {
    reset_case();
    struct bt_conn *captured = bt_conn_ref(&peers[0]);
    uint32_t old_epoch = context_epoch++;
    active_connection = &peers[1];
    active_profile = selected.ble.profile_index = 1;
    selected.transport = ZMK_TRANSPORT_USB; /* Old release does not require BLE to be selected. */
    assert(bbp_gamepad_ble_send(captured, &current_report, old_epoch) == -ESTALE);
    unsigned int getters = active_gets, refs = ref_calls, unrefs = unref_calls;
    struct bbp_gamepad_report_body neutral = bbp_gamepad_neutral();
    const uint8_t neutral_bytes[9] = {0U,0U,8U,0U,0U,0U,0U,0U,0U};
    assert(bbp_gamepad_ble_send(captured, &neutral, old_epoch) == 0);
    assert(notify_count == 1U && notifications[0].conn == captured &&
           notifications[0].len == 9U && !memcmp(notifications[0].bytes, neutral_bytes, 9U));
    assert(active_gets == getters && ref_calls == refs && unref_calls == unrefs);
    assert(peers[0].refs == 2 && peers[1].refs == 1);
    /* New peer1's subscription must not authorize neutral to unsubscribed peer0. */
    struct _bt_gatt_ccc *ccc = gamepad_ccc()->user_data;
    ccc->cfg[0].value = 0U;
    assert(ccc->cfg[1].value == BT_GATT_CCC_NOTIFY);
    assert(bbp_gamepad_ble_send(captured, &neutral, old_epoch) == -EAGAIN && notify_count == 1U);
    ccc->cfg[0].value = BT_GATT_CCC_NOTIFY;
    /* Mock SDK security rejection passes through even for old-context neutral;
     * this models an error, NOT real ATT encryption or radio validation. */
    notify_error = -EPERM;
    assert(bbp_gamepad_ble_send(captured, &neutral, old_epoch) == -EPERM);
    assert(notify_count == 2U && notifications[1].conn == captured &&
           notifications[1].attr == gamepad_value() && notifications[1].len == 9U &&
           !memcmp(notifications[1].bytes, neutral_bytes, 9U));
    assert(!security_calls && !submits && !schedules);
    assert(active_gets == getters && ref_calls == refs && unref_calls == unrefs);
    notify_error = 0;
    assert(bbp_gamepad_ble_send(NULL, &neutral, old_epoch) == -ENOTCONN);
    peers[0].state = BT_CONN_STATE_DISCONNECTED;
    assert(bbp_gamepad_ble_send(captured, &neutral, old_epoch) == -ENOTCONN);
    assert(notify_count == 2U); /* No fallback to the new active peer. */
    bt_conn_unref(captured);
    balanced();
}

static void test_zero_filled_is_not_neutral_and_errors_are_passthrough(void) {
    reset_case();
    struct bbp_gamepad_report_body wrong_neutral = {0}; /* Hat0 means UP. */
    assert(bbp_gamepad_ble_send(&peers[0], &wrong_neutral, context_epoch-1U) == -ESTALE);
    const int errors[] = {-EPERM,-EACCES,-ENOMEM,-EAGAIN,-EIO};
    for (size_t i = 0; i < ARRAY_SIZE(errors); ++i) {
        notify_error = errors[i];
        unsigned int sent = notify_count;
        assert(bbp_gamepad_ble_send(&peers[0], &current_report, context_epoch) == errors[i]);
        assert(notify_count == sent + 1U); /* One attempt, no internal retry. */
        assert(!security_calls && !submits && !schedules);
    }
    balanced();
}

static void test_original_stock_senders_remain(void) {
    reset_case();
    assert(gamepad_mock_hog_init() == 0);
    struct zmk_hid_keyboard_report_body kb = keyboard.body;
    struct zmk_hid_consumer_report_body co = consumer.body;
    assert(zmk_hog_send_keyboard_report(&kb) == 0);
    assert(zmk_hog_send_consumer_report(&co) == 0);
    memset(&kb, 0, sizeof(kb));
    memset(&co, 0, sizeof(co));
    send_keyboard_report_callback(&hog_keyboard_work);
    send_consumer_report_callback(&hog_consumer_work);
    assert(notifications[0].attr == &hog_svc.attrs[5]);
    assert(notifications[0].len == sizeof(keyboard.body));
    assert(memcmp(notifications[0].bytes, &keyboard.body, sizeof(keyboard.body)) == 0);
    assert(notifications[1].attr == &hog_svc.attrs[9]);
    assert(notifications[1].len == sizeof(consumer.body));
    assert(memcmp(notifications[1].bytes, &consumer.body, sizeof(consumer.body)) == 0);
#if CONFIG_ZMK_POINTING
    struct zmk_hid_mouse_report_body mo = mouse.body;
#if BBP_GAMEPAD_TEST_WITH_SNAPSHOT
    assert(bbp9981_stock_hog_send_mouse_report(&mo) == 0);
#else
    assert(zmk_hog_send_mouse_report(&mo) == 0);
#endif
    memset(&mo, 0, sizeof(mo));
    send_mouse_report_callback(&hog_mouse_work);
    assert(notifications[2].attr == &hog_svc.attrs[13]);
    assert(notifications[2].len == sizeof(mouse.body));
    assert(memcmp(notifications[2].bytes, &mouse.body, sizeof(mouse.body)) == 0);
#endif
    notify_error = -EPERM;
    assert(zmk_hog_send_keyboard_report(&keyboard.body) == 0);
    send_keyboard_report_callback(&hog_keyboard_work);
    assert(security_calls == 1U); /* Original stock security handling, not replaced. */
    balanced();
}

#if BBP_GAMEPAD_TEST_WITH_SNAPSHOT && CONFIG_ZMK_POINTING
static void test_existing_mouse_snapshot_uses_original_index13(void) {
    reset_case();
    struct zmk_hid_mouse_report_body moving = mouse.body;
    assert(bbp9981_ble_hid_send_mouse_buttons_snapshot(&moving, 0) == 0);
    assert(zmk_hog_send_mouse_report(&moving) == 0);
    bbp9981_hog_mouse_worker(&bbp9981_hog_mouse_work.work);
    bbp9981_hog_mouse_worker(&bbp9981_hog_mouse_work.work);
    assert(notify_count == 2U && notifications[0].attr == &hog_svc.attrs[13]);
    assert(notifications[1].attr == &hog_svc.attrs[13]);
    struct zmk_hid_mouse_report_body snapshot;
    memcpy(&snapshot, notifications[0].bytes, sizeof(snapshot));
    assert(snapshot.buttons == moving.buttons && !snapshot.d_x && !snapshot.d_y &&
           !snapshot.d_scroll_y && !snapshot.d_scroll_x);
    assert(memcmp(notifications[1].bytes, &moving, sizeof(moving)) == 0);
    assert(!bbp9981_hog_mouse_stats.outstanding);
    balanced();
}
#endif

int main(void) {
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) { peers[i].refs = 1; }
#define RUN(test) do { test(); balanced(); puts("PASS " #test); } while (0)
    RUN(test_pinned_service_old_attrs_and_tail);
    RUN(test_tail_contract_fails_closed);
    RUN(test_report_map_all_offsets_crossing_and_end);
    RUN(test_map_identity_and_stock_reads_passthrough);
    RUN(test_read_nine_byte_body_and_offsets);
    RUN(test_read_wrong_peer_route_and_reconnected_object);
    RUN(test_read_epoch_and_peer_changes_during_getter);
    RUN(test_ccc_is_independent_and_per_peer);
    RUN(test_send_value_body_and_caller_reference);
    RUN(test_send_refuses_unsubscribed_or_not_connected);
    RUN(test_trigger_only_requires_current_context_and_peer);
    RUN(test_send_rechecks_epoch_and_active_before_submission);
    RUN(test_neutral_releases_only_old_captured_peer);
    RUN(test_zero_filled_is_not_neutral_and_errors_are_passthrough);
    RUN(test_original_stock_senders_remain);
#if BBP_GAMEPAD_TEST_WITH_SNAPSHOT && CONFIG_ZMK_POINTING
    RUN(test_existing_mouse_snapshot_uses_original_index13);
#endif
    printf("PASS gamepad BLE: real pinned HOG TU, %zu attrs, 9-byte/72-bit body, active sticks+LT/RT, balanced refs; "
           "snapshot=%d smooth=%d indicators=%d; HOST ONLY, no radio/Android validation\n",
           hog_svc.attr_count, BBP_GAMEPAD_TEST_WITH_SNAPSHOT,
           CONFIG_ZMK_POINTING_SMOOTH_SCROLLING, CONFIG_ZMK_HID_INDICATORS);
    return 0;
}
