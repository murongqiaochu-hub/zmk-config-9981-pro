/* SPDX-License-Identifier: MIT
 * Run via run_gamepad_ble_service_tests.py. Temporary fixture reuses archival
 * host plumbing and transport assertions, NEVER the legacy production adapter.
 * Actual unmodified pinned hog.c and new service compile into this same TU.
 */
#include "legacy_contract_fixture.h"

static void test_independent_service(void) {
    reset_case();
    const struct bt_gatt_attr *a = zz_bbp_gamepad_svc.attrs;
    assert(zz_bbp_gamepad_svc.attr_count == 11 && hog_svc.attr_count == 22);
    assert(a != hog_svc.attrs && a[0].uuid->val == 0x2800);
    assert(((const struct bt_uuid *)a[0].user_data)->val == 0x1812);
    assert(a[0].perm == BT_GATT_PERM_READ && a[0].read == bt_gatt_attr_read_service);
    const uint16_t uuids[] = {0x2a4a,0x2a4b,0x2a4c,0x2a4d};
    const uint8_t props[] = {BT_GATT_CHRC_READ, BT_GATT_CHRC_READ,
        BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY};
    for (size_t i = 0; i < 4; ++i) {
        const struct bt_gatt_attr *decl = &a[1 + i * 2], *value = decl + 1;
        const struct bt_gatt_chrc *chrc = decl->user_data;
        assert(decl->uuid->val == 0x2803 && decl->perm == BT_GATT_PERM_READ);
        assert(chrc->uuid->val == uuids[i] && chrc->properties == props[i]);
        assert(value->uuid->val == uuids[i]);
        assert(value->perm == (i == 2 ? BT_GATT_PERM_WRITE_ENCRYPT : BT_GATT_PERM_READ_ENCRYPT));
    }
    assert(!a[6].read && a[6].write == bbp_gamepad_ble_write_cp);
    assert(gamepad_value() == &a[8] && gamepad_ccc() == &a[9]);
    assert(a[9].perm == (BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));
    struct _bt_gatt_ccc *ccc = a[9].user_data;
    assert(ccc->cfg_changed == bbp_gamepad_ble_ccc_changed && !ccc->cfg_write && !ccc->cfg_match);
    for (size_t i = 0; i < hog_svc.attr_count; ++i) {
        if (hog_svc.attrs[i].uuid->val == 0x2902) { assert(hog_svc.attrs[i].user_data != ccc); }
    }
    uint8_t out[16];
    const uint8_t expected_info[] = {0x11,0x01,0,2};
    assert(a[2].read(&peers[0], &a[2], out, sizeof(out), 0) == 4);
    assert(!memcmp(out, expected_info, 4));
    assert(a[10].uuid->val == 0x2908 && a[10].perm == BT_GATT_PERM_READ_ENCRYPT && !a[10].write);
    memset(out, 0xaa, sizeof(out));
    assert(a[10].read(&peers[0], &a[10], out, sizeof(out), 0) == 2);
    assert(out[0] == 4 && out[1] == 1 && out[2] == 0xaa);
    assert(a[10].read(&peers[0], &a[10], out, 2, 1) == 1 && out[0] == 1);
    assert(a[10].read(&peers[0], &a[10], out, 2, 2) == 0);
    assert(a[10].read(&peers[0], &a[10], out, 2, UINT16_MAX) == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
}

static void check_map(const struct bt_gatt_attr *attr, const uint8_t *expected, size_t size) {
    const uint16_t lengths[] = {0,1,2,7,22,140,141,142,195,196,197,255,UINT16_MAX};
    uint8_t out[512];
    for (size_t offset = 0; offset <= size + 1; ++offset) {
        for (size_t i = 0; i < ARRAY_SIZE(lengths); ++i) {
            memset(out, 0xa5, sizeof(out));
            ssize_t got = attr->read(&peers[0], attr, out, lengths[i], offset);
            if (offset > size) {
                assert(got == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET) && out[0] == 0xa5);
            } else {
                size_t count = size - offset;
                if (count > lengths[i]) { count = lengths[i]; }
                assert(got == (ssize_t)count && !memcmp(out, expected + offset, count));
                assert(out[count] == 0xa5);
            }
        }
    }
    assert(attr->read(NULL, attr, out, UINT16_MAX, UINT16_MAX) == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
}
static void test_separate_real_maps_and_stock_reads(void) {
    reset_case();
    /* Independent literal from the original v2.2.1 audit, not production macro. */
    const char hex[] = "05010905a1018504050915002501350045005500650075011901290295028102950181031904290595028102950181031907290f950981029501810305010939150025073500463b0155006514750495018142750495018103350045005500650009300931093209351581257f750895048102050209c509c4150026ff003500450055006500750895028102c0";
    uint8_t expected[141];
    assert(strlen(hex) == sizeof(expected) * 2);
    for (size_t i = 0; i < sizeof(expected); ++i) {
        unsigned int byte;
        assert(sscanf(hex + 2*i, "%2x", &byte) == 1);
        expected[i] = byte;
    }
    assert(sizeof(zmk_hid_report_desc) == 196 && sizeof(bbp_gamepad_ble_descriptor) == 141);
    check_map(&hog_svc.attrs[4], zmk_hid_report_desc, 196);
    check_map(&zz_bbp_gamepad_svc.attrs[4], expected, 141);
    uint8_t out[256];
    assert(hog_svc.attrs[2].read(&peers[0], &hog_svc.attrs[2], out, sizeof(out), 0) == sizeof(info));
    assert(!memcmp(out, &info, sizeof(info)));
    assert(hog_svc.attrs[6].read(&peers[0], &hog_svc.attrs[6], out, sizeof(out), 0) == sizeof(keyboard.body));
    assert(!memcmp(out, &keyboard.body, sizeof(keyboard.body)));
    assert(hog_svc.attrs[10].read(&peers[0], &hog_svc.attrs[10], out, sizeof(out), 1) == sizeof(consumer.body)-1);
    assert(!memcmp(out, (uint8_t *)&consumer.body+1, sizeof(consumer.body)-1));
    assert(hog_svc.attrs[14].read(&peers[0], &hog_svc.attrs[14], out, sizeof(out), 0) == sizeof(mouse.body));
    assert(!memcmp(out, &mouse.body, sizeof(mouse.body)));
}
static ssize_t cp(struct bt_conn *conn, uint8_t value) {
    const struct bt_gatt_attr *a = &zz_bbp_gamepad_svc.attrs[6];
    return a->write(conn, a, &value, 1, 0, 0);
}
static void suspend_active(void) { assert(cp(&peers[0], 0) == 1); }

static void test_cp_validation_isolation_and_lifecycle(void) {
    reset_case();
    const struct bt_gatt_attr *a = &zz_bbp_gamepad_svc.attrs[6];
    struct bbp_gamepad_report_body body = current_report, neutral = bbp_gamepad_neutral();
    uint8_t data[2] = {0,0};
    for (unsigned int offset = 1; offset <= UINT16_MAX; ++offset) {
        assert(a->write(&peers[0], a, data, 1, offset, 0) == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
    }
    const uint16_t invalid_lengths[] = {0,2,9,UINT16_MAX};
    for (size_t i = 0; i < ARRAY_SIZE(invalid_lengths); ++i) {
        assert(a->write(&peers[0], a, data, invalid_lengths[i], 0, 0) == BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN));
    }
    for (unsigned int value = 2; value <= UINT8_MAX; ++value) {
        assert(cp(&peers[0], value) == BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED));
    }
    assert(cp(NULL, 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
    assert(a->write(&peers[0], a, NULL, 1, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED));
    peers[0].state = BT_CONN_STATE_DISCONNECTED;
    assert(cp(&peers[0], 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
    peers[0].state = BT_CONN_STATE_CONNECTED;
    struct bt_conn invalid_slot = {.state = BT_CONN_STATE_CONNECTED, .refs = 1};
    invalid_slot.dst.a.val[0] = CONFIG_BT_MAX_CONN;
    assert(cp(&invalid_slot, 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
    assert(bbp_gamepad_ble_send(&invalid_slot, &neutral, context_epoch) == -ENOTSUP);
    assert(!invalidations && !bbp_gamepad_ble_is_suspended(&peers[0]));
    uint8_t stock_cp = ctrl_point;
    assert(cp(&peers[1], 0) == 1 && !invalidations);
    assert(bbp_gamepad_ble_is_suspended(&peers[1]) && !bbp_gamepad_ble_is_suspended(&peers[0]));
    assert(bbp_gamepad_ble_send(&peers[1], &neutral, context_epoch) == -ENOTSUP);
    assert(bbp_gamepad_ble_send(&peers[0], &body, context_epoch) == 0);
    assert(cp(&peers[0], 0) == 1 && invalidations == 1);
    unsigned int checks = subscription_checks, gets = active_gets, sends = notify_count;
    assert(bbp_gamepad_ble_send(&peers[0], &body, context_epoch) == -ENOTSUP);
    assert(bbp_gamepad_ble_send(&peers[0], &neutral, context_epoch) == -ENOTSUP);
    assert(checks == subscription_checks && gets == active_gets && sends == notify_count);
    assert_neutral_read(&peers[0]);
    assert(ctrl_point == stock_cp && !host_requests_notification && !security_calls);
    assert(cp(&peers[1], 1) == 1 && invalidations == 1);
    assert(bbp_gamepad_ble_is_suspended(&peers[0]) && !bbp_gamepad_ble_is_suspended(&peers[1]));
    uint32_t old_epoch = context_epoch;
    assert(cp(&peers[0], 1) == 1 && invalidations == 3 && context_epoch != old_epoch);
    assert_neutral_read(&peers[0]);
    assert(bbp_gamepad_ble_send(&peers[0], &body, old_epoch) == -ESTALE);
    assert(bbp_gamepad_ble_send(&peers[0], &neutral, context_epoch) == 0);
    assert(cp(&peers[0], 0) == 1 && cp(&peers[1], 0) == 1);
    bbp_gamepad_ble_conn_callbacks.disconnected(&peers[0], 0x13);
    assert(!bbp_gamepad_ble_is_suspended(&peers[0]) && bbp_gamepad_ble_is_suspended(&peers[1]));
    assert(cp(&peers[0], 0) == 1);
    bbp_gamepad_ble_conn_callbacks.connected(&peers[0], 0);
    assert(!bbp_gamepad_ble_is_suspended(&peers[0]) && bbp_gamepad_ble_is_suspended(&peers[1]));
    assert(ctrl_point == stock_cp && !security_calls);
    /* Old HOG CP remains writable and does not control this HIDS. */
    data[0] = 0;
    const struct bt_gatt_attr *stock = &hog_svc.attrs[hog_svc.attr_count-1];
    assert(stock->write(&peers[0], stock, data, 1, 0, 0) == 1);
    assert(!bbp_gamepad_ble_is_suspended(&peers[0]));
    assert(bbp_gamepad_ble_send(&peers[0], &neutral, context_epoch) == 0);
}
static unsigned int exit_wakes;
static void exit_worker_race(void) {
    struct bbp_gamepad_report_body neutral = bbp_gamepad_neutral();
    assert(bbp_gamepad_is_neutral(&current_report));
    int rc = bbp_gamepad_ble_send(&peers[0], &neutral, context_epoch);
    assert(rc == (exit_wakes == 0 ? -ENOTSUP : 0));
    exit_wakes++;
    if (exit_wakes == 1) { invalidation_hook = exit_worker_race; }
}
static void test_exit_rearms_even_if_worker_runs_synchronously(void) {
    reset_case();
    assert(cp(&peers[0], 0) == 1);
    exit_wakes = 0;
    invalidation_hook = exit_worker_race;
    assert(cp(&peers[0], 1) == 1);
    assert(exit_wakes == 2 && notify_count == 1 && !security_calls);
    assert(!bbp_gamepad_ble_is_suspended(&peers[0]));
}
static void test_cp_mid_read_and_send(void) {
    reset_case();
    report_hook = suspend_active;
    assert_neutral_read(&peers[0]);
    assert(!security_calls);
    reset_case();
    struct bbp_gamepad_report_body body = current_report;
    subscription_hook = suspend_active;
    int rc = bbp_gamepad_ble_send(&peers[0], &body, context_epoch);
    assert(rc == -ENOTSUP);
    assert(!notify_count && !security_calls);
    reset_case();
    struct bbp_gamepad_report_body neutral = bbp_gamepad_neutral();
    subscription_hook = suspend_active;
    assert(bbp_gamepad_ble_send(&peers[0], &neutral, context_epoch) == -ENOTSUP);
    assert(!notify_count && !security_calls);
}

int main(void) {
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) { peers[i].refs = 1; }
#define RUN(test) do { test(); balanced(); puts("PASS " #test); } while (0)
    RUN(test_pinned_service_old_attrs_and_tail);
    RUN(test_independent_service);
    RUN(test_tail_contract_fails_closed);
    RUN(test_separate_real_maps_and_stock_reads);
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
    RUN(test_cp_validation_isolation_and_lifecycle);
    RUN(test_cp_mid_read_and_send);
    RUN(test_exit_rearms_even_if_worker_runs_synchronously);
    puts("PASS independent1812: stock196 + gamepad141; 9-byte body; per-peer CP; HOST ONLY");
    return 0;
}
