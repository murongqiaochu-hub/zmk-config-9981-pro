/* SPDX-License-Identifier: MIT
 * Read-only diagnostic observer. No payload/keycode/address/key material logs;
 * no GATT registration, security requests, report edits or settings mutations.
 * SDK queries and deferred logs are ALWAYS outside the short IRQ lock.
 */
#include <inttypes.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include "ipad_diag.h"
#include "ipad_diag_core.h"
#if !defined(CONFIG_LOG_MODE_DEFERRED) || !CONFIG_LOG_MODE_DEFERRED
#error "IPDIAG requires deferred logging"
#endif
#if defined(CONFIG_LOG_BLOCK_IN_THREAD) && CONFIG_LOG_BLOCK_IN_THREAD
#error "IPDIAG must never block a producer for log buffer space"
#endif
LOG_MODULE_REGISTER(ipad_diag, LOG_LEVEL_INF);
extern const struct bt_gatt_service_static hog_svc;
static struct ipdiag_ring ring;
static struct ipdiag_rate tx[CONFIG_BT_MAX_CONN][5];
static uint32_t epochs[CONFIG_BT_MAX_CONN], suppressed, input_events;
static struct k_work_delayable tick_work;
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* Original pinned service indices: values 6/10/14, GP tail value23.
 * Guard count and UUID; diagnostics fail unknown, never change a report. */
static const struct bt_gatt_attr *report_value(unsigned id) {
    static const uint8_t index[] = {0, 6, 10, 14, 23};
    if (!id || id > 4 || hog_svc.attr_count != 26U) { return NULL; }
    const struct bt_gatt_attr *a = &hog_svc.attrs[index[id]];
    return a->uuid && bt_uuid_cmp(a->uuid, BT_UUID_HIDS_REPORT) == 0 ? a : NULL;
}
static unsigned report_id(const struct bt_gatt_attr *attr) {
    for (unsigned id = 1; id <= 4; ++id) {
        const struct bt_gatt_attr *value = report_value(id);
        if (value && (attr == value || attr == value - 1)) { return id; }
    }
    return 0;
}
static uint8_t conn_index(struct bt_conn *conn) {
    return conn ? bt_conn_index(conn) : UINT8_MAX;
}
static void note(uint8_t kind, struct bt_conn *conn, int32_t a, int32_t b,
                 int32_t d, int32_t e, int32_t f, int32_t g) {
    uint8_t index = conn_index(conn);
    struct ipdiag_record record = {.kind=kind, .conn=index, .t=k_uptime_get_32(),
                                  .a=a, .b=b, .d=d, .e=e, .f=f, .g=g};
    unsigned key = irq_lock();
    record.epoch = index < CONFIG_BT_MAX_CONN ? epochs[index] : 0;
    (void)ipdiag_push(&ring, record);
    irq_unlock(key);
}
int bbp_ipdiag_notify(struct bt_conn *conn, struct bt_gatt_notify_params *params) {
    uint8_t index = conn_index(conn);
    unsigned id = params ? report_id(params->attr) : 0;
    unsigned key = irq_lock();
    uint32_t epoch = index < CONFIG_BT_MAX_CONN ? epochs[index] : 0;
    irq_unlock(key);
    /* Same original call, same parameters/callbacks/ownership, exactly once. */
    int result = bt_gatt_notify_cb(conn, params);
    if (index < CONFIG_BT_MAX_CONN && id) {
        uint32_t now = k_uptime_get_32(), calls;
        key = irq_lock();
        if (epochs[index] != epoch) {
            /* A slow SDK call must never charge the reused slot's new epoch. */
            suppressed++;
            irq_unlock(key);
            return result;
        }
        bool emit = ipdiag_sample(&tx[index][id], now, result);
        calls = tx[index][id].calls;
        if (!emit) { suppressed++; }
        irq_unlock(key);
        if (emit) {
            const struct bt_gatt_attr *value = report_value(id);
            struct ipdiag_record record = {
                .kind=6, .conn=index, .epoch=epoch, .t=now, .a=id,
                .b=bt_gatt_attr_get_handle(value), .d=params->len,
                .e=result, .f=(int32_t)calls, .g=bt_conn_get_security(conn),
            };
            key = irq_lock();
            (void)ipdiag_push(&ring, record);
            irq_unlock(key);
        }
    }
    return result;
}
void bbp_ipdiag_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                    uint16_t requested, uint16_t offset, const void *value,
                    uint16_t total, ssize_t result, bool map) {
    if (!attr || !attr->uuid) { return; }
    if (map) {
        note(5, conn, 1, bt_gatt_attr_get_handle(attr), offset, requested, result, total);
    } else if (total == 2 && value && bt_uuid_cmp(attr->uuid, BT_UUID_HIDS_REPORT_REF) == 0) {
        /* Only standard Report Reference metadata; never an input report body. */
        const uint8_t *ref = value;
        note(5, conn, 2, bt_gatt_attr_get_handle(attr), offset, requested, result,
             (int32_t)ref[0] | ((int32_t)ref[1] << 8));
    }
}
static void connected(struct bt_conn *conn, uint8_t error) {
    uint8_t index = conn_index(conn);
    if (index < CONFIG_BT_MAX_CONN) {
        unsigned key = irq_lock();
        epochs[index]++;
        memset(tx[index], 0, sizeof(tx[index]));
        irq_unlock(key);
    }
    note(2, conn, 1, error, bt_conn_get_security(conn), 0, 0, 0);
}
static void disconnected(struct bt_conn *conn, uint8_t reason) {
    note(2, conn, 2, reason, bt_conn_get_security(conn), 0, 0, 0);
}
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err error) {
    note(2, conn, 3, error, level, 0, 0, 0);
}
BT_CONN_CB_DEFINE(ipad_diag) = {
    .connected = connected, .disconnected = disconnected, .security_changed = security_changed,
};
static int input_event(const zmk_event_t *event) {
    if (as_zmk_position_state_changed(event)) {
        unsigned key = irq_lock();
        input_events++; /* No key position, state, modifier, timing per key or payload. */
        irq_unlock(key);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(ipad_diag_input, input_event);
ZMK_SUBSCRIPTION(ipad_diag_input, zmk_position_state_changed);

static void snapshot(struct bt_conn *conn, void *unused) {
    (void)unused;
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) || info.state != BT_CONN_STATE_CONNECTED) { return; }
    uint8_t index = conn_index(conn);
    if (index >= CONFIG_BT_MAX_CONN) { return; }
    unsigned mask = 0;
    for (unsigned id = 1; id <= 4; ++id) {
        const struct bt_gatt_attr *value = report_value(id);
        bool subscribed = value && bt_gatt_is_subscribed(conn, value, BT_GATT_CCC_NOTIFY);
        if (subscribed) { mask |= 1U << (id - 1U); }
        unsigned key = irq_lock();
        uint32_t calls = tx[index][id].calls, errors = tx[index][id].errors;
        irq_unlock(key);
        note(4, conn, id, 1, value ? bt_gatt_attr_get_handle(value) : 0,
             value ? (int)subscribed : -1, (int32_t)calls, (int32_t)errors);
    }
    struct bt_conn *active = zmk_ble_active_profile_conn();
    bool is_active = active == conn;
    if (active) { bt_conn_unref(active); }
    note(3, conn, bt_conn_get_security(conn), bt_gatt_get_mtu(conn), mask,
         zmk_endpoints_selected().transport, zmk_ble_active_profile_index(), is_active);
}
static bool console_open(void) {
    uint32_t dtr = 0;
    return device_is_ready(console) && zmk_usb_is_hid_ready() &&
           uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0;
}
static void tick(struct k_work *work) {
    (void)work;
    static uint32_t last;
    static bool first = true;
    uint32_t now = k_uptime_get_32();
    if (first || (uint32_t)(now - last) >= 2000U) {
        first = false;
        last = now;
        /* Runs independently of MDIAG activation, even before CDC opens. */
        bt_conn_foreach(BT_CONN_TYPE_LE, snapshot, NULL);
        unsigned key = irq_lock();
        uint32_t dropped = ring.dropped, count = ring.count, skipped = suppressed, inputs = input_events;
        irq_unlock(key);
        note(1, NULL, (int32_t)dropped, (int32_t)count, (int32_t)skipped,
             (int32_t)inputs, 337, 64);
        if (console_open()) {
            LOG_INF("IPDIAG_ID v=1 build=" IPDIAG_BUILD " config=" IPDIAG_CONFIG_REV
                    " zmk=edf5c0814fd3ea202e43aad2d68fd32e882a518c"
                    " zephyr=dacab4875df72109b96cc8977547a0dc04875bcd");
        }
    }
    /* No SDK/log call in the lock. Closed CDC never drains to UART: the fixed
     * 64-slot ring drops new entries and counts them; no allocation or retry.
     * Deferred nonblocking logger may independently drop messages, detected by
     * seq gaps. DTR is sampled, not an atomic delivery guarantee. */
    for (unsigned n = 0; n < 4 && console_open(); ++n) {
        struct ipdiag_record r;
        unsigned key = irq_lock();
        bool have = ipdiag_pop(&ring, &r);
        irq_unlock(key);
        if (!have) { break; }
        LOG_INF("IPDIAG v=1 build=" IPDIAG_BUILD " t=%" PRIu32 " seq=%" PRIu32
                " k=%u c=%u ep=%" PRIu32 " a=%" PRId32 " b=%" PRId32
                " d=%" PRId32 " e=%" PRId32 " f=%" PRId32 " g=%" PRId32,
                r.t, r.seq, (unsigned)r.kind, (unsigned)r.conn, r.epoch,
                r.a, r.b, r.d, r.e, r.f, r.g);
    }
    k_work_reschedule(&tick_work, K_MSEC(250));
}
static int ipdiag_init(void) {
    k_work_init_delayable(&tick_work, tick);
    k_work_reschedule(&tick_work, K_MSEC(1000));
    return 0;
}
SYS_INIT(ipdiag_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
