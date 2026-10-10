#!/usr/bin/env python3
"""Compile the real driver/behavior code with host mocks; not a HID/device test."""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / '.host-test-build'
STUBS = BUILD / 'include'
HEADERS = [
    'zephyr/device.h', 'zephyr/devicetree.h', 'zephyr/kernel.h',
    'zephyr/logging/log.h', 'zephyr/sys/atomic.h', 'zephyr/sys/byteorder.h',
    'zephyr/drivers/i2c.h', 'zephyr/drivers/gpio.h', 'zephyr/drivers/led.h',
    'zephyr/input/input.h', 'zephyr/dt-bindings/input/input-event-codes.h',
    'drivers/behavior.h', 'dt-bindings/zmk/keys.h',
    'zmk/behavior.h', 'zmk/keymap.h', 'zmk/event_manager.h', 'zmk/endpoints.h',
    'zmk/activity.h', 'zmk/backlight.h', 'zmk/rgb_underglow.h',
    'zmk/events/keycode_state_changed.h', 'zmk/events/position_state_changed.h',
    'zmk/events/activity_state_changed.h',
]
for relative in HEADERS:
    p = STUBS / relative
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('/* Test mock declarations come from test_env.h. */\n')
cc = os.environ.get('CC', 'cc')
for name in ['ctrl', 'sensor', 'pointer', 'combo', 'keyboard_led', 'trackpad_led']:
    exe = BUILD / ('test_' + name)
    extra = ['-DTEST_TWO_LEDS=1'] if name == 'keyboard_led' else []
    subprocess.run([cc, *extra, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-I', str(STUBS), str(ROOT / 'tests' / ('test_' + name + '.c')),
                    '-o', str(exe)], check=True)
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
    assert before == after, relative
    updated.append(after)
assert updated[0] == updated[1]
print('PASS Keymaps: exactly one binding changed per keymap, all standalone SYM/aA and arrow bindings preserved')
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_uf2_validator.py')], check=True)
