#!/usr/bin/env python3
"""Independent, fail-closed daily scope. Never learn expected bytes from HEAD.

11ee89e and all its 262 tests/SDK fixtures are immutable. Only the explicitly
listed removals, three C preprocessor projections, USB counts and MEDIA bindings
are authorized. This is a source gate, not a firmware/device acceptance claim.
"""
from functools import lru_cache
import hashlib
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '11ee89eee92969a15f75a0d5848c38c6a2c2ce15'
MEDIA_BASE = '72c0cee8cb0dbd9e14e692a96d9ef3726bcc73e8'
BOARD = 'config/boards/arm/bbp9981'
DRIVER = BOARD + '/custom_driver/'
KEYMAPS = ('config/bbp9981.keymap', BOARD + '/bbp9981.keymap')
DELETED = frozenset([
    BOARD + '/cmake/gamepad.cmake', BOARD + '/cmake/gamepad_sdk_guard.cmake',
    'config/dts/bindings/behaviors/zmk,behavior-gamepad.yaml',
    *[DRIVER + name for name in (
        'behavior_gamepad.c', 'gamepad_ble_extension.h', 'gamepad_ble_service.c',
        'gamepad_report.h', 'gamepad_runtime.c', 'gamepad_runtime.h',
        'gamepad_stick.h', 'gamepad_usb.c', 'gamepad_usb_extension.h',
        'gamepad_usb_lifecycle.h')],
])
INTEGRATION = frozenset([
    *KEYMAPS, 'config/bbp9981.conf', BOARD + '/Kconfig.board',
    BOARD + '/CMakeLists.txt', DRIVER + 'a320_0x57.c',
    DRIVER + 'trackpad_microtap_runtime.c', DRIVER + 'trackpad_tap_output.c',
])
# Independently recovered pre-controller contract, NOT current lead keymap data.
MEDIA = tuple('&to DEFAULT' if p in (35, 40, 41) else '&none' for p in range(42))
MEDIA_SHA256 = 'd223b193563700a10f3ef5ff36aeaa51dedd8154072484acdf167d33adbef9c2'
LAYER_NAMES = ('default_layer', 'sym_layer', 'upper_layer', 'media_layer')


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


@lru_cache(maxsize=None)
def blob(path, commit=BASE):
    return git('show', commit + ':' + path)


@lru_cache(maxsize=None)
def paths(prefix, commit=BASE):
    args = ['ls-tree', '-r', '--name-only', '-z', commit]
    if prefix:
        args.append(prefix)
    return tuple(name for name in git(*args).decode().split('\0') if name)


def replace_once(data, old, new):
    # Historical conf/keymaps/A320 contain CRLF. Match that exact spelling,
    # never normalize the rest of the file or accept whitespace drift.
    if old not in data and b'\r' not in old and b'\n' in old:
        old = old.replace(b'\n', b'\r\n')
        new = new.replace(b'\n', b'\r\n')
    assert data.count(old) == 1, ('ambiguous historical edit', old[:100])
    return data.replace(old, new, 1)


def without_gp_conditionals(data):
    """Evaluate ONLY exact #ifdef CONFIG_BBP9981_GAMEPAD as false.

    Keep all other bytes, nested conditions and CRLF unchanged. This parser
    deliberately rejects elif rather than guessing a different expression.
    """
    lines = data.splitlines(keepends=True)
    out = []
    index = 0
    while index < len(lines):
        if lines[index].strip() != b'#ifdef CONFIG_BBP9981_GAMEPAD':
            out.append(lines[index]); index += 1; continue
        index += 1
        depth = 1
        keep = False
        while depth:
            assert index < len(lines), 'unterminated original GP conditional'
            line = lines[index]
            directive = re.match(rb'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b', line)
            kind = directive.group(1) if directive else None
            if kind in (b'if', b'ifdef', b'ifndef'):
                depth += 1
            elif kind == b'endif':
                depth -= 1
                if depth == 0:
                    index += 1; break
            elif depth == 1 and kind == b'else':
                keep = True; index += 1; continue
            elif depth == 1 and kind == b'elif':
                raise AssertionError('unreviewed GP elif')
            if keep:
                out.append(line)
            index += 1
    return b''.join(out)


def layer_block(data, name):
    hits = list(re.finditer(rb'\b' + name.encode() + rb'\s*\{[^{}]*\};', data))
    assert len(hits) == 1, (name, len(hits))
    return hits[0].group()


def layers(data):
    text = re.sub(rb'/\*.*?\*/|//[^\r\n]*', b'', data, flags=re.S)
    result = {}
    for name in LAYER_NAMES:
        block = layer_block(text, name)
        match = re.fullmatch(rb'\w+\s*\{\s*bindings\s*=\s*<(.*?)>\s*;\s*\};', block, re.S)
        assert match, ('unexpected layer property', name)
        bindings = tuple(' '.join(item.decode().split()) for item in re.findall(rb'&[^&]+', match[1]))
        assert len(bindings) == 42, (name, len(bindings))
        result[name] = bindings
    return result


def expected_nonkeymap(path):
    data = blob(path)
    if path in (DRIVER + 'a320_0x57.c', DRIVER + 'trackpad_tap_output.c',
                DRIVER + 'trackpad_microtap_runtime.c'):
        data = without_gp_conditionals(data)
        if path.endswith('trackpad_microtap_runtime.c'):
            data = replace_once(data, b'#define gamepad_suppresses_mouse() false\n', b'')
            # All three are now constant-false gates; do not touch native guards.
            expression = b'if (gamepad_suppresses_mouse()) { return; }'
            lines = data.splitlines(keepends=True)
            assert sum(line.strip() == expression for line in lines) == 3
            data = b''.join(line for line in lines if line.strip() != expression)
    elif path == BOARD + '/CMakeLists.txt':
        data = replace_once(data, b'    include("${CMAKE_CURRENT_LIST_DIR}/cmake/gamepad.cmake")\n', b'')
    elif path == BOARD + '/Kconfig.board':
        block = b'''config BBP9981_GAMEPAD
    bool "Xbox-layout standard HID gamepad on MEDIA layer"
    depends on BOARD_BBP9981 && (ZMK_USB || ZMK_BLE)
    default n
    help
      Add report ID4 and a gamepad behavior without using keyboard keycodes.
      USB uses its own HID_1 interface; BLE appends an encrypted input report.
      Requires matching bindings. Relative trackpad motion drives one stick
      at a time; LT/RT are digital. This is not native XInput and has no rumble.

'''
        data = replace_once(data, block, b'')
    elif path == 'config/bbp9981.conf':
        data = replace_once(data, b'# Keep the stock keyboard/mouse HID_0; expose the gamepad on its own USB HID_1.\nCONFIG_BBP9981_GAMEPAD=y\nCONFIG_USB_HID_DEVICE_COUNT=2\nCONFIG_USB_HID_REPORTS=4\n',
                            b'# Daily keyboard/mouse only: preserve stock HID_0 and report IDs 1/2/3.\nCONFIG_USB_HID_DEVICE_COUNT=1\nCONFIG_USB_HID_REPORTS=3\n')
    return data


def check_keymap(path, data):
    original = blob(path)
    previous = layers(original)
    actual = layers(data)
    historic = layers(blob(path, MEDIA_BASE))
    assert historic['media_layer'] == MEDIA, 'pre-controller provenance disagrees'
    assert hashlib.sha256('\n'.join(MEDIA).encode()).hexdigest() == MEDIA_SHA256
    for name in LAYER_NAMES[:3]:
        assert actual[name] == previous[name], (path, name, '126 bindings must not change')
    assert actual['media_layer'] == MEDIA, (path, 'MEDIA must restore exactly P35/P40/P41 exits')
    node = b'''        gamepad: gamepad {
            compatible = "zmk,behavior-gamepad";
            #binding-cells = <1>;
            display-name = "Xbox-layout standard HID gamepad";
        };
'''
    original = replace_once(original, node, b'')
    # Permit only formatting/comments inside this restored MEDIA block, not
    # other nodes, parameters, Kconfig gates, layer definitions or any old byte.
    original = replace_once(original, layer_block(original, 'media_layer'), b'MEDIA_RESTORED')
    after = replace_once(data, layer_block(data, 'media_layer'), b'MEDIA_RESTORED')
    assert after == original, (path, 'out-of-MEDIA byte drift')
    return actual


def source_files(root, prefix):
    return {p.relative_to(root).as_posix() for p in (root / prefix).rglob('*') if p.is_file()}


def check(root=ROOT, overrides=None, verbose=True):
    overrides = overrides or {}
    baseline = set(paths('config'))
    assert len(baseline) == 86 and len(DELETED) == 13 and len(INTEGRATION) == 8
    current = source_files(root, 'config')
    assert current == baseline - DELETED, ('config paths changed', sorted(current ^ (baseline - DELETED)))
    maps = []
    for path in sorted(current):
        assert not (root / path).is_symlink(), ('unexpected source symlink', path)
        data = overrides.get(path, (root / path).read_bytes())
        if path in KEYMAPS:
            maps.append(check_keymap(path, data))
        else:
            assert data == expected_nonkeymap(path), ('unauthorized bytes', path)
        assert not re.search(rb'CONFIG_BBP9981_GAMEPAD|bbp_gamepad_|BBP_GAMEPAD|&gamepad|zmk,behavior-gamepad|gamepad\.cmake', data), ('live controller reference', path)
    assert maps[0] == maps[1]
    frozen = paths('tests')
    assert len(frozen) == 262
    for path in frozen:
        assert (root / path).is_file() and not (root / path).is_symlink(), ('missing/replaced frozen test', path)
        assert overrides.get(path, (root / path).read_bytes()) == blob(path), ('frozen test/fixture drift', path)
    for path in source_files(root, 'tests') - set(frozen):
        assert '/__pycache__/' in path or path == 'tests/run_daily_keymouse_tests.py' or path.startswith('tests/daily_'), ('unowned new test', path)
    if verbose:
        print('PASS daily scope: 86 baseline configs -> 73 current; 13 exact removals, 8 narrow integrations, 65 byte-identical; 262 original tests/fixtures frozen', flush=True)
        print('PASS both 4x42 source maps: 126 non-MEDIA unchanged, MEDIA 39 none + P35/P40/P41 DEFAULT exits independently pinned to 72c0cee8', flush=True)


def negatives():
    cases = [
        (DRIVER + 'a320_0x57.c', b'MOTION_GPIO_PIN 2', b'MOTION_GPIO_PIN 3'),
        (DRIVER + 'trackpad_pointer.h', b'TP_POINTER_DENOMINATOR 400', b'TP_POINTER_DENOMINATOR 401'),
        (DRIVER + 'trackpad_microtap.h', b'TP_MICROTAP_QUIET_MS 120U', b'TP_MICROTAP_QUIET_MS 121U'),
        (DRIVER + 'trackpad_microtap_runtime.c', b'MICRO_NATIVE_GUARD_MS 600U', b'MICRO_NATIVE_GUARD_MS 601U'),
        (DRIVER + 'trackpad_tap_output.h', b'UINT32_C(12)', b'UINT32_C(13)'),
        (DRIVER + 'keyboard_backlight.c', b'BLINK_INTERVAL_MS 500', b'BLINK_INTERVAL_MS 501'),
        (DRIVER + 'keyboard_backlight.c', b'MEDIA_BREATHE_INTERVAL_MS 20', b'MEDIA_BREATHE_INTERVAL_MS 21'),
        ('config/bbp9981.conf', b'CONFIG_USB_HID_DEVICE_COUNT=1', b'CONFIG_USB_HID_DEVICE_COUNT=2'),
        (KEYMAPS[0], b'&kp LSHFT', b'&kp LALT'),
        (KEYMAPS[1], b'&cursor_speed', b'&none'),
        ('tests/test_env.h', b'#pragma once', b'#pragma once\n/* unauthorized fixture change */'),
    ]
    for path, old, new in cases:
        current = (ROOT / path).read_bytes()
        assert old in current
        changed = current.replace(old, new, 1)
        try:
            check(overrides={path: changed}, verbose=False)
        except AssertionError:
            pass
        else:
            raise AssertionError('scope accepted counterexample: ' + path)
    # Deliberately retain 42 bindings while corrupting an originally blank slot.
    for path in KEYMAPS:
        current = (ROOT / path).read_bytes()
        before = layer_block(current, 'media_layer')
        changed = current.replace(before, before.replace(b'&none', b'&trans', 1), 1)
        try:
            check(overrides={path: changed}, verbose=False)
        except AssertionError:
            pass
        else:
            raise AssertionError('MEDIA accepted transparent fallback: ' + path)
    print(f'PASS {len(cases) + len(KEYMAPS)} independent scope counterexamples: GPIO/gain/tap/LED/USB/other keys/MEDIA/fixture drift rejected', flush=True)


if __name__ == '__main__':
    check(); negatives()
