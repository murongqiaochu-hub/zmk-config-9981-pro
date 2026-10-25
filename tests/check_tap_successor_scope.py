#!/usr/bin/env python3
"""Freeze the accepted base; prove old A320/micro code survives GP-only guards."""
from pathlib import Path
import hashlib
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '72c0cee8cb0dbd9e14e692a96d9ef3726bcc73e8'
PREFIX = 'config/boards/arm/bbp9981/custom_driver/'
GUARDED = {PREFIX + 'a320_0x57.c', PREFIX + 'trackpad_microtap_runtime.c', PREFIX + 'trackpad_tap_output.c'}
ALLOWED = {'config/bbp9981.conf', 'config/bbp9981.keymap', 'config/boards/arm/bbp9981/bbp9981.keymap',
           'config/boards/arm/bbp9981/CMakeLists.txt', 'config/boards/arm/bbp9981/Kconfig.board'} | GUARDED
NEW = {PREFIX + n for n in ['gamepad_report.h', 'gamepad_stick.h', 'gamepad_runtime.h',
       'behavior_gamepad.c', 'gamepad_usb.c', 'gamepad_usb_extension.h',
       'gamepad_usb_lifecycle.h', 'gamepad_ble_extension.h']}
NEW.add('config/dts/bindings/behaviors/zmk,behavior-gamepad.yaml')
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

protected = {}
for p in paths:
    a, b = old(p), (ROOT / p).read_bytes()
    if p not in ALLOWED:
        assert a == b, p
    if a == b:
        protected[p] = hashlib.sha256(b).hexdigest()
from ipad_diag_scope import project_config
# Validate exact diagnostic bytes before applying the unchanged legacy path oracle.
current = set(project_config({p.relative_to(ROOT).as_posix(): p.read_bytes()
                              for p in (ROOT / 'config').rglob('*') if p.is_file()}))
assert current - set(paths) == NEW, sorted(current - set(paths))
assert len(protected) >= 46

# Not a blanket exemption: remove exactly the GP sections/call gates, then
# compare all old source text. Only A320's historical full-CRLF line endings
# normalize to LF (its base blob has 498 CRLFs); no other whitespace is ignored.
a320 = (ROOT / (PREFIX + 'a320_0x57.c')).read_text()
a320 = cut_to(a320, '#ifdef CONFIG_BBP9981_GAMEPAD\n#include "gamepad_runtime.h"',
              '#ifdef CONFIG_A320_TAP_OUTPUT\n')
a320 = cut_to(a320, '#ifdef CONFIG_BBP9981_GAMEPAD\n    uint32_t game_epoch',
              '    /* Epoch also detects enter+exit entirely between two sensor polls. */')
a320 = a320.replace('a320_report_rel(', 'input_report_rel(')
assert a320.encode() == old(PREFIX + 'a320_0x57.c').replace(b'\r\n', b'\n'), 'A320 original path changed beyond guarded ownership routing/CRLF normalization'

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
# Verify the entire four-file candidate before projecting only its exact conf insertion.
# The legacy BASE, original conf oracle, driver restorations and all old gates remain intact.
from hid_metadata_scope import check_scope, projected_legacy_conf
check_scope()
conf = projected_legacy_conf((ROOT / 'config/bbp9981.conf').read_text())
assert conf == old('config/bbp9981.conf').decode().replace('\r\n', '\n').replace('CONFIG_ZMK_BLE=y\n',
    'CONFIG_ZMK_BLE=y\n\n# Keep the stock keyboard/mouse HID_0; expose the gamepad on its own USB HID_1.\nCONFIG_BBP9981_GAMEPAD=y\nCONFIG_USB_HID_DEVICE_COUNT=2\nCONFIG_USB_HID_REPORTS=4\n')
print(f'PASS Xbox scope: {len(protected)} base config files byte-identical; A320/micro/bridge original source restored exactly after GP-only gates/old A320 CRLF normalization; tap policy and bridge I/O/UP logic, native shoulder/Ctrl, GPIO/matrix/DT, pointer math and lights preserved; eight integration + nine new GP files only')
