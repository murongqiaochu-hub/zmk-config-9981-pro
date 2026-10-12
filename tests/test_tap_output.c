/* SPDX-License-Identifier: MIT
 * Production-source host test: includes the actual trackpad_tap_output.c.
 * Mock the real ZMK/Zephyr functions, NOT trackpad_click.h or a fake ops table.
 *
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
 *   -fno-sanitize-recover=all -Itests/tap_output_mocks tests/test_tap_output.c -o <temp-exe>
 * Repeat with -DTP_TEST_NO_USB_HOOK=1 to verify unmodified SDK USB fails closed.
 * Mocks cover control-flow/report copies, not physical USB/BLE delivery or a
 * firmware SDK build. The USB positive test supplies the explicitly missing
 * extension; it is NOT evidence that ZMK v0.3.0 provides that extension.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_ZMK_POINTING 1
#define CONFIG_ZMK_USB 1
#define CONFIG_ZMK_BLE 1

#include <zephyr/kernel.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/usb.h>

uint32_t mock_now;
int mock_schedule_error;
unsigned int mock_queue_starts;

static struct zmk_endpoint_instance mock_endpoint;
static int mock_profile;
static bool mock_ble_connected, mock_usb_ready;
static enum usb_dc_status_code mock_usb_status;
static struct zmk_hid_mouse_report mock_mouse;
static int mock_refs[ZMK_HID_MOUSE_NUM_BUTTONS];
static int mock_press_error, mock_release_error, mock_send_error;
static uint32_t mock_send_duration, mock_usb_generation;
static bool mock_worker_active;
static unsigned int mock_callback, mock_press_calls, mock_release_calls, mock_send_calls;
static void (*mock_send_effect)(void);
static bool mock_emulate_full_hog_queue, mock_transport_pending;
static unsigned int mock_evictions;
static struct zmk_hid_mouse_report_body mock_transport_body;

struct recorded_report {
    struct zmk_hid_mouse_report_body body;
    struct zmk_endpoint_instance endpoint;
    uint32_t at;
    unsigned int callback;
};
static struct recorded_report mock_reports[128];
static unsigned int mock_report_count;
struct recorded_api { char kind; unsigned int button, callback; uint32_t at; };
static struct recorded_api mock_api[256];
static unsigned int mock_api_count;

static void record_api(char kind, unsigned int button) {
    assert(mock_api_count < sizeof(mock_api) / sizeof(mock_api[0]));
    mock_api[mock_api_count++] = (struct recorded_api){kind, button, mock_callback, mock_now};
}
static void refresh_buttons(void) {
    zmk_mouse_button_flags_t buttons = 0;
    for (unsigned int i = 0; i < ZMK_HID_MOUSE_NUM_BUTTONS; i++) {
        if (mock_refs[i] != 0) {
            buttons |= (zmk_mouse_button_flags_t)(1u << i);
        }
    }
    mock_mouse.body.buttons = buttons;
}
int zmk_hid_mouse_button_press(zmk_mouse_button_t button) {
    mock_press_calls++;
    record_api('P', button);
    if (mock_press_error) {
        return mock_press_error;
    }
    if (button >= ZMK_HID_MOUSE_NUM_BUTTONS) {
        return -EINVAL;
    }
    mock_refs[button]++;
    refresh_buttons();
    return 0;
}
int zmk_hid_mouse_button_release(zmk_mouse_button_t button) {
    mock_release_calls++;
    record_api('R', button);
    if (mock_release_error) {
        return mock_release_error;
    }
    if (button >= ZMK_HID_MOUSE_NUM_BUTTONS || mock_refs[button] <= 0) {
        return -EINVAL;
    }
    mock_refs[button]--;
    refresh_buttons();
    return 0;
}
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void) { return &mock_mouse; }
bool zmk_endpoint_instance_eq(struct zmk_endpoint_instance a, struct zmk_endpoint_instance b) {
    if (a.transport != b.transport) {
        return false;
    }
    return a.transport == ZMK_TRANSPORT_USB || a.ble.profile_index == b.ble.profile_index;
}
struct zmk_endpoint_instance zmk_endpoints_selected(void) { return mock_endpoint; }
int zmk_ble_active_profile_index(void) { return mock_profile; }
bool zmk_ble_active_profile_is_connected(void) { return mock_ble_connected; }
bool zmk_usb_is_hid_ready(void) { return mock_usb_ready; }
enum usb_dc_status_code zmk_usb_get_status(void) { return mock_usb_status; }

/* This is the production implementation under test, with its original includes. */
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_tap_output.c"

static int mock_send(struct zmk_hid_mouse_report_body *body, char kind, bool pinned,
                     int expected_profile, uint32_t expected_usb_epoch) {
    assert(mock_worker_active);             /* Transport never runs on producer. */
    assert(!tap_output.lock.locked);        /* Nor under a producer/state lock. */
    assert(body != &mock_mouse.body);       /* No SDK mutable-global-report path. */
    assert(body->d_x == 0 && body->d_y == 0 && body->d_scroll_x == 0 && body->d_scroll_y == 0);
    struct zmk_hid_mouse_report_body sent = *body;
    struct zmk_endpoint_instance captured = mock_endpoint;
    if (kind == 'B' && pinned) {
        captured.ble.profile_index = expected_profile;
    }
    mock_send_calls++;
    record_api(kind, 0);
    mock_now += mock_send_duration;         /* Simulate SDK's worker-side wait. */
    if (mock_send_effect) {
        void (*effect)(void) = mock_send_effect;
        mock_send_effect = NULL;
        effect();
    }
    if (kind == 'U' && pinned) {
        if (expected_usb_epoch != mock_usb_generation) {
            return -ESTALE;                 /* Check AFTER the real semaphore wait. */
        }
        sent.buttons = mock_mouse.body.buttons; /* SDK after-wait CURRENT owners. */
    }
    if (mock_send_error) {
        return mock_send_error;             /* No queue acceptance on this error. */
    }
    if (kind == 'B' && mock_emulate_full_hog_queue) {
        if (mock_transport_pending) {
            if (pinned) {
                return -EAGAIN;             /* Required SDK hook: no eviction. */
            }
            mock_evictions++;               /* STOCK v0.3 HOG: drop-oldest and return 0. */
        }
        mock_transport_body = sent;
        mock_transport_pending = true;
    }
    assert(mock_report_count < sizeof(mock_reports) / sizeof(mock_reports[0]));
    mock_reports[mock_report_count++] = (struct recorded_report){
        .body = sent, .endpoint = pinned ? captured : mock_endpoint,
        .at = mock_now, .callback = mock_callback};
    return 0;                              /* Queue copy, NOT host notification ACK. */
}
/* Stock API model exists to demonstrate why the bridge must not fall back. */
int zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *body) {
    assert(mock_endpoint.transport == ZMK_TRANSPORT_BLE);
    return mock_send(body, 'B', false, -1, 0);
}
#if !defined(TP_TEST_NO_BACKEND_HOOKS)
int bbp9981_ble_hid_send_mouse_buttons_snapshot(struct zmk_hid_mouse_report_body *body,
                                               int expected_profile) {
    assert(mock_endpoint.transport == ZMK_TRANSPORT_BLE && mock_profile == expected_profile);
    return mock_send(body, 'B', true, expected_profile, 0);
}
#endif
#if !defined(TP_TEST_NO_USB_HOOK) && !defined(TP_TEST_NO_BACKEND_HOOKS)
/* SDK extensions, NOT official zero-argument zmk_usb_hid_send_mouse_report. */
uint32_t bbp9981_usb_hid_connection_epoch(void) { return mock_usb_generation; }
int bbp9981_usb_hid_send_mouse_buttons_snapshot_epoch(struct zmk_hid_mouse_report_body *body,
                                                     uint32_t expected_epoch) {
    assert(mock_endpoint.transport == ZMK_TRANSPORT_USB);
    return mock_send(body, 'U', true, -1, expected_epoch);
}
#endif

static struct tp_tap_output_context context(uint32_t epoch) {
    return (struct tp_tap_output_context){
        .epoch = epoch, .enabled = true, .physical_left_known = true, .physical_left_down = false};
}
static void cold_boot(void) {
    /* Host test ONLY: separate cases represent separate hardware cold boots.
     * Production never memsets state or re-initializes an inflight bridge. */
    memset(&tap_output, 0, sizeof(tap_output));
    memset(&mock_mouse, 0, sizeof(mock_mouse));
    memset(mock_refs, 0, sizeof(mock_refs));
    memset(mock_api, 0, sizeof(mock_api));
    memset(mock_reports, 0, sizeof(mock_reports));
    mock_mouse.report_id = ZMK_HID_REPORT_ID_MOUSE;
    mock_endpoint = (struct zmk_endpoint_instance){.transport = ZMK_TRANSPORT_BLE,
                                                  .ble = {.profile_index = 0}};
    mock_profile = 0;
    mock_ble_connected = mock_usb_ready = true;
    mock_usb_status = USB_DC_CONFIGURED;
    mock_now = mock_send_duration = mock_usb_generation = 0;
    mock_schedule_error = mock_press_error = mock_release_error = mock_send_error = 0;
    mock_queue_starts = mock_callback = mock_press_calls = mock_release_calls = mock_send_calls = 0;
    mock_api_count = mock_report_count = 0;
    mock_worker_active = false;
    mock_send_effect = NULL;
    mock_emulate_full_hog_queue = mock_transport_pending = false;
    mock_evictions = 0;
    memset(&mock_transport_body, 0, sizeof(mock_transport_body));
    assert(tp_tap_output_init() == 0);
    assert(tp_tap_output_update_context(context(1)) == 0);
}
static void run_worker_at(uint32_t now) {
    assert(!mock_worker_active && tap_output.work.pending);
    mock_now = now;
    tap_output.work.pending = false;
    mock_callback++;
    mock_worker_active = true;
    tap_output.work.work.handler(&tap_output.work.work);
    mock_worker_active = false;
}
static void start_down(uint32_t now) {
    mock_now = now;
    assert(tp_tap_output_submit() == 0);
    assert(tp_tap_output_read_inflight().phase == TP_TAP_OUTPUT_QUEUED);
    run_worker_at(now);
    assert(tp_tap_output_read_inflight().phase == TP_TAP_OUTPUT_HOLDING);
    assert(tp_tap_output_read_inflight().owns_left && mock_refs[0] == 1);
}
static void lifecycle_event(void) {
    zmk_event_t event = {.kind = 1};
    assert(tp_tap_output_lifecycle_registered(&event) == ZMK_EV_EVENT_BUBBLE);
}
static void select_profile(int profile) {
    mock_endpoint.ble.profile_index = profile;
    mock_profile = profile;
    lifecycle_event();
}
static void assert_idle(void) {
    struct tp_tap_output_inflight s = tp_tap_output_read_inflight();
    assert(s.phase == TP_TAP_OUTPUT_IDLE && !s.busy && !s.owns_left && !s.down_accepted &&
           !s.up_pending);
}

static void test_virgin_init_and_default_disabled(void) {
    assert(tp_tap_output_submit() == -EACCES);
    assert(tp_tap_output_cancel() == -EACCES);
    assert(tp_tap_output_update_context(context(1)) == -EACCES);
    cold_boot();
    assert(tp_tap_output_init() == -EALREADY && mock_queue_starts == 1);
    struct tp_tap_output_context c = context(1);
    c.enabled = false;
    assert(tp_tap_output_update_context(c) == 0);
    assert(tp_tap_output_submit() == -EACCES);
    c.enabled = true;
    c.physical_left_known = false;
    assert(tp_tap_output_update_context(c) == 0);
    assert(tp_tap_output_submit() == -EAGAIN);
    assert(mock_press_calls == 0 && mock_send_calls == 0);
}
static void test_single_inflight_and_separate_12ms_events(void) {
    cold_boot();
    assert(tp_tap_output_submit() == 0);
    assert(mock_send_calls == 0 && mock_now == 0);    /* Immediate submit. */
    assert(tp_tap_output_submit() == -EBUSY);
    run_worker_at(100);
    assert(mock_report_count == 1 && mock_reports[0].body.buttons == 1);
    assert(mock_api_count == 2 && mock_api[0].kind == 'P' && mock_api[0].button == 0);
    assert(mock_api[1].kind == 'B' && mock_api[0].callback == mock_api[1].callback);
    assert(tp_tap_output_init() == -EALREADY && mock_refs[0] == 1); /* No live reset. */
    assert(tp_tap_output_submit() == -EBUSY);
    run_worker_at(111);                              /* Deliberately early callback. */
    assert(mock_report_count == 1 && mock_release_calls == 0 && tap_output.work.delay == 1);
    run_worker_at(112);
    assert(mock_report_count == 2 && mock_reports[1].body.buttons == 0);
    assert(mock_api_count == 4 && mock_api[2].kind == 'R' && mock_api[2].button == 0);
    assert(mock_api[3].kind == 'B');
    assert(mock_reports[0].callback != mock_reports[1].callback);
    assert((uint32_t)(mock_reports[1].at - mock_reports[0].at) >= TP_TAP_OUTPUT_HOLD_MS);
    assert(mock_refs[0] == 0 && mock_release_calls == 1);
    assert_idle();
}
static void test_sdk_wait_is_worker_only_hold_starts_after_acceptance(void) {
    cold_boot();
    mock_send_duration = 100;
    assert(tp_tap_output_submit() == 0 && mock_now == 0 && mock_send_calls == 0);
    run_worker_at(0);
    assert(mock_now == 100 && tp_tap_output_read_inflight().accepted_at == 100);
    mock_send_duration = 0;
    run_worker_at(111);
    assert(mock_report_count == 1 && mock_release_calls == 0);
    run_worker_at(112);
    assert(mock_report_count == 2 && mock_reports[1].at == 112);
    assert_idle();
}
static void test_foreign_buttons_preserved_and_motion_never_repeated(void) {
    cold_boot();
    assert(zmk_hid_mouse_button_press(1) == 0);       /* R */
    assert(zmk_hid_mouse_button_press(2) == 0);       /* M */
    assert(zmk_hid_mouse_button_press(4) == 0);       /* fifth */
    mock_mouse.body.d_x = 273;
    mock_mouse.body.d_y = -411;
    mock_mouse.body.d_scroll_x = 23;
    mock_mouse.body.d_scroll_y = -16;
    start_down(20);
    assert(mock_reports[0].body.buttons == 23);
    assert(mock_mouse.body.d_x == 273 && mock_mouse.body.d_y == -411);
    assert(zmk_hid_mouse_button_release(1) == 0);
    assert(zmk_hid_mouse_button_press(3) == 0);       /* changed between events */
    run_worker_at(32);
    assert(mock_reports[1].body.buttons == 28);
    assert(mock_mouse.body.buttons == 28 && mock_refs[2] == 1 && mock_refs[3] == 1 && mock_refs[4] == 1);
    assert(mock_mouse.body.d_x == 273 && mock_mouse.body.d_y == -411);
    assert(mock_mouse.body.d_scroll_x == 23 && mock_mouse.body.d_scroll_y == -16);
    assert_idle();
}
static void test_physical_before_submit_and_before_worker(void) {
    cold_boot();
    struct tp_tap_output_context c = context(1);
    c.physical_left_down = true;
    assert(tp_tap_output_update_context(c) == 0);
    assert(tp_tap_output_submit() == -EBUSY && mock_send_calls == 0);
    c.physical_left_down = false;
    assert(tp_tap_output_update_context(c) == 0);
    assert(zmk_hid_mouse_button_press(0) == 0);       /* foreign-left HID bit safeguard */
    assert(tp_tap_output_submit() == -EBUSY && mock_send_calls == 0);
    assert(zmk_hid_mouse_button_release(0) == 0);
    assert(tp_tap_output_submit() == 0);
    c.physical_left_down = true;
    assert(zmk_hid_mouse_button_press(0) == 0);
    assert(tp_tap_output_update_context(c) == 0);
    run_worker_at(0);
    assert(mock_refs[0] == 1 && mock_report_count == 0);
    assert_idle();
}
static void test_physical_takeover_preserves_real_reference(void) {
    cold_boot();
    start_down(0);
    assert(zmk_hid_mouse_button_press(0) == 0 && mock_refs[0] == 2);
    struct tp_tap_output_context c = context(1);
    c.physical_left_down = true;
    assert(tp_tap_output_update_context(c) == 0);
    run_worker_at(5);
    assert(mock_refs[0] == 2 && mock_release_calls == 0 && mock_report_count == 1);
    run_worker_at(12);
    assert(mock_refs[0] == 1 && mock_release_calls == 1);
    assert(mock_reports[1].body.buttons == 1);       /* Our UP is not physical UP. */
    assert(tp_tap_output_submit() == -EBUSY);
    assert_idle();
}
static void test_down_press_and_send_failures_are_not_success(void) {
    cold_boot();
    mock_press_error = -EINVAL;
    assert(tp_tap_output_submit() == 0);
    run_worker_at(0);
    assert(tp_tap_output_read_inflight().last_error == -EINVAL);
    assert(mock_refs[0] == 0 && mock_send_calls == 0 && mock_release_calls == 0);
    assert_idle();
    cold_boot();
    mock_send_error = -ENOBUFS;
    assert(tp_tap_output_submit() == 0);
    run_worker_at(0);
    struct tp_tap_output_inflight s = tp_tap_output_read_inflight();
    assert(s.last_error == -ENOBUFS && s.down_reports == 0 && !s.down_accepted);
    assert(s.last_down_error == -ENOBUFS && s.down_failures == 1 && s.up_pending && s.busy);
    assert(s.phase == TP_TAP_OUTPUT_RELEASING && tp_tap_output_submit() == -EBUSY);
    assert(mock_refs[0] == 0 && mock_release_calls == 1 && mock_report_count == 0);
    assert(mock_api_count == 3 && mock_api[0].kind == 'P' && mock_api[1].kind == 'B' &&
           mock_api[2].kind == 'R');
    mock_send_error = 0;
    run_worker_at(1);
    s = tp_tap_output_read_inflight();
    assert(s.down_reports == 0 && s.up_reports == 1 && s.last_error == -ENOBUFS);
    assert(mock_send_calls == 2 && mock_reports[0].body.buttons == 0 && mock_release_calls == 1);
    assert_idle();
}
static void test_up_retries_current_buttons_no_double_release(void) {
    cold_boot();
    start_down(0);
    mock_send_error = -EAGAIN;
    run_worker_at(12);
    struct tp_tap_output_inflight s = tp_tap_output_read_inflight();
    assert(s.phase == TP_TAP_OUTPUT_RELEASING && !s.owns_left && s.down_accepted);
    assert(s.up_attempts == 1 && s.last_error == -EAGAIN && mock_release_calls == 1);
    assert(tp_tap_output_submit() == -EBUSY);
    assert(zmk_hid_mouse_button_press(1) == 0);
    assert(zmk_hid_mouse_button_press(0) == 0);       /* Physical owner during retry. */
    struct tp_tap_output_context c = context(1);
    c.physical_left_down = true;
    assert(tp_tap_output_update_context(c) == 0);
    mock_send_error = 0;
    run_worker_at(21);
    assert(mock_send_calls == 2 && mock_release_calls == 1 && tap_output.work.delay == 1);
    run_worker_at(22);
    assert(mock_send_calls == 3 && mock_release_calls == 1 && mock_refs[0] == 1);
    assert(mock_report_count == 2 && mock_reports[1].body.buttons == 3);
    assert_idle();
}
static void test_release_ref_failure_keeps_owned_until_explicit_cancel(void) {
    cold_boot();
    start_down(0);
    mock_release_error = -EIO;
    run_worker_at(12);
    struct tp_tap_output_inflight s = tp_tap_output_read_inflight();
    assert(s.phase == TP_TAP_OUTPUT_FAULT && s.owns_left && s.busy && mock_refs[0] == 1);
    assert(s.last_error == -EIO && mock_send_calls == 1);
    assert(tp_tap_output_submit() == -EBUSY);
    assert(tp_tap_output_update_context(context(2)) == 0);
    assert(tp_tap_output_read_inflight().phase == TP_TAP_OUTPUT_FAULT);
    assert(mock_release_calls == 1);                /* No auto/busy-loop ref retries. */
    mock_release_error = 0;
    assert(tp_tap_output_cancel() == 0);
    run_worker_at(13);
    assert(mock_release_calls == 2 && mock_refs[0] == 0 && mock_report_count == 2);
    assert_idle();
    cold_boot();
    mock_send_error = -ENOBUFS;
    mock_release_error = -EIO;
    assert(tp_tap_output_submit() == 0);
    run_worker_at(0);
    s = tp_tap_output_read_inflight();
    assert(s.phase == TP_TAP_OUTPUT_FAULT && s.owns_left && !s.down_accepted);
    assert(tp_tap_output_submit() == -EBUSY && mock_report_count == 0);
    mock_release_error = mock_send_error = 0;
    assert(tp_tap_output_cancel() == 0);
    run_worker_at(1);
    assert(mock_refs[0] == 0 && mock_send_calls == 2 && mock_report_count == 1);
    assert(mock_reports[0].body.buttons == 0 && tp_tap_output_read_inflight().down_reports == 0);
    assert(tp_tap_output_read_inflight().last_down_error == -ENOBUFS);
    assert_idle();
}
static void test_retry_exhaustion_blocks_and_can_explicitly_retry(void) {
    cold_boot();
    start_down(0);
    mock_send_error = -EAGAIN;
    for (unsigned int i = 0; i < TP_TAP_OUTPUT_MAX_UP_ATTEMPTS; i++) {
        run_worker_at(12 + i * TP_TAP_OUTPUT_RETRY_MS);
    }
    struct tp_tap_output_inflight s = tp_tap_output_read_inflight();
    assert(s.phase == TP_TAP_OUTPUT_FAULT && !s.owns_left && s.down_accepted && s.busy);
    assert(s.up_attempts == TP_TAP_OUTPUT_MAX_UP_ATTEMPTS && mock_release_calls == 1);
    assert(tp_tap_output_submit() == -EBUSY);
    mock_send_error = 0;
    assert(tp_tap_output_cancel() == 0);
    run_worker_at(mock_now);
    assert(mock_report_count == 2 && mock_release_calls == 1);
    assert_idle();
}
static void test_cancel_and_context_epoch_do_not_replay_candidates(void) {
    cold_boot();
    assert(tp_tap_output_submit() == 0);
    assert(tp_tap_output_cancel() == 0);
    run_worker_at(0);
    assert(mock_press_calls == 0 && mock_send_calls == 0);
    assert_idle();
    assert(tp_tap_output_submit() == 0);
    assert(tp_tap_output_update_context(context(2)) == 0);
    run_worker_at(0);
    assert(mock_press_calls == 0 && mock_send_calls == 0);
    assert_idle();
    start_down(0);
    assert(tp_tap_output_cancel() == 0);
    run_worker_at(3);
    assert(mock_refs[0] == 1 && mock_release_calls == 0 && tap_output.work.delay == 9);
    run_worker_at(12);
    assert(mock_report_count == 2 && mock_refs[0] == 0);
    assert_idle();
}
static void test_endpoint_profile_and_disconnect_epochs(void) {
    cold_boot();
    assert(tp_tap_output_submit() == 0);
    select_profile(1);
    run_worker_at(0);
    assert(mock_report_count == 0 && mock_press_calls == 0);
    assert(tp_tap_output_read_inflight().last_error == -ESTALE);
    assert_idle();
    cold_boot();
    start_down(0);
    select_profile(1);
    run_worker_at(12);
    assert(mock_refs[0] == 0 && mock_release_calls == 1 && mock_report_count == 1);
    assert(mock_reports[0].endpoint.ble.profile_index == 0); /* No old UP to profile 1. */
    assert(tp_tap_output_read_inflight().backend_unfenced);
    assert_idle();
    cold_boot();
    start_down(0);
    mock_ble_connected = false;
    lifecycle_event();
    run_worker_at(12);
    assert(mock_report_count == 1 && mock_refs[0] == 0);
    assert(tp_tap_output_submit() == -ENODEV);
    assert_idle();
    cold_boot();
    assert(tp_tap_output_submit() == 0);
    mock_endpoint.transport = ZMK_TRANSPORT_USB;
    lifecycle_event();
    run_worker_at(0);
    assert(mock_press_calls == 0 && mock_report_count == 0);
    assert_idle();
}
static void test_idle_same_endpoint_reconnect_invalidates_generation(void) {
    cold_boot();
    start_down(0);
    run_worker_at(12);
    uint32_t old_epoch = tp_tap_output_read_inflight().lifecycle_epoch;
    assert_idle();
    mock_ble_connected = false;
    lifecycle_event();
    mock_ble_connected = true;
    lifecycle_event();
    assert(tp_tap_output_read_inflight().lifecycle_epoch != old_epoch);
    assert(tp_tap_output_read_inflight().backend_unfenced); /* Not a fabricated queue flush ACK. */
    start_down(20);                               /* A genuinely NEW validated candidate. */
    run_worker_at(32);
    assert(mock_report_count == 4 && mock_refs[0] == 0);
    assert_idle();
}
static void test_uptime_wrap(void) {
    cold_boot();
    uint32_t start = UINT32_MAX - 5;
    start_down(start);
    run_worker_at(start + 11u);
    assert(mock_release_calls == 0 && mock_report_count == 1 && tap_output.work.delay == 1);
    run_worker_at(start + 12u);
    assert(mock_release_calls == 1 && mock_report_count == 2);
    assert((uint32_t)(mock_reports[1].at - mock_reports[0].at) == 12);
    assert_idle();
    cold_boot();
    start_down(UINT32_MAX - 15);
    mock_send_error = -EAGAIN;
    run_worker_at(UINT32_MAX - 3);
    mock_send_error = 0;
    run_worker_at(5);                               /* nine ms after failed UP */
    assert(mock_send_calls == 2 && tap_output.work.delay == 1);
    run_worker_at(6);
    assert(mock_report_count == 2 && mock_send_calls == 3 && mock_release_calls == 1);
    assert_idle();
}
static void test_schedule_failure_does_not_leave_fake_accepted_candidate(void) {
    cold_boot();
    mock_schedule_error = -EBUSY;
    assert(tp_tap_output_submit() == -EBUSY);
    assert(mock_press_calls == 0 && mock_send_calls == 0);
    assert(tp_tap_output_read_inflight().last_error == -EBUSY);
    assert_idle();
    cold_boot();
    assert(tp_tap_output_submit() == 0);
    mock_schedule_error = -EIO;
    run_worker_at(0);                                /* DOWN accepted, hold scheduling fails. */
    assert(tp_tap_output_read_inflight().phase == TP_TAP_OUTPUT_FAULT);
    assert(tp_tap_output_read_inflight().owns_left && mock_refs[0] == 1);
    assert(tp_tap_output_submit() == -EBUSY);
    mock_schedule_error = 0;
    assert(tp_tap_output_cancel() == 0);
    run_worker_at(11);
    assert(mock_release_calls == 0);
    run_worker_at(12);
    assert(mock_refs[0] == 0 && mock_report_count == 2);
    assert_idle();
}
static void test_usb_snapshot_extension_or_unpatched_fail_closed(void) {
    cold_boot();
    mock_endpoint.transport = ZMK_TRANSPORT_USB;
    lifecycle_event();
    mock_usb_ready = true;
    mock_usb_status = USB_DC_SUSPEND;
    assert(tp_tap_output_submit() == -ENODEV);       /* wakeup success is not report acceptance */
    mock_usb_status = USB_DC_CONFIGURED;
#if defined(TP_TEST_NO_USB_HOOK) || defined(TP_TEST_NO_BACKEND_HOOKS)
    assert(tp_tap_output_submit() == -ENOTSUP);
    assert(mock_press_calls == 0 && mock_send_calls == 0);
    puts("PASS unmodified-v0.3 USB: explicit ENOTSUP, no fake report success");
#else
    mock_mouse.body.d_x = 1000;
    mock_mouse.body.d_scroll_y = -24;
    mock_send_duration = 30;
    assert(tp_tap_output_submit() == 0 && mock_send_calls == 0 && mock_now == 0);
    run_worker_at(0);
    assert(mock_now == 30 && mock_reports[0].endpoint.transport == ZMK_TRANSPORT_USB);
    mock_send_duration = 0;
    run_worker_at(41);
    assert(mock_release_calls == 0);
    run_worker_at(42);
    assert(mock_report_count == 2 && mock_reports[1].body.buttons == 0);
    assert(mock_mouse.body.d_x == 1000 && mock_mouse.body.d_scroll_y == -24);
    assert_idle();
    cold_boot();
    mock_endpoint.transport = ZMK_TRANSPORT_USB;
    lifecycle_event();
    start_down(0);
    mock_usb_ready = false;
    mock_usb_status = USB_DC_DISCONNECTED;
    lifecycle_event();
    run_worker_at(12);
    assert(mock_report_count == 1 && mock_refs[0] == 0);
    mock_usb_ready = true;
    mock_usb_status = USB_DC_CONFIGURED;
    lifecycle_event();
    start_down(20);
    run_worker_at(32);
    assert(mock_report_count == 3 && mock_refs[0] == 0);
    assert_idle();
    puts("PASS optional USB snapshot extension mock (NOT a stock v0.3 API)");
#endif
}
static void change_profile_inside_sdk_wait(void) { select_profile(1); }
static void test_pinned_profile_capture_during_sdk_wait(void) {
    cold_boot();
    mock_send_duration = 100;
    mock_send_effect = change_profile_inside_sdk_wait;
    assert(tp_tap_output_submit() == 0);
    run_worker_at(0);
    /* Required SDK hook captures profile 0's connection before its wait. The
     * bridge must pass the submit-time profile, never the current profile 1. */
    assert(mock_report_count == 1 && mock_reports[0].endpoint.ble.profile_index == 0);
    assert(tp_tap_output_read_inflight().backend_unfenced); /* Still NOT host ACK/queue flush. */
    mock_send_duration = 0;
    run_worker_at(112);
    assert(mock_report_count == 1 && mock_refs[0] == 0); /* No old UP to profile 1. */
    assert_idle();
    puts("PASS pinned BLE extension route across SDK wait; acceptance still is not host ACK");
}

static void test_pinned_ble_no_eviction_and_backpressure(void) {
    cold_boot();
    mock_emulate_full_hog_queue = true;
    start_down(0);
    run_worker_at(12);
    struct tp_tap_output_inflight s = tp_tap_output_read_inflight();
    assert(s.down_reports == 1 && s.up_reports == 0 && s.last_error == -EAGAIN);
    assert(s.up_pending && s.busy && mock_refs[0] == 0 && mock_evictions == 0);
    assert(mock_transport_pending && mock_transport_body.buttons == 1);
    mock_transport_pending = false; /* Captured connection drained DOWN, now retry UP. */
    run_worker_at(22);
    s = tp_tap_output_read_inflight();
    assert(s.down_reports == 1 && s.up_reports == 1 && mock_evictions == 0);
    assert(mock_transport_pending && mock_transport_body.buttons == 0);
    assert_idle();
}
static void test_public_hog_eviction_is_not_host_ack(void) {
    cold_boot();
    mock_emulate_full_hog_queue = true;
    /* Direct STOCK SDK model only: production bridge never falls back to it. */
    struct zmk_hid_mouse_report_body body = {.buttons = 1};
    mock_worker_active = true;
    assert(zmk_hog_send_mouse_report(&body) == 0);
    body.buttons = 0;
    assert(zmk_hog_send_mouse_report(&body) == 0);
    mock_worker_active = false;
    assert(mock_report_count == 2 && mock_evictions == 1 && mock_transport_body.buttons == 0);
    assert(tp_tap_output_read_inflight().down_reports == 0); /* No bridge path used stock API. */
    puts("STOCK SDK LIMITATION: drop-oldest can evict DOWN; bridge requires no-evict pinned hook");
}

static void test_failed_down_corrective_up_retries_without_false_success(void) {
    cold_boot();
    mock_send_error = -EAGAIN;
    assert(tp_tap_output_submit() == 0);
    run_worker_at(0);
    struct tp_tap_output_inflight s = tp_tap_output_read_inflight();
    assert(s.down_reports == 0 && s.down_failures == 1 && s.last_down_error == -EAGAIN);
    assert(!s.down_accepted && !s.owns_left && s.up_pending && s.busy);
    run_worker_at(0); /* Separate corrective event, fails with backpressure too. */
    assert(mock_release_calls == 1 && tp_tap_output_read_inflight().up_attempts == 1);
    mock_send_error = 0;
    assert(zmk_hid_mouse_button_press(1) == 0);
    run_worker_at(10);
    s = tp_tap_output_read_inflight();
    assert(s.down_reports == 0 && s.up_reports == 1 && s.last_error == -EAGAIN);
    assert(mock_report_count == 1 && mock_reports[0].body.buttons == 2);
    assert(mock_release_calls == 1 && mock_refs[0] == 0 && mock_refs[1] == 1);
    assert_idle();
}
#if !defined(TP_TEST_NO_USB_HOOK) && !defined(TP_TEST_NO_BACKEND_HOOKS)
static void usb_raw_disconnect_during_wait(void) { mock_usb_generation++; }
static void physical_left_and_right_during_wait(void) {
    assert(zmk_hid_mouse_button_press(0) == 0);
    assert(zmk_hid_mouse_button_press(1) == 0);
    struct tp_tap_output_context c = context(1);
    c.physical_left_down = true;
    assert(tp_tap_output_update_context(c) == 0);
}
#endif
static void test_usb_raw_generation_fence_and_idle_reconnect(void) {
#if !defined(TP_TEST_NO_USB_HOOK) && !defined(TP_TEST_NO_BACKEND_HOOKS)
    cold_boot();
    mock_endpoint.transport = ZMK_TRANSPORT_USB;
    assert(tp_tap_output_submit() == 0);
    mock_usb_generation++; /* Same endpoint, no ZMK lifecycle notifier delivered. */
    run_worker_at(0);
    assert(mock_press_calls == 0 && mock_report_count == 0);
    assert(tp_tap_output_read_inflight().last_error == -ESTALE);
    assert_idle();

    assert(tp_tap_output_submit() == 0);
    mock_send_duration = 30;
    mock_send_effect = usb_raw_disconnect_during_wait;
    run_worker_at(0);
    struct tp_tap_output_inflight s = tp_tap_output_read_inflight();
    assert(s.down_reports == 0 && s.last_down_error == -ESTALE && !s.owns_left);
    assert(mock_report_count == 0 && mock_release_calls == 1);
    mock_send_duration = 0;
    run_worker_at(30); /* New generation: no old corrective UP sent there. */
    assert(mock_send_calls == 1 && mock_report_count == 0);
    assert_idle();

    start_down(40);
    run_worker_at(52);
    uint32_t old_lifecycle = tp_tap_output_read_inflight().lifecycle_epoch;
    mock_usb_generation++; /* Idle reconnect must be observed on the next submit. */
    start_down(60);
    assert(tp_tap_output_read_inflight().lifecycle_epoch != old_lifecycle);
    run_worker_at(72);
    assert(mock_report_count == 4 && mock_refs[0] == 0);
    assert_idle();
#endif
}
static void test_usb_after_wait_uses_current_foreign_buttons(void) {
#if !defined(TP_TEST_NO_USB_HOOK) && !defined(TP_TEST_NO_BACKEND_HOOKS)
    cold_boot();
    mock_endpoint.transport = ZMK_TRANSPORT_USB;
    start_down(0);
    mock_send_duration = 30;
    mock_send_effect = physical_left_and_right_during_wait;
    run_worker_at(12);
    assert(mock_report_count == 2 && mock_reports[1].body.buttons == 3);
    assert(mock_refs[0] == 1 && mock_refs[1] == 1 && mock_release_calls == 1);
    assert_idle(); /* Our reference gone, real physical reference preserved. */
#endif
}

int main(void) {
#if defined(TP_TEST_NO_BACKEND_HOOKS)
    cold_boot();
    assert(tp_tap_output_submit() == -ENOTSUP);
    mock_endpoint.transport = ZMK_TRANSPORT_USB;
    lifecycle_event();
    assert(tp_tap_output_submit() == -ENOTSUP);
    assert(mock_press_calls == 0 && mock_send_calls == 0 && mock_report_count == 0);
    assert_idle();
    puts("PASS missing SDK hooks: both USB and BLE fail closed before HID ref acquisition");
    return 0;
#endif
    test_virgin_init_and_default_disabled();
    test_single_inflight_and_separate_12ms_events();
    test_sdk_wait_is_worker_only_hold_starts_after_acceptance();
    test_foreign_buttons_preserved_and_motion_never_repeated();
    test_physical_before_submit_and_before_worker();
    test_physical_takeover_preserves_real_reference();
    test_down_press_and_send_failures_are_not_success();
    test_up_retries_current_buttons_no_double_release();
    test_release_ref_failure_keeps_owned_until_explicit_cancel();
    test_retry_exhaustion_blocks_and_can_explicitly_retry();
    test_cancel_and_context_epoch_do_not_replay_candidates();
    test_endpoint_profile_and_disconnect_epochs();
    test_idle_same_endpoint_reconnect_invalidates_generation();
    test_uptime_wrap();
    test_schedule_failure_does_not_leave_fake_accepted_candidate();
    test_usb_snapshot_extension_or_unpatched_fail_closed();
    test_pinned_profile_capture_during_sdk_wait();
    test_pinned_ble_no_eviction_and_backpressure();
    test_public_hog_eviction_is_not_host_ack();
    test_failed_down_corrective_up_retries_without_false_success();
    test_usb_raw_generation_fence_and_idle_reconnect();
    test_usb_after_wait_uses_current_foreign_buttons();
#if defined(TP_TEST_NO_USB_HOOK)
    puts("PASS tap output: 20 cases; 2 USB-hook-only cases skipped; required-hook fail-closed covered");
#else
    puts("PASS tap output: 22 production-source host cases; ZMK HID + required SDK hook mocks; not host ACK");
#endif
    return 0;
}
