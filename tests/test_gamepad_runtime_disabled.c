/* Real runtime + separately linked production disabled.c; USB send is a mock.
 * --wrap observes calls but ALWAYS delegates to the real disabled TU.
 * Never force pump repeatedly: a terminal ENOTSUP must leave no scheduled retry.
 */
#define CONFIG_A320_MICROTAP 1
#define TEST_LINK_REAL_BLE_DISABLED 1
#define main archival_mock_suite_main
#include "test_gamepad_runtime.c"
#undef main

static unsigned disabled_calls, microtap_interrupts;
void tp_microtap_runtime_interrupt(void) { assert(!lock_depth); microtap_interrupts++; }
int __real_bbp_gamepad_ble_send(struct bt_conn *, const struct bbp_gamepad_report_body *, uint32_t);
int __wrap_bbp_gamepad_ble_send(struct bt_conn *c, const struct bbp_gamepad_report_body *b, uint32_t e) {
    assert(!lock_depth && c && c->refs > 1);
    assert(bbp_gamepad_is_neutral(b)); /* Ordinary keyboard mode never produces game input. */
    disabled_calls++;
    int ret = __real_bbp_gamepad_ble_send(c, b, e);
    assert(ret == -ENOTSUP);
    return ret;
}
static void quiet(void) {
    assert(!work_pending && !count && !pending_valid && !last_valid && !releasing);
    assert(!security_requests && !accepted_count && !idle_security_requested);
    assert(peer_a.refs == 1 && peer_b.refs == 1 && !lock_depth);
    assert(!state_destination_valid && !tap_bridge_cancels && !microtap_interrupts);
}
static void scheduled_once(unsigned calls) {
    assert(work_pending);
    unsigned before = schedules;
    now_ms += scheduled_delay;
    work_pending = false;
    pump(NULL);
    assert(disabled_calls == calls && schedules == before);
    quiet();
    /* Idle time alone is not a producer: no pending timer, no retry or leaked refs. */
    now_ms += 60000;
    quiet();
}
static void ordinary_ble(void) {
    fixture(true); highest_layer = 0; disabled_calls = 0;
    peer_a.secure = false; peer_a.subscribed = false;
    scheduled_once(1);
    struct bbp_gamepad_report_body b;
    bbp_gamepad_get_report(ZMK_TRANSPORT_BLE, &b);
    assert(bbp_gamepad_is_neutral(&b)); quiet();

    peer_a.connected = false;
    gp_disconnected(&peer_a, 0); scheduled_once(1); /* No captured target. */
    peer_a.connected = true;
    gp_connected(&peer_a, 0); scheduled_once(2);
    gp_connected(&peer_a, 1); quiet(); /* Failed connect cannot wake worker. */
    gp_security_changed(&peer_a, 1, BT_SECURITY_ERR_SUCCESS); scheduled_once(3);
    peer_a.secure = true;
    gp_security_changed(&peer_a, BT_SECURITY_L2, BT_SECURITY_ERR_SUCCESS); scheduled_once(4);
    gp_security_changed(&peer_b, BT_SECURITY_L2, BT_SECURITY_ERR_SUCCESS); scheduled_once(5);
    gp_security_changed(&peer_a, BT_SECURITY_L2, (enum bt_security_err)1); scheduled_once(6);

    active_peer = &peer_b; selected.ble.profile_index = 1;
    context_changed(NULL); scheduled_once(7); /* Active-profile / endpoint listener. */
    for (unsigned layer = 0; layer < 3; layer++) {
        highest_layer = layer; context_changed(NULL); scheduled_once(8 + layer);
    }
    no_leaks();
}
static void ordinary_usb_real_runtime(void) {
    fixture(false); highest_layer = 0; disabled_calls = 0;
    assert(work_pending && gamepad_work_q.started && ready);
    run(1, 0);
    assert(accepted_count == 1 && accepted[0].transport == ZMK_TRANSPORT_USB);
    assert(bbp_gamepad_is_neutral(&accepted[0].body) && accepted[0].body.hat == 8);
    assert(!work_pending && !security_requests && !disabled_calls);
    struct bbp_gamepad_report_body b;
    bbp_gamepad_get_report(ZMK_TRANSPORT_USB, &b);
    assert(bbp_gamepad_is_neutral(&b));
    usb_epoch++; context_changed(NULL); run(1, 0);
    assert(accepted_count == 2 && bbp_gamepad_is_neutral(&accepted[1].body));
    assert(!work_pending && !security_requests && !disabled_calls && !tap_bridge_cancels);
    no_leaks();
    /* These original assertions prove runtime ownership/release, not a shim getter. */
    usb_and_owner(); queued_exit(); analog_coalescing_and_quiet();
    right_mode_click_owner_and_axes_epoch(); observed_tick_and_sample_freshness();
    assert(!disabled_calls);
}
int main(void) {
    ordinary_ble(); ordinary_usb_real_runtime();
    puts("PASS real runtime + linked production BLE-disabled TU: ordinary BLE idle/connect/disconnect/security/profile/layer events return ENOTSUP without security requests, scheduled retries or refs; actual runtime USB neutral/ownership/release; host only");
    return 0;
}
