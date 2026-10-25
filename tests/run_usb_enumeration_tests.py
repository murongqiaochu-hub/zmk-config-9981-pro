#!/usr/bin/env python3
"""Original USB transport matrix plus archived neutral shim (host only).
The shim is NOT compiled in firmware; current runtime has its own strict suite.
"""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / '.host-test-build'
BUILD.mkdir(exist_ok=True)
COMMON = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
          '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
          '-fsanitize=undefined', '-fno-sanitize-recover=all']
for mocks in ['gamepad_usb_mocks', 'hid_snapshot_mocks', 'sdk_snapshot_mocks']:
    COMMON += ['-I', str(ROOT / 'tests' / mocks)]
MODES = ['normal', 'abort', 'submit-reset', 'submit-reset-abort', 'error-ready',
         'stale-ready', 'endpoints', 'missing', 'bad-cfg', 'init-fail']
for boot in [0, 1]:
    exe = BUILD / f'gamepad_usb_{boot}'
    subprocess.run([*COMMON, f'-DCONFIG_ZMK_USB_BOOT={boot}',
                    str(ROOT / 'tests/test_gamepad_usb.c'), '-o', str(exe)], check=True)
    for mode in MODES:
        subprocess.run([str(exe), mode], check=True)
    exe = BUILD / f'gamepad_usb_neutral_{boot}'
    subprocess.run([*COMMON, f'-DCONFIG_ZMK_USB_BOOT={boot}',
                    str(ROOT / 'tests/test_gamepad_usb_neutral.c'),
                    str(ROOT / 'config/boards/arm/bbp9981/custom_driver/gamepad_usb_neutral.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS USB enumeration host tests: original transport 20 modes + actual neutral 2 modes; no hardware claim')
