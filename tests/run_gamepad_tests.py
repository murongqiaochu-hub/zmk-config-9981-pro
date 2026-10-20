#!/usr/bin/env python3
"""Strict host regressions for real gamepad core/runtime + pinned USB/HOG TUs.
No compiler output or SDK acceptance here is an Android/USB/radio test.
"""
from pathlib import Path
import hashlib
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / '.host-test-build'
CC = os.environ.get('CC', 'cc')
COMMON = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
          '-fsanitize=undefined', '-fno-sanitize-recover=all']

def run(args):
    return subprocess.run(args, cwd=ROOT, check=True)

def build(name, includes=(), flags=()):
    exe = BUILD / name
    run([CC, *COMMON, *flags, *['-I' + str(ROOT/p) for p in includes],
         str(ROOT/'tests'/(name.split('_case')[0] + '.c')), '-o', str(exe)])
    return exe

def descriptor_checks(exe):
    raw = subprocess.check_output([str(exe), '--descriptor'], cwd=ROOT)
    pos = size = count = page = report = 0
    lo = hi = umin = umax = None
    usages, inputs = [], []
    while pos < len(raw):
        prefix = raw[pos]; pos += 1
        length = [0, 1, 2, 4][prefix & 3]
        data = raw[pos:pos+length]; pos += length
        value = int.from_bytes(data, 'little')
        typ, tag = (prefix >> 2) & 3, prefix >> 4
        if typ == 1:
            if tag == 0: page = value
            elif tag == 1: lo = int.from_bytes(data, 'little', signed=True)
            elif tag == 2: hi = value
            elif tag == 7: size = value
            elif tag == 8: report = value
            elif tag == 9: count = value
        elif typ == 2:
            if tag == 0: usages.append(value)
            elif tag == 1: umin = value
            elif tag == 2: umax = value
        elif typ == 0:
            if tag == 8:
                inputs.append(dict(page=page, bits=size*count, flags=value, report=report,
                                   lo=lo, hi=hi, usages=list(usages), umin=umin, umax=umax))
            usages, umin, umax = [], None, None
    assert sum(v['bits'] for v in inputs) == 72
    assert {v['report'] for v in inputs} == {4}
    buttons = set()
    for v in inputs:
        if v['page'] == 9 and not (v['flags'] & 1):
            buttons.update(range(v['umin'], v['umax']+1))
    assert buttons == {1, 2, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15}, buttons
    hat = next(v for v in inputs if v['page'] == 1 and 0x39 in v['usages'])
    assert hat['bits'] == 4 and hat['lo'] == 0 and hat['hi'] == 7 and hat['flags'] & 0x40
    axes = next(v for v in inputs if v['usages'] == [0x30, 0x31, 0x32, 0x35])
    assert axes['bits'] == 32 and axes['lo'] == -127 and axes['hi'] == 127 and not axes['flags'] & 4
    triggers = next(v for v in inputs if v['page'] == 2 and v['usages'] == [0xC5, 0xC4])
    assert triggers['bits'] == 16 and triggers['lo'] == 0 and triggers['hi'] == 255 and not triggers['flags'] & 4
    print('PASS Xbox-layout HID descriptor: ID4, 72body bits, 13 real Button usages, null hat8, four active signed axes and Brake/Gas triggers')

def main():
    BUILD.mkdir(exist_ok=True)
    fixtures = {
        'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c': '2e8c12bb4197173d238a6be1e33abf2db5dedbd4a6cf29a83d5e2f6c79d4919b',
        'tests/sdk_v0_3_0_usb_hid.c': '3c7a934a2b5eac808889cd14cbc2d8b40e1dcc35a4ce0fbf6e6ac3a12b51ac8a',
        'tests/sdk_v0_3_0_usb.c': '4b182d9a942c6a09d4c6849f2c9504fd251b7bf5d1cd741e5d05dc82534c138e',
        'tests/hid_snapshot_mocks/zmk/hid.h': 'dd161a6631a9dad59f320389a0c146712b2c953215f4a97a8d0d64b0b3ffdee7',
    }
    for p, expected in fixtures.items():
        assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest() == expected, p
    core = build('test_gamepad_core')
    run([str(core)])
    descriptor_checks(core)
    includes = BUILD / 'gamepad_runtime_include'
    for name in ['zephyr/device.h', 'zephyr/kernel.h', 'zephyr/sys/atomic.h', 'zephyr/logging/log.h',
                 'drivers/behavior.h', 'zmk/behavior.h', 'zmk/endpoints.h', 'zmk/keymap.h',
                 'zmk/event_manager.h', 'zmk/events/layer_state_changed.h', 'zmk/events/endpoint_changed.h',
                 'zmk/usb.h', 'zmk/events/usb_conn_state_changed.h', 'zephyr/bluetooth/conn.h',
                 'zmk/ble.h', 'zmk/events/ble_active_profile_changed.h',
                 'zephyr/devicetree.h', 'zephyr/irq.h', 'zephyr/sys/byteorder.h',
                 'zephyr/drivers/i2c.h', 'zephyr/drivers/gpio.h', 'zephyr/input/input.h',
                 'zephyr/dt-bindings/input/input-event-codes.h', 'dt-bindings/zmk/keys.h',
                 'zmk/events/position_state_changed.h', 'zmk/hid.h']:
        p = includes/name; p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists(): p.write_text('/* declarations from gamepad_runtime_env.h */\n')
    runtime = build('test_gamepad_runtime', includes=[includes], flags=['-Wno-unused-function'])
    run([str(runtime)])
    driver = build('test_gamepad_driver', includes=[includes],
                   flags=['-Wno-unused-function', '-Wno-unused-variable'])
    run([str(driver)])
    bridge = build('test_gamepad_tap_bridge', includes=['tests/tap_output_mocks'])
    run([str(bridge)])
    ble_cases = [[], ['-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT=1'],
                 ['-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=1'],
                 ['-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT=1', '-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=1'],
                 ['-DCONFIG_ZMK_POINTING=0', '-DCONFIG_ZMK_HID_INDICATORS=0']]
    for i, flags in enumerate(ble_cases):
        exe = build('test_gamepad_ble_case' + str(i), ['tests/gamepad_ble_mocks'], flags)
        run([str(exe)])
    for boot in [0, 1]:
        exe = build('test_gamepad_usb_case' + str(boot),
                    ['tests/gamepad_usb_mocks', 'tests/hid_snapshot_mocks', 'tests/sdk_snapshot_mocks'],
                    ['-Wno-unused-function', '-Wno-unused-variable', f'-DCONFIG_ZMK_USB_BOOT={boot}'])
        for mode in ['normal', 'abort', 'submit-reset', 'submit-reset-abort', 'error-ready',
                     'stale-ready', 'endpoints', 'missing', 'bad-cfg', 'init-fail']:
            run([str(exe), mode])
    print('PASS gamepad suite: strict UBSan core/runtime + five HOG configurations + USBBOOT0/1; no device claims')

if __name__ == '__main__':
    main()
