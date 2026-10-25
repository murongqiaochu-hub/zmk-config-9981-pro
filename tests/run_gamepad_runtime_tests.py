#!/usr/bin/env python3
"""Actual 1c61 runtime: original mock-send suite + real linked BLE-disabled path."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / '.host-test-build'
INCLUDES = BUILD / 'gamepad_runtime_include'
HEADERS = [
    'zephyr/device.h', 'zephyr/kernel.h', 'zephyr/sys/atomic.h', 'zephyr/logging/log.h',
    'drivers/behavior.h', 'zmk/behavior.h', 'zmk/endpoints.h', 'zmk/keymap.h',
    'zmk/event_manager.h', 'zmk/events/layer_state_changed.h', 'zmk/events/endpoint_changed.h',
    'zmk/usb.h', 'zmk/events/usb_conn_state_changed.h', 'zephyr/bluetooth/conn.h',
    'zmk/ble.h', 'zmk/events/ble_active_profile_changed.h',
]
for name in HEADERS:
    p = INCLUDES / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('/* declarations supplied by gamepad_runtime_env.h */\n')
COMMON = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
          '-Wno-unused-parameter', '-Wno-unused-function',
          '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I', str(INCLUDES)]
for name in ['test_gamepad_runtime', 'test_gamepad_runtime_disabled']:
    exe = BUILD / name
    sources = [str(ROOT / 'tests' / (name + '.c'))]
    flags = []
    if name.endswith('_disabled'):
        # Separate production TU, no macro replacement or test implementation of send.
        sources.append(str(ROOT / 'config/boards/arm/bbp9981/custom_driver/gamepad_ble_disabled.c'))
        flags.append('-Wl,--wrap=bbp_gamepad_ble_send')
    subprocess.run([*COMMON, *sources, *flags, '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], cwd=ROOT, check=True)
print('PASS actual runtime suites under strict UBSan; no ARM or hardware acceptance claim')
