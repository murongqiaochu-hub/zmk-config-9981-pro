#!/usr/bin/env python3
"""Freeze the accepted base; prove old A320/micro code survives GP-only guards."""
from pathlib import Path
import difflib
import hashlib
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '72c0cee8cb0dbd9e14e692a96d9ef3726bcc73e8'
PREFIX = 'config/boards/arm/bbp9981/custom_driver/'
GUARDED = {PREFIX + 'a320_0x57.c', PREFIX + 'trackpad_microtap_runtime.c', PREFIX + 'trackpad_tap_output.c'}
ALLOWED = {'config/bbp9981.conf', 'config/bbp9981.keymap', 'config/boards/arm/bbp9981/bbp9981.keymap',
           'config/boards/arm/bbp9981/CMakeLists.txt', 'config/boards/arm/bbp9981/Kconfig.board',
           PREFIX + 'behavior_microtap_shoulder.c'} | GUARDED
NEW = {PREFIX + n for n in ['gamepad_report.h', 'gamepad_stick.h', 'gamepad_runtime.h',
       'behavior_gamepad.c', 'gamepad_usb.c', 'gamepad_usb_extension.h',
       'gamepad_usb_lifecycle.h', 'gamepad_ble_extension.h', 'trackpad_scroll_lock.h',
        'behavior_aa_scroll_shoulder.c', 'behavior_aa_scroll_gesture_key.c']}
NEW.add('config/dts/bindings/behaviors/zmk,behavior-gamepad.yaml')
NEW.add('config/dts/bindings/behaviors/zmk,behavior-aa-scroll-shoulder.yaml')
NEW.add('config/dts/bindings/behaviors/zmk,behavior-aa-scroll-gesture-key.yaml')
paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', BASE, 'config'], cwd=ROOT, text=True).splitlines()

def old(p):
    return subprocess.check_output(['git', 'show', BASE + ':' + p], cwd=ROOT)

def clean(s):
    return re.sub(r'\s+', '', re.sub(r'/\*.*?\*/|//[^\n]*', '', s, flags=re.S))

def cut_to(text, start, end):
    assert text.count(start) == 1, start
    i = text.index(start)
    j = text.index(end, i)
    return text[:i] + text[j:]

def remove_exact(text, snippet, count=1):
    actual = text.count(snippet)
    assert actual == count, f'expected {count} exact additions, found {actual}: {snippet[:80]!r}'
    return text.replace(snippet, '')

protected = {}
for p in paths:
    a, b = old(p), (ROOT / p).read_bytes()
    if p not in ALLOWED:
        assert a == b, p
    if a == b:
        protected[p] = hashlib.sha256(b).hexdigest()
current = {str(p.relative_to(ROOT)) for p in (ROOT / 'config').rglob('*') if p.is_file()}
assert current - set(paths) == NEW, sorted(current - set(paths))
assert len(protected) >= 45  # One additional, mode-aware default-shoulder source is now in scope.

# Not a blanket exemption: remove exactly GP gates plus the documented v2.2.4-v2.2.6
# scroll-lock, gesture, and mouse-orientation additions, then compare every remaining old source byte. Only
# A320's historical full-CRLF line endings normalize to LF; no other whitespace is ignored.
a320 = (ROOT / (PREFIX + 'a320_0x57.c')).read_text()
a320 = cut_to(a320, '#ifdef CONFIG_BBP9981_GAMEPAD\n#include "gamepad_runtime.h"',
              '#ifdef CONFIG_A320_TAP_OUTPUT\n')
a320 = cut_to(a320, '#ifdef CONFIG_BBP9981_GAMEPAD\n    uint32_t game_epoch',
              '    /* Epoch also detects enter+exit entirely between two sensor polls. */')
a320 = a320.replace('a320_report_rel(', 'input_report_rel(')
a320 = remove_exact(a320, '/* Mouse orientation is a volatile mouse-only flag; other layers ignore it. */\n')
a320 = remove_exact(a320, 'static atomic_t mouse_orientation_snapshot;\n')
a320 = remove_exact(a320, 'static bool last_mouse_orientation_reversed;\n')
a320 = a320.replace('struct micro_cursor_context { const struct device *dev; bool slow; bool invert_y; };\n',
                    'struct micro_cursor_context { const struct device *dev; bool slow; };\n')
a320 = remove_exact(a320, '    if (ctx->invert_y) { y = tp_scroll_negate_saturated(y); }\n')
a320 = remove_exact(a320, '    bool mouse_reversed = tp_mouse_orientation_reversed();\n    if (mouse_reversed != last_mouse_orientation_reversed) {\n        /* Reset only mouse pointer fractions; leave scroll accumulation untouched. */\n        tp_pointer_reset(&cursor_motion);\n        last_mouse_orientation_reversed = mouse_reversed;\n#ifdef CONFIG_A320_MICROTAP\n        tp_microtap_runtime_interrupt();\n#endif\n    }\n')
a320 = remove_exact(a320, '                if (mouse_reversed) { cursor_y = tp_scroll_negate_saturated(cursor_y); }\n')
a320 = a320.replace('    struct micro_cursor_context cursor_ctx = {\n        .dev = dev, .slow = slow_pointer, .invert_y = mouse_reversed,\n    };\n',
                    '    struct micro_cursor_context cursor_ctx = {.dev = dev, .slow = slow_pointer};\n')
a320 = remove_exact(a320, '#include "trackpad_scroll_lock.h"\n')
a320 = remove_exact(a320, 'static uint8_t last_scroll_rotation;\n')
a320 = remove_exact(a320, '        /* Each lock session starts unrotated; a held aA override does not reset it. */\n        tp_scroll_rotation_reset();\n        tp_scroll_gesture_cancel();\n')
a320 = remove_exact(a320, '    if ((ev->state && ev->position != TP_BLACKBERRY_POSITION &&\n         ev->position != TP_BACK_POSITION) || ev->position == TP_AA_POSITION) {\n        /* Other keys and either aA edge invalidate a partial gesture/shortcut. */\n        tp_scroll_gesture_cancel();\n    }\n')
a320 = remove_exact(a320, '/* Shoulder modifiers are owned only by the conditional UPPER-layer behavior. */\nstatic atomic_t scroll_shoulder_modifier_snapshot;\n')
a320 = remove_exact(a320, 'static bool last_scroll_locked;\nstatic bool last_shoulder_slow;\nstatic int16_t shoulder_scroll_remainder_x, shoulder_scroll_remainder_y;\n')
a320 = remove_exact(a320, '/* Halve discrete wheel reports without discarding every odd notch. */\nstatic int16_t scroll_half_with_remainder(int16_t value, int16_t *remainder) {\n    int32_t total = (int32_t)value + *remainder;\n    int16_t scaled = (int16_t)(total / 2);\n    *remainder = (int16_t)(total % 2);\n    return scaled;\n}\n\n')
a320 = remove_exact(a320, '    bool scroll_locked = (snapshot & TP_MODE_LATCHED) != 0U && (snapshot & TP_MODE_PRESSED) == 0U;\n    bool shoulder_slow = scroll_locked &&\n                          (uint32_t)atomic_get(&scroll_shoulder_modifier_snapshot) != 0U;\n')
a320 = remove_exact(a320, '    uint8_t scroll_rotation = tp_scroll_rotation_quadrants();\n')
a320 = remove_exact(a320, '    if (scroll_locked != last_scroll_locked || shoulder_slow != last_shoulder_slow) {\n        /* Do not carry raw samples or fractional wheel notches across lock/speed edges. */\n        sum_dx = sum_dy = 0;\n        sample_cnt = 0;\n        last_read_time = 0;\n        shoulder_scroll_remainder_x = shoulder_scroll_remainder_y = 0;\n        last_scroll_locked = scroll_locked;\n        last_shoulder_slow = shoulder_slow;\n    }\n')
a320 = remove_exact(a320, '    if (scroll_rotation != last_scroll_rotation) {\n        /* A new orientation must not inherit partial motion or odd half-speed notches. */\n        sum_dx = sum_dy = 0;\n        sample_cnt = 0;\n        last_read_time = 0;\n        shoulder_scroll_remainder_x = shoulder_scroll_remainder_y = 0;\n        last_scroll_rotation = scroll_rotation;\n    }\n')
remainder_lines = re.findall(r'(?m)^[ \t]*shoulder_scroll_remainder_x = shoulder_scroll_remainder_y = 0;\n', a320)
assert len(remainder_lines) == 2, f'unexpected leftover scroll remainder resets: {len(remainder_lines)}'
a320 = re.sub(r'(?m)^[ \t]*shoulder_scroll_remainder_x = shoulder_scroll_remainder_y = 0;\n', '', a320)
new_wheel = '''                    int16_t wheel_x = -scroll_x;
                    int16_t wheel_y = scroll_y;
                    if (scroll_locked) {
                        tp_scroll_apply_rotation(scroll_rotation, &wheel_x, &wheel_y);
                        int16_t unrotated_x = wheel_x;
                        wheel_x = -wheel_y;
                        wheel_y = unrotated_x;
                    }
                    if (shoulder_slow) {
                        wheel_x = scroll_half_with_remainder(wheel_x, &shoulder_scroll_remainder_x);
                        wheel_y = scroll_half_with_remainder(wheel_y, &shoulder_scroll_remainder_y);
                    }
                    input_report_rel(dev, INPUT_REL_HWHEEL, wheel_x, false, K_FOREVER);
                    input_report_rel(dev, INPUT_REL_WHEEL, wheel_y, true, K_FOREVER);
'''
new_wheel = re.sub(r'\n[ \t]*int16_t unrotated_x = wheel_x;\n[ \t]*wheel_x = -wheel_y;\n[ \t]*wheel_y = unrotated_x;', '', new_wheel)
assert a320.count(new_wheel) == 1
a320 = a320.replace(new_wheel, '                    input_report_rel(dev, INPUT_REL_HWHEEL, -scroll_x, false, K_FOREVER);\n                    input_report_rel(dev, INPUT_REL_WHEEL, scroll_y, true, K_FOREVER);\n')
a320 = remove_exact(a320, 'bool tp_scroll_lock_is_latched(void) {\n    return ((uint32_t)atomic_get(&scroll_mode_snapshot) & TP_MODE_LATCHED) != 0U;\n}\nbool tp_scroll_aa_is_pressed(void) { return tp_aa_is_pressed(); }\n')
a320 = remove_exact(a320, 'bool tp_mouse_orientation_reversed(void) {\n    return atomic_get(&mouse_orientation_snapshot) != 0;\n}\nbool tp_mouse_orientation_toggle(void) {\n    int32_t next = tp_mouse_orientation_reversed() ? 0 : 1;\n    atomic_set(&mouse_orientation_snapshot, next);\n    return next != 0;\n}\n')
a320 = remove_exact(a320, 'bool tp_scroll_is_locked(void) {\n    /* A held aA temporarily restores the original scroll mode over a latched lock. */\n    return tp_scroll_lock_is_latched() && !tp_scroll_aa_is_pressed();\n}\nstatic uint32_t scroll_shoulder_bit(uint32_t position) {\n    if (position == TP_LEFT_SHOULDER_POSITION) { return TP_SCROLL_SHOULDER_LEFT_BIT; }\n    if (position == TP_RIGHT_SHOULDER_POSITION) { return TP_SCROLL_SHOULDER_RIGHT_BIT; }\n    return 0U;\n}\nvoid tp_scroll_shoulder_modifier_update(uint32_t position, bool pressed) {\n    uint32_t bit = scroll_shoulder_bit(position);\n    if (bit == 0U) { return; }\n    uint32_t current = (uint32_t)atomic_get(&scroll_shoulder_modifier_snapshot);\n    uint32_t next = pressed ? (current | bit) : (current & ~bit);\n    atomic_set(&scroll_shoulder_modifier_snapshot, (int32_t)next);\n}\nbool tp_scroll_shoulder_slow_active(void) {\n    return tp_scroll_is_locked() &&\n           ((uint32_t)atomic_get(&scroll_shoulder_modifier_snapshot) != 0U);\n}\n')
a320 = remove_exact(a320, '    atomic_set(&scroll_shoulder_modifier_snapshot, 0);\n')
expected_a320 = old(PREFIX + 'a320_0x57.c').replace(b'\r\n', b'\n').decode()
if a320 != expected_a320:
    diff = list(difflib.unified_diff(expected_a320.splitlines(True), a320.splitlines(True), fromfile='base', tofile='restored'))
    print(''.join(diff[:100]))
    raise AssertionError('A320 original path changed beyond documented GP gates and exact locked-scroll additions')

runtime = (ROOT / (PREFIX + 'trackpad_microtap_runtime.c')).read_text()
restored = runtime.replace('#ifdef CONFIG_BBP9981_GAMEPAD\n#include "gamepad_runtime.h"\nstatic uint32_t seen_gamepad_pointer_epoch;\n#endif\n', '')
restored = restored.replace('#ifdef CONFIG_BBP9981_GAMEPAD\n    /* Capture before any policy/bridge callback can change the layer twice. */\n    seen_gamepad_pointer_epoch = bbp_gamepad_pointer_epoch();\n#endif\n', '')
restored = cut_to(restored, '#ifdef CONFIG_BBP9981_GAMEPAD\nstatic bool gamepad_suppresses_mouse',
                  'static void drain(bool allow_click, tp_microtap_emit_fn emit, void *ctx) {')
assert sum(line.strip() == 'if (gamepad_suppresses_mouse()) { return; }' for line in restored.splitlines()) == 3
restored = ''.join(line for line in restored.splitlines(keepends=True)
                   if line.strip() != 'if (gamepad_suppresses_mouse()) { return; }')
restored = restored.replace('#ifdef CONFIG_BBP9981_GAMEPAD\n    allowed = allowed && !bbp_gamepad_layer_active();\n#endif\n', '')
assert restored.encode() == old(PREFIX + 'trackpad_microtap_runtime.c'), 'Micro runtime changed beyond GP mouse ownership guards'

bridge = (ROOT / (PREFIX + 'trackpad_tap_output.c')).read_text()
bridge_blocks = re.findall(r'^#ifdef CONFIG_BBP9981_GAMEPAD\n.*?^#endif\n', bridge, flags=re.M | re.S)
assert len(bridge_blocks) == 6, 'Review each GP-only bridge admission/epoch block'
restored_bridge = re.sub(r'^#ifdef CONFIG_BBP9981_GAMEPAD\n.*?^#endif\n', '', bridge, flags=re.M | re.S)
assert restored_bridge.encode() == old(PREFIX + 'trackpad_tap_output.c'), 'Bridge logic changed beyond GP-only DOWN admission gates'

core = clean((ROOT / (PREFIX + 'trackpad_microtap.h')).read_text())
for name, value in [('MIN_FRAMES', 1), ('MAX_FRAMES', 8), ('MIN_CURSOR_DISTANCE_PX', 3), ('MAX_CURSOR_DISTANCE_PX', 50),
                    ('QUIET_MS', 120), ('MAX_CANDIDATE_MS', 360), ('NAV_QUIET_MS', 200), ('SWIPE_QUIET_MS', 300),
                    ('MAX_MOTION_SPAN_MS', 120), ('CLICK_INTERVAL_MS', 300), ('MAX_PATH', 8192), ('MAX_OFFSET', 4096)]:
    assert f'#defineTP_MICROTAP_{name}{value}U' in core, name
assert 'TP_MICROTAP_MIN_PATH' not in core
assert 'tp_pointer_motion(&s->measure_pointer' in core
assert 'tp_microtap_cursor_distance_in_range(s)' in core
assert 'MICROTAP_POLICY px=3..50 quiet=120 gap=300 native=600 nav=200 swipequiet=300 range=8192/4096 frames=1..8 span=120 age=360 gain=480/400;slow=240/400' in runtime
assert not re.search(r'\b(?:zmk_hid_mouse_button(?:s)?_(?:press|release)|zmk_behavior_invoke_binding|input_report_key)\s*\(', runtime)
workflow = (ROOT / '.github/workflows/main.yml').read_text()
branch = 'codex/xbox-layout-20261020'
assert workflow.count(f'refs/heads/{branch}') == 1 and workflow.count(f'HEAD:{branch}') == 1
conf = (ROOT / 'config/bbp9981.conf').read_text()
assert conf == old('config/bbp9981.conf').decode().replace('\r\n', '\n').replace('CONFIG_ZMK_BLE=y\n',
    'CONFIG_ZMK_BLE=y\n\n# Keep the stock keyboard/mouse HID_0; expose the gamepad on its own USB HID_1.\nCONFIG_BBP9981_GAMEPAD=y\nCONFIG_USB_HID_DEVICE_COUNT=2\nCONFIG_USB_HID_REPORTS=4\n')
print(f'PASS Xbox scope: {len(protected)} base config files byte-identical; A320/micro/bridge source restored exactly after GP gates and explicit v2.2.4-v2.2.6 scroll/mouse additions; pointer/tap policy, bridge I/O/UP, native baseline, GPIO/matrix/DT and lights preserved; eight integration + 14 new gamepad/scroll config files only')
