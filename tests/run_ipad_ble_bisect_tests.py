#!/usr/bin/env python3
"""Stock HOG integration + inert stub; reuse existing mocks, preserve old oracles.
The mock HID map is representative, NOT proof of the actual 196B firmware map.
The real 196B map must additionally be checked in the pinned-header/ARM build.
"""
from pathlib import Path
import hashlib
import os
import subprocess
from check_ipad_ble_bisect import check, ROOT


def between(text, start, end):
    assert text.count(start) == text.count(end) == 1
    return text[text.index(start):text.index(end)]


def main():
    check()
    fixture = ROOT/'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c'
    assert hashlib.sha256(fixture.read_bytes()).hexdigest() == '2e8c12bb4197173d238a6be1e33abf2db5dedbd4a6cf29a83d5e2f6c79d4919b'
    source = (ROOT/'tests/test_gamepad_ble.c').read_text()
    # Reuse the existing environment, NOT its extension tests. All extracted
    # boundaries are unique and original test_gamepad_ble.c remains unchanged.
    env = source[:source.index('static void balanced(void) {')]
    old = '#include "../config/boards/arm/bbp9981/custom_driver/gamepad_ble_extension.h"'
    assert env.count(old) == 1
    env = env.replace(old, '#include "../config/boards/arm/bbp9981/custom_driver/gamepad_ble_disabled.c"')
    helpers = between(source, 'static void check_chrc(', 'static struct bbp_gamepad_report_body read_gamepad(')
    attrs = between(source, 'static void test_pinned_service_old_attrs_and_tail(void) {',
                    '    assert(hog_svc.attr_count == next + 4U);')
    attrs = attrs.replace('    reset_case();\n', '')
    attrs += '    assert(hog_svc.attr_count == next);\n}\n'
    senders = between(source, 'static void test_original_stock_senders_remain(void) {',
                      '#if BBP_GAMEPAD_TEST_WITH_SNAPSHOT && CONFIG_ZMK_POINTING\nstatic void test_existing_mouse_snapshot')
    senders = senders.replace('    reset_case();\n', '').replace('    balanced();\n', '    assert(ref_calls == unref_calls);\n')
    main_c = r'''
int main(void) {
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) {
        peers[i].refs = 1;
        peers[i].state = BT_CONN_STATE_CONNECTED;
        peers[i].dst.a.val[0] = (uint8_t)i;
    }
    active_connection = &peers[0];
    selected.transport = ZMK_TRANSPORT_BLE;
    keyboard.body.modifiers = 8;
    consumer.body.keys[0] = 0x123;
    mouse.body.d_x = -12;
    test_pinned_service_old_attrs_and_tail();
    uint8_t bytes[sizeof(zmk_hid_report_desc) + 8];
    memset(bytes, 0xa5, sizeof(bytes));
    const struct bt_gatt_attr *map = &hog_svc.attrs[4];
    for (size_t offset = 0; offset <= sizeof(zmk_hid_report_desc); ++offset) {
        ssize_t n = map->read(&peers[0], map, bytes, sizeof(bytes), offset);
        assert(n == (ssize_t)(sizeof(zmk_hid_report_desc) - offset));
        assert(!memcmp(bytes, zmk_hid_report_desc + offset, (size_t)n));
    }
    assert(map->read(&peers[0], map, bytes, sizeof(bytes), sizeof(zmk_hid_report_desc)+1)
           == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
    struct bbp_gamepad_report_body body = {0};
    for (unsigned i = 0; i < 100; ++i) {
        assert(bbp_gamepad_ble_send(NULL, NULL, i) == -ENOTSUP);
        assert(bbp_gamepad_ble_send(&peers[0], &body, UINT32_MAX-i) == -ENOTSUP);
    }
    assert(!notify_count && !security_calls && !active_gets && !ref_calls);
    assert(!subscription_checks && !report_gets && !submits && !schedules);
    test_original_stock_senders_remain();
    puts("PASS stock HOG attributes/map slicing/keyboard-consumer-mouse senders/security path + BLE stub no side effects (host model, not 196B/real radio proof)");
    return 0;
}
'''
    build = ROOT/'.host-test-build'
    build.mkdir(exist_ok=True)
    generated = build/'ipad_stock_hog.c'
    generated.write_text(env + helpers + attrs + senders + main_c)
    for snapshot in [0, 1]:
        exe = build/f'ipad_stock_hog_{snapshot}'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        f'-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT={snapshot}',
                        '-Itests/gamepad_ble_mocks', '-Itests', str(generated), '-o', str(exe)],
                       cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)

if __name__ == '__main__':
    main()
