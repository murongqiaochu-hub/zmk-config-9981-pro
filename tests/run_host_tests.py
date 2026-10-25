#!/usr/bin/env python3
"""Compile the real driver/behavior code with host mocks; not a HID/device test."""
from pathlib import Path
import os
import hashlib
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
# Fail closed on the complete config before running either old or new oracles.
subprocess.run([os.sys.executable, '-B', str(ROOT / 'tests/check_usb_enumeration_scope.py')], check=True)
subprocess.run([os.sys.executable, '-B', str(ROOT / 'tests/test_usb_enumeration_scope.py')], check=True)
subprocess.run([os.sys.executable, '-B', str(ROOT / 'tests/run_usb_enumeration_tests.py')], check=True)
# User contract: every functionality/keymap update also updates the manual.
subprocess.run([os.sys.executable, '-B', str(ROOT / 'tests/check_manual_sync.py')], check=True)
BUILD = ROOT / '.host-test-build'
STUBS = BUILD / 'include'
HEADERS = [
    'zephyr/device.h', 'zephyr/devicetree.h', 'zephyr/kernel.h', 'zephyr/irq.h', 'zephyr/init.h', 'zephyr/settings/settings.h',
    'zephyr/logging/log.h', 'zephyr/sys/atomic.h', 'zephyr/sys/byteorder.h',
    'zephyr/drivers/i2c.h', 'zephyr/drivers/gpio.h', 'zephyr/drivers/led.h', 'zephyr/drivers/uart.h', 'zephyr/drivers/pwm.h',
    'zephyr/input/input.h', 'zephyr/dt-bindings/input/input-event-codes.h',
    'drivers/behavior.h', 'dt-bindings/zmk/keys.h',
    'zmk/behavior.h', 'zmk/keymap.h', 'zmk/event_manager.h', 'zmk/endpoints.h',
    'zmk/activity.h', 'zmk/backlight.h', 'zmk/rgb_underglow.h', 'zmk/usb.h',
    'zmk/events/keycode_state_changed.h', 'zmk/events/position_state_changed.h',
    'zmk/events/activity_state_changed.h', 'zmk/events/endpoint_changed.h',
    'zmk/events/layer_state_changed.h',
    'zmk/hid.h', 'dt-bindings/zmk/mouse.h',
]
for relative in HEADERS:
    p = STUBS / relative
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('/* Test mock declarations come from test_env.h. */\n')
cc = os.environ.get('CC', 'cc')
for name in ['ctrl', 'sensor', 'pointer', 'contact_probe', 'probe_runtime', 'bus_probe', 'bus_runtime', 'tap', 'tap_click', 'tap_output', 'hog_snapshot', 'sdk_usb', 'tap_pipeline', 'tap_driver', 'combo', 'combo_symmetric', 'keyboard_led', 'keyboard_light_memory', 'keyboard_low_pwm', 'trackpad_led', 'trackpad_low_pwm', 'trackpad_light_memory', 'menu_diag', 'menu_endpoints', 'menu_matrix', 'diag_hog', 'microtap', 'shoulder', 'micro_runtime', 'shoulder_runtime', 'micro_driver', 'micro_cross_source']:
    exe = BUILD / ('test_' + name)
    extra = ['-DTEST_TWO_LEDS=1'] if name in ['keyboard_led', 'keyboard_light_memory', 'keyboard_low_pwm'] else []
    special = {'tap_output': 'tap_output_mocks', 'hog_snapshot': 'hog_snapshot_mocks', 'sdk_usb': 'sdk_snapshot_mocks', 'menu_matrix': 'menu_diag_mocks', 'diag_hog': 'hog_snapshot_mocks', 'menu_diag': 'menu_endpoints_mocks', 'menu_endpoints': 'menu_endpoints_mocks'}
    include = ROOT / 'tests' / special[name] if name in special else STUBS
    sources = [str(ROOT / 'tests' / ('test_' + name + '.c'))]
    if name == 'tap_pipeline':
        sources.append(str(ROOT / 'config/boards/arm/bbp9981/custom_driver/trackpad_tap_pipeline.c'))
    subprocess.run([cc, *extra, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-I', str(include), *sources, '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# Exercise fractional trackpad PWM and partial write failures with two channels.
exe = BUILD / 'test_trackpad_led_two'
subprocess.run([cc, '-DTEST_TWO_LEDS=1', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I', str(STUBS),
                str(ROOT / 'tests/test_trackpad_low_pwm.c'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)

# Ensure both stock keymaps preserve every other key and all other layers.
BASE = 'd7e535e'
paths = ['config/bbp9981.keymap', 'config/boards/arm/bbp9981/bbp9981.keymap']
def layers(text):
    result = {}
    for name in ['default_layer', 'sym_layer', 'upper_layer', 'media_layer']:
        body = re.search(r'\b' + name + r'\s*\{\s*bindings\s*=\s*<(.*?)>;', text, re.S).group(1)
        body = re.sub(r'//[^\n]*', '', body)
        result[name] = [' '.join(x.split()) for x in re.findall(r'&[^&]+', body)]
        assert len(result[name]) == 42, (name, len(result[name]))
    return result
updated = []
for relative in paths:
    before = layers(subprocess.check_output(['git', 'show', BASE + ':' + relative], cwd=ROOT, text=True))
    after = layers((ROOT / relative).read_text())
    assert before['upper_layer'][40] == '&to DEFAULT'
    before['upper_layer'][40] = '&aa_sym_ctrl'
    before['default_layer'][0] = '&microtap_shoulder'
    assert before['upper_layer'][7] == '&none'
    before['upper_layer'][7] = '&out OUT_BLE'
    assert before == after, relative
    updated.append(after)
assert updated[0] == updated[1]
print('PASS Keymaps: only left shoulder, SYM Ctrl and unused UPPER-Q explicit OUT_BLE differ from original; all other bindings preserved')
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_uf2_validator.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/hid_snapshot_mocks/replay.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/run_diag_hid.py')], check=True)
subprocess.run([os.sys.executable, '-B', '-m', 'unittest', 'discover', '-s', str(ROOT / 'tests'), '-p', 'test_menu_capture.py', '-v'], check=True)

# Verify untouched pinned API/source bytes, real source-specific injection and its link guard.
fixtures = {
    'tests/sdk_v0_3_0_endpoints.c': '0ff5c15c0e07092586df7b40ba67f1c7cfddbe34c86242d6d0f72ed9d72b9d8e',
    'tests/menu_endpoints_mocks/zmk/endpoints.h': 'd328ffd8472d12cc4f10562d9461372dbab577177c27c23a019ce8b3d57b85d8',
    'tests/menu_endpoints_mocks/zmk/endpoints_types.h': '736852811dbc99b9d4b1523bf574374513c55e8c97c65c15ff9b1e36c16e088c',
    'tests/menu_endpoints_mocks/dt-bindings/zmk/outputs.h': 'fb0e1937478ee16aad86da0108882b9fc64fc20e4bd2383883411811d8b35614',
}
for path, digest in fixtures.items():
    assert hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == digest, path
mock = ROOT / 'tests/menu_endpoints_mocks'
prefix = ROOT / 'config/boards/arm/bbp9981/custom_driver/menu_diag_endpoints.h'
common = [cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable', '-I', str(mock)]
for usb in [0, 1]:
    for ble in [0, 1]:
        obj = BUILD / f'endpoints_injected_{usb}_{ble}.o'
        subprocess.run([*common, f'-DCONFIG_ZMK_USB={usb}', f'-DCONFIG_ZMK_BLE={ble}', '-include', str(prefix), '-c', str(ROOT / 'tests/sdk_v0_3_0_endpoints.c'), '-o', str(obj)], check=True)
        symbols = subprocess.check_output(['nm', '-g', '--defined-only', str(obj)], text=True)
        for name in ['zmk_endpoints_select_transport', 'zmk_endpoints_toggle_transport', 'bbp9981_stock_zmk_endpoints_select_transport', 'bbp9981_stock_zmk_endpoints_toggle_transport', 'bbp_menu_diag_endpoints_snapshot']:
            assert re.search(r'\bT\s+' + name + r'$', symbols, re.M), name
for label, flag in [('settings_off', '-DCONFIG_SETTINGS=0'), ('explicit_fault_mock', '-DMD_TEST_WRAPPER_FAILURE=1')]:
    exe = BUILD / ('menu_endpoints_' + label)
    subprocess.run([*common, flag, '-fsanitize=undefined', '-fno-sanitize-recover=all', str(ROOT / 'tests/test_menu_endpoints.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
negative = subprocess.run([*common, '-DMD_TEST_NO_ENDPOINT_SNAPSHOT=1', str(ROOT / 'tests/test_menu_diag.c'), '-o', str(BUILD / 'missing_snapshot_negative')], text=True, capture_output=True)
assert negative.returncode != 0 and 'bbp_menu_diag_endpoints_snapshot' in negative.stderr and 'undefined reference' in negative.stderr
cmake = (ROOT / 'config/boards/arm/bbp9981/CMakeLists.txt').read_text()
assert '"${APPLICATION_SOURCE_DIR}/src/endpoints.c"' in cmake and cmake.count('menu_diag_endpoints.h') == 1
print('PASS endpoint diagnostics: four unchanged SDK fixtures, actual preinclude across USB/BLE variants, settings-off/fault-mock, missing-snapshot negative link, source-specific CMake target')
subprocess.run([os.sys.executable, '-B', str(ROOT / 'tests/check_tap_successor_scope.py')], check=True)
subprocess.run([os.sys.executable, '-B', str(ROOT / 'tests/test_matrix_isolation.py'), '-v'], check=True)
subprocess.run([os.sys.executable, '-B', str(ROOT / 'tests/test_micro_source_contrast.py')], check=True)
