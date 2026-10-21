#!/usr/bin/env python3
"""Mechanical split proof against the fixed refactor baseline; no writes/builds."""
from difflib import unified_diff
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = 'f7412ec90c925487617a1e8d40dd886c310142a6'
DRIVER = 'config/boards/arm/bbp9981/custom_driver/'


def old(path):
    return subprocess.check_output(
        ['git', 'show', f'{BASELINE}:{path}'], cwd=ROOT).decode('utf-8')


def current(path):
    return (ROOT / path).read_bytes().decode('utf-8')


def identical(actual, expected, label):
    if actual != expected:
        diff = ''.join(unified_diff(expected.splitlines(True), actual.splitlines(True),
                                    fromfile='baseline-derived', tofile=label))
        raise AssertionError(f'{label} differs from the mechanical transform:\n{diff}')


def functions(text):
    """Extract this source's function definitions, preserving every byte."""
    result = {}
    signature = re.compile(r'^(?:static )?(?:uint32_t|bool|void|int)\s+(\w+)\([^;{]*\)\s*\{', re.M)
    token = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')
    for match in signature.finditer(text):
        depth = 1
        for brace in token.finditer(text, match.end()):
            if brace.group() == '{':
                depth += 1
            elif brace.group() == '}':
                depth -= 1
                if not depth:
                    result[match.group(1)] = text[match.start():brace.end()]
                    break
        else:
            raise AssertionError(f'unclosed function {match.group(1)}')
    return result


def main():
    original = old(DRIVER + 'behavior_gamepad.c')
    runtime = current(DRIVER + 'gamepad_runtime.c')
    adapter = current(DRIVER + 'behavior_gamepad.c')
    key_start = original.index('static int binding_key(')
    callbacks_start = original.index('static int pressed(')
    context_start = original.index('static int context_changed(')
    registration_start = original.index('#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)\n')
    original_key = original[key_start:callbacks_start]
    callbacks = original[callbacks_start:context_start]
    registration = original[registration_start:]
    key_signature = ('int bbp_gamepad_key_event(uint32_t position, uint32_t action, '
                     'bool pressed, int64_t timestamp) {')
    expected_key = re.sub(r'^static int binding_key\([\s\S]*?\) \{',
                          key_signature, original_key, count=1)
    for before, after in [('event.position', 'position'), ('binding->param1', 'action'),
                          ('event.timestamp', 'timestamp'), ('ZMK_BEHAVIOR_OPAQUE', '0')]:
        expected_key = expected_key.replace(before, after)
    expected_runtime = original.replace(original_key, expected_key, 1)
    expected_runtime = expected_runtime.replace(callbacks, '', 1).replace(registration, '', 1)
    for adapter_only in ['#define DT_DRV_COMPAT zmk_behavior_gamepad\n',
                         '#include <drivers/behavior.h>\n', '#include <zmk/behavior.h>\n']:
        expected_runtime = expected_runtime.replace(adapter_only, '', 1)
    identical(runtime, expected_runtime, 'gamepad_runtime.c')
    expected_adapter = '''/* SPDX-License-Identifier: MIT
 * Thin ZMK behavior adapter; runtime owns state, FIFO and transport work.
 */
#define DT_DRV_COMPAT zmk_behavior_gamepad
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include "gamepad_runtime.h"

static int binding_key(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event, bool pressed) {
    int ret = bbp_gamepad_key_event(event.position, binding->param1, pressed, event.timestamp);
    return ret < 0 ? ret : ZMK_BEHAVIOR_OPAQUE;
}
'''
    identical(adapter, expected_adapter + callbacks + registration, 'behavior_gamepad.c')
    original_functions = functions(original)
    moved_functions = functions(runtime)
    unchanged = sorted(set(original_functions) - {'binding_key', 'pressed', 'released'})
    for name in unchanged:
        identical(moved_functions[name], original_functions[name], name)
    print(f'PASS {len(unchanged)} original runtime functions byte-identical: ' + ', '.join(unchanged))
    print('PASS key API changes only signature, position/action/timestamp field projection, '
          'and opaque-to-zero return; adapter preserves opaque/error results')
    print('PASS all runtime globals, constants, comments, function/subscription/callback order, '
          'bbp_gamepad_context registration, gamepad_init/SYS_INIT and bbp_gamepad logging byte-identical')

    usb = current(DRIVER + 'gamepad_usb.c')
    original_usb = old(DRIVER + 'gamepad_usb_extension.h')
    identical(usb, original_usb.replace('#pragma once\n', '', 1), 'gamepad_usb.c')
    usb_functions = functions(original_usb)
    for name, text in usb_functions.items():
        identical(functions(usb)[name], text, name)
    print(f'PASS USB header-to-C move: {len(usb_functions)} functions and all implementation bytes '
          'identical except removal of #pragma once')
    compatibility_header = '''/* SPDX-License-Identifier: MIT
 * Compatibility declaration entry only. The standalone HID_1 implementation
 * lives in gamepad_usb.c; never use this header as an SDK usb_hid.c prefix.
 */
#pragma once
#include "gamepad_runtime.h"
'''
    identical(current(DRIVER + 'gamepad_usb_extension.h'), compatibility_header,
              'gamepad_usb_extension.h')
    header_addition = '''/* Basic-type behavior boundary. Returns 0 for consumed/ignored events and
 * -EINVAL for an invalid position or DOWN action. UP releases its captured
 * owner regardless of action. ZMK adapters map 0 to ZMK_BEHAVIOR_OPAQUE. */
int bbp_gamepad_key_event(uint32_t position, uint32_t action, bool pressed, int64_t timestamp);

'''
    identical(current(DRIVER + 'gamepad_runtime.h'),
              old(DRIVER + 'gamepad_runtime.h').replace('struct bt_conn;\n\n',
                  'struct bt_conn;\n\n' + header_addition, 1), 'gamepad_runtime.h')
    for path in ['gamepad_ble_extension.h', 'gamepad_usb_lifecycle.h', 'trackpad_usb_epoch.h',
                 'trackpad_usb_snapshot.h']:
        identical(current(DRIVER + path), old(DRIVER + path), path)
    print('PASS BLE and USB SDK/lifecycle prefixes unchanged')

    runtime_test = 'tests/test_gamepad_runtime.c'
    expected_test = old(runtime_test).replace(
        '#include "../' + DRIVER + 'behavior_gamepad.c"',
        '#include "../' + DRIVER + 'gamepad_runtime.c"\n'
        '#include "../' + DRIVER + 'behavior_gamepad.c"', 1)
    identical(current(runtime_test), expected_test, runtime_test)
    usb_test = 'tests/test_gamepad_usb.c'
    expected_test = old(usb_test).replace(
        '/* Original HID_0 prefixes and SDK fixtures are unchanged. The new header is\n'
        ' * included as standalone code; it must NOT redirect any stock API or symbol. */',
        '/* Original HID_0 prefixes and SDK fixtures are unchanged. The standalone pad\n'
        ' * translation unit must NOT redirect any stock API or symbol. */', 1).replace(
        '#include "../' + DRIVER + 'gamepad_usb_extension.h"',
        '#include "../' + DRIVER + 'gamepad_usb.c"', 1)
    identical(current(usb_test), expected_test, usb_test)
    print('PASS complete original runtime/USB tests and assertions retained; only implementation '
          'includes and the USB include comment changed')
    print('PASS fixed-baseline gamepad refactor mechanical proof; no hardware claims')


if __name__ == '__main__':
    main()
