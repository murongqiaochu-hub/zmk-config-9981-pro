#!/usr/bin/env python3
"""Enumeration isolation host tests; not hardware or a complete playable gamepad."""
from pathlib import Path
import os
import re
import subprocess
from check_ble_enum_scope import project_config, git_bytes, ORIGINAL

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / '.host-test-build' / 'ble_enum'
BUILD.mkdir(parents=True, exist_ok=True)
project_config()
# Reuse the old oracle and every BLE fixture byte-for-byte, not a rewritten map.
fixtures = ['tests/test_gamepad_ble.c'] + subprocess.check_output(
    ['git', 'ls-tree', '-r', '--name-only', ORIGINAL, 'tests/gamepad_ble_mocks'],
    cwd=ROOT, text=True).splitlines()
assert len(fixtures) > 1
for p in fixtures:
    assert (ROOT / p).read_bytes() == git_bytes(ORIGINAL, p), p
cc = os.environ.get('CC', 'cc')
flags = [cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
         '-fsanitize=undefined', '-fno-sanitize-recover=all']
for snapshot in [0, 1]:
    for smooth in [0, 1]:
        exe = BUILD / f'ble_{snapshot}_{smooth}'
        subprocess.run([*flags, '-I', str(ROOT / 'tests/gamepad_ble_mocks'),
                        f'-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT={snapshot}',
                        f'-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING={smooth}',
                        str(ROOT / 'tests/test_gamepad_ble.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
shim = ROOT / 'config/boards/arm/bbp9981/custom_driver/gamepad_neutral_shim.c'
exe = BUILD / 'neutral_shim'
subprocess.run([*flags, str(ROOT / 'tests/test_gamepad_neutral_shim.c'), str(shim), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
# Without sanitizers/instrumentation: the real shim must have no external calls
# and only its four public functions, proving no event/work/BT registrations.
obj = BUILD / 'neutral_shim.o'
subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-c', str(shim), '-o', str(obj)], check=True)
assert not subprocess.check_output(['nm', '-u', str(obj)], text=True).strip(), 'shim has external dependencies'
symbols = subprocess.check_output(['nm', '-g', '--defined-only', str(obj)], text=True)
assert set(re.findall(r'\bT\s+(\w+)', symbols)) == {
    'bbp_gamepad_context_epoch', 'bbp_gamepad_context_valid',
    'bbp_gamepad_transport_invalidate', 'bbp_gamepad_get_report'}
assert all(' T ' in line for line in symbols.splitlines()), 'unexpected global data/init state'
print('PASS BLE enumeration: original map/attrs oracle x4; exact full-config inverse; real neutral shim all transports, no external calls/registrations')
print('BOUNDARY: old BLE oracle uses a mock dynamic runtime; real shim test is separate. No radio, iPad enumeration, or full playable runtime is verified. Not a mathematical complement.')
