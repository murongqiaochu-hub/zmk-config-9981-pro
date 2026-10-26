/* SPDX-License-Identifier: MIT
 * Isolated pure-gamepad GATT prototype. NOT linked by production CMake.
 * No advertising, identity, pairing mutation, notification worker or mode switch.
 * Integration must provide peer/epoch-safe snapshots and per-peer suspend state.
 */
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/gatt.h>
#include "stadia_descriptor.h"
#include "stadia_codec.h"

/* Diagnostics carry operation/offset/result, never button payloads or peer IDs.
 * CCC change is aggregate, and must NOT be interpreted as per-peer subscription.
 */
enum bbp_exp_event {
    BBP_EXP_MAP_READ, BBP_EXP_INPUT_READ, BBP_EXP_OUTPUT_WRITE,
    BBP_EXP_CONTROL_WRITE, BBP_EXP_PROTOCOL_WRITE, BBP_EXP_CCC_AGGREGATE
};
void bbp_exp_observe(enum bbp_exp_event event, uint16_t offset, int result);
bool bbp_exp_capture_report(struct bt_conn *conn, struct bbp_gamepad_report_body *out);
void bbp_exp_set_suspended(struct bt_conn *conn, bool suspended);

static uint8_t bbp_exp_input_ref[] = {BBP_EXP_INPUT_ID, 1U};
static uint8_t bbp_exp_output_ref[] = {BBP_EXP_OUTPUT_ID, 2U};
static const uint8_t bbp_exp_hid_info[] = {0x01U, 0x01U, 0U, 3U};

static ssize_t bbp_exp_read_info(struct bt_conn *c, const struct bt_gatt_attr *a,
                                void *buf, uint16_t len, uint16_t off) {
    return bt_gatt_attr_read(c, a, buf, len, off, bbp_exp_hid_info, sizeof(bbp_exp_hid_info));
}
static ssize_t bbp_exp_read_map(struct bt_conn *c, const struct bt_gatt_attr *a,
                               void *buf, uint16_t len, uint16_t off) {
    ssize_t ret = bt_gatt_attr_read(c, a, buf, len, off, bbp_exp_report_map,
                                   sizeof(bbp_exp_report_map));
    bbp_exp_observe(BBP_EXP_MAP_READ, off, (int)ret);
    return ret;
}
static ssize_t bbp_exp_read_ref(struct bt_conn *c, const struct bt_gatt_attr *a,
                               void *buf, uint16_t len, uint16_t off) {
    return bt_gatt_attr_read(c, a, buf, len, off, a->user_data, 2U);
}
static ssize_t bbp_exp_read_input(struct bt_conn *c, const struct bt_gatt_attr *a,
                                 void *buf, uint16_t len, uint16_t off) {
    struct bbp_gamepad_report_body state = bbp_gamepad_neutral();
    uint8_t bytes[BBP_EXP_INPUT_SIZE];
    if (!c || !bbp_exp_capture_report(c, &state)) { state = bbp_gamepad_neutral(); }
    (void)bbp_experiment_stadia_encode(&state, bytes);
    ssize_t ret = bt_gatt_attr_read(c, a, buf, len, off, bytes, sizeof(bytes));
    bbp_exp_observe(BBP_EXP_INPUT_READ, off, (int)ret);
    return ret;
}
static ssize_t bbp_exp_read_output(struct bt_conn *c, const struct bt_gatt_attr *a,
                                  void *buf, uint16_t len, uint16_t off) {
    /* No physical actuators. Do not retain a different peer's last command. */
    const uint8_t zero[BBP_EXP_OUTPUT_SIZE] = {0};
    return bt_gatt_attr_read(c, a, buf, len, off, zero, sizeof(zero));
}
/* Pinned Zephyr flags: PREPARE=0x01, CMD=0x02, EXECUTE=0x04.
 * Reports/control are atomic here; no queued-write state exists.
 */
static int bbp_exp_check_write(const void *buf, uint16_t len, uint16_t off,
                               uint16_t expected, uint8_t flags) {
    if (flags & (uint8_t)~0x02U) { return BT_GATT_ERR(0x06); }
    if (off) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    if (len != expected) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN); }
    if (!buf) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    return len;
}
static ssize_t bbp_exp_write_output(struct bt_conn *c, const struct bt_gatt_attr *a,
                                   const void *buf, uint16_t len, uint16_t off, uint8_t flags) {
    (void)c; (void)a;
    int ret = bbp_exp_check_write(buf, len, off, BBP_EXP_OUTPUT_SIZE, flags);
    bbp_exp_observe(BBP_EXP_OUTPUT_WRITE, off, ret);
    return ret; /* Valid rumble report accepted but deliberately ignored. */
}
static ssize_t bbp_exp_read_protocol(struct bt_conn *c, const struct bt_gatt_attr *a,
                                    void *buf, uint16_t len, uint16_t off) {
    const uint8_t report_protocol = 1U;
    return bt_gatt_attr_read(c, a, buf, len, off, &report_protocol, 1U);
}
static ssize_t bbp_exp_write_protocol(struct bt_conn *c, const struct bt_gatt_attr *a,
                                     const void *buf, uint16_t len, uint16_t off, uint8_t flags) {
    (void)c; (void)a;
    int ret = bbp_exp_check_write(buf, len, off, 1U, flags);
    if (ret == 1 && *(const uint8_t *)buf != 1U) { ret = BT_GATT_ERR(0x13); }
    bbp_exp_observe(BBP_EXP_PROTOCOL_WRITE, off, ret);
    return ret; /* Report-only device; never claim Boot protocol support. */
}
static ssize_t bbp_exp_write_control(struct bt_conn *c, const struct bt_gatt_attr *a,
                                    const void *buf, uint16_t len, uint16_t off, uint8_t flags) {
    (void)a;
    int ret = bbp_exp_check_write(buf, len, off, 1U, flags);
    if (ret == 1 && *(const uint8_t *)buf > 1U) { ret = BT_GATT_ERR(0x13); }
    if (!c && ret == 1) { ret = BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    if (ret == 1) { bbp_exp_set_suspended(c, *(const uint8_t *)buf == 0U); }
    bbp_exp_observe(BBP_EXP_CONTROL_WRITE, off, ret);
    return ret;
}
static void bbp_exp_ccc_changed(const struct bt_gatt_attr *a, uint16_t value) {
    (void)a;
    bbp_exp_observe(BBP_EXP_CCC_AGGREGATE, 0U, value);
}

BT_GATT_SERVICE_DEFINE(bbp_exp_hids,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(0x2a4e),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
        bbp_exp_read_protocol, bbp_exp_write_protocol, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ_ENCRYPT, bbp_exp_read_info, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ_ENCRYPT, bbp_exp_read_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ_ENCRYPT, bbp_exp_read_input, NULL, NULL),
    BT_GATT_CCC(bbp_exp_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT,
        bbp_exp_read_ref, NULL, bbp_exp_input_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
        bbp_exp_read_output, bbp_exp_write_output, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT,
        bbp_exp_read_ref, NULL, bbp_exp_output_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE_ENCRYPT, NULL, bbp_exp_write_control, NULL));
