#!/usr/bin/env python3
"""v2.2.9 -> v2.3.0 exact scope, historical gate and differential host traces.

The reference comes from an immutable local Git commit, never the edited tree.
No device, SDK checkout, pairing/settings store or network service is modified.
"""
from pathlib import Path
import io
import os
import re
import subprocess
import tarfile
import tempfile
from refactor_support import ROOT, BASE, DRIVER, reference, tokens, function, expand_cmake

BUILD = ROOT / '.host-test-build'
CC = os.environ.get('CC', 'cc')
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
         '-Wno-unused-function', '-Wno-unused-variable', '-fsanitize=undefined',
         '-fno-sanitize-recover=all']


def text(path): return (ROOT / path).read_text()


def same_function(old, new, name):
    assert tokens(function(old, name)) == tokens(function(new, name)), 'changed function: ' + name


def scope_checks():
    previous = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', BASE, 'config'],
                                       cwd=ROOT, text=True).splitlines()
    changed = {p for p in previous if reference(p) != (ROOT / p).read_bytes()}
    expected = {'config/boards/arm/bbp9981/CMakeLists.txt'} | {DRIVER + p for p in [
        'a320_0x57.c', 'behavior_gamepad.c', 'gamepad_runtime.h', 'gamepad_usb.c',
        'gamepad_usb_extension.h', 'keyboard_backlight.c', 'trackpad_led.c']}
    assert changed == expected, ('unexpected refactor edits', changed ^ expected)
    added = {str(p.relative_to(ROOT)) for p in (ROOT / 'config').rglob('*') if p.is_file()} - set(previous)
    new = {DRIVER + p for p in ['trackpad_mode.c', 'trackpad_mode.h', 'trackpad_scroll.h',
                               'gamepad_runtime.c', 'indicator_pwm.h']}
    new |= {'config/boards/arm/bbp9981/cmake/' + p for p in ['matrix_guard.cmake',
            'tap_extensions.cmake', 'gamepad.cmake', 'gamepad_sdk_guard.cmake', 'diagnostics.cmake']}
    assert added == new, ('unexpected refactor files', added ^ new)
    print(f'PASS refactor scope: {len(previous)-len(changed)} baseline config files byte-identical; '
          f'{len(changed)} explicit existing files / {len(new)} explicit new files')

    # All sensor I/O, diagnostic probe transactions and mode/listener bodies are frozen.
    old = reference(DRIVER + 'a320_0x57.c').decode()
    mode = text(DRIVER + 'trackpad_mode.c')
    sensor = text(DRIVER + 'a320_0x57.c')
    for name in ['ctrl_listener_cb', 'tp_scroll_mode_active', 'tp_scroll_lock_is_latched',
                 'tp_scroll_aa_is_pressed', 'tp_scroll_is_locked', 'tp_mouse_orientation_reversed',
                 'tp_mouse_orientation_toggle', 'scroll_shoulder_bit',
                 'tp_scroll_shoulder_modifier_update', 'tp_scroll_shoulder_slow_active', 'tp_aa_is_pressed']:
        same_function(old, mode, name)
    for name in ['probe_read_one', 'compare_pointer_stop', 'compare_read_only', 'compare_combined',
                 'compare_bus', 'probe_legacy', 'probe_poll', 'micro_emit_cursor', 'a320_read_motion', 'tp_is_touched']:
        same_function(old, sensor, name)
    # Listener names/registration order are retained, not silently re-registered.
    registrations = r'ZMK_(?:LISTENER|SUBSCRIPTION)\([^;]*\);'
    assert re.findall(registrations, old) == re.findall(registrations, mode)
    for getter, field in [('tp_mode_scroll_snapshot','scroll_mode_snapshot'),
                          ('tp_mode_speed_snapshot','cursor_speed_snapshot'),
                          ('tp_mode_shoulder_snapshot','scroll_shoulder_modifier_snapshot')]:
        assert tokens(function(mode, getter)) == tokens(
            f'uint32_t {getter}(void) {{ return (uint32_t)atomic_get(&{field}); }}')
    print('PASS mode service: original listener/public function tokens and subscriptions unchanged')

    # Every lighting function other than its explicit shared PWM wrapper is unchanged.
    for path, wrapper in [('keyboard_backlight.c','kb_set_fraction'), ('trackpad_led.c','set_led_brightness')]:
        old = reference(DRIVER + path).decode()
        new_text = text(DRIVER + path)
        names = re.findall(r'^(?:static\s+)?(?:void|bool|int|uint8_t|uint32_t)\s+(\w+)\s*\([^;{}]*\)\s*\{', old, re.M)
        for name in names:
            if name != wrapper: same_function(old, new_text, name)
    print('PASS lighting: all original animation/settings/init function tokens retained outside shared PWM wrappers')

    # Local include extraction is exact; the only production additions are the two new TUs.
    path = 'config/boards/arm/bbp9981/CMakeLists.txt'
    old = reference(path).decode()
    new_text = expand_cmake(ROOT / path)
    new_text = new_text.replace(' custom_driver/trackpad_mode.c', '').replace(' custom_driver/gamepad_runtime.c', '')
    strip_comments = lambda s: re.sub(r'(?m)#[^\n]*', '', s)
    assert tokens(strip_comments(old)) == tokens(strip_comments(new_text)), 'CMake source/guard/prefix ordering changed'
    print('PASS CMake: expanded build/SDK gates and ordered preincludes identical, except 2 explicit TUs')


def materialize_reference(destination):
    archive = subprocess.check_output(['git', 'archive', BASE, 'config', 'tests', '.github'], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        for member in tar:
            path = Path(member.name)
            assert not path.is_absolute() and '..' not in path.parts
            target = destination / path
            if member.isdir(): target.mkdir(parents=True, exist_ok=True)
            else:
                assert member.isfile(), ('unexpected archive entry', member.name)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(tar.extractfile(member).read())


def compile_trace(source, destination, definitions, includes):
    subprocess.run([CC, *FLAGS, *['-I' + str(p) for p in includes],
                    *[f'-D{k}={v}' for k,v in definitions.items()], str(source), '-o', str(destination)],
                   cwd=ROOT, check=True)
    return subprocess.check_output([str(destination)], cwd=ROOT)


def trace_checks(snapshot, temp):
    cases = [('sensor', 'refactor_trace_driver.c', {})]
    cases += [(f'{kind}_settings{settings}', 'refactor_trace_lights.c',
               {'TRACE_TP': str(kind == 'tp').lower().replace('true','1').replace('false','0'),
                'CONFIG_SETTINGS': str(settings), 'TEST_TWO_LEDS': '1'})
              for kind in ['kb','tp'] for settings in [0,1]]
    cases += [('gamepad','refactor_trace_gamepad.c',{})]
    for name, source, extra in cases:
        outputs = []
        for label, tree in [('baseline', snapshot), ('refactor', ROOT)]:
            defs = dict(extra)
            defs['REFACTORED'] = '1' if tree == ROOT else '0'
            if name == 'sensor':
                defs['FW_A320_SOURCE'] = '"' + str(tree / DRIVER / 'a320_0x57.c') + '"'
                defs['FW_MODE_SOURCE'] = '"' + str(tree / DRIVER / 'trackpad_mode.c') + '"'
            elif name == 'gamepad':
                defs['FW_GAMEPAD_TEST'] = '"' + str(tree / 'tests/test_gamepad_runtime.c') + '"'
            else:
                leaf = 'trackpad_led.c' if extra['TRACE_TP'] == '1' else 'keyboard_backlight.c'
                defs['FW_LIGHT_SOURCE'] = '"' + str(tree / DRIVER / leaf) + '"'
            include_dirs = [BUILD / 'include', BUILD / 'gamepad_runtime_include', ROOT / 'tests']
            output = compile_trace(ROOT / 'tests' / source, temp / f'{name}-{label}', defs, include_dirs)
            (temp / f'{name}-{label}.trace').write_bytes(output)
            outputs.append(output)
        assert outputs[0] == outputs[1], f'{name}: baseline/refactor ordered trace mismatch'
        print(f'PASS differential {name}: {len(outputs[0].splitlines())} identical cumulative checkpoints')
        if name == 'kb_settings1':
            # Prove the error-log oracle is live, not swallowed by host LOG macros.
            negative = temp/'negative-keyboard.c'
            original = text(DRIVER+'keyboard_backlight.c')
            marker = 'LOG_ERR("Keyboard PWM[%d] failed: %d", channel, error)'
            assert original.count(marker)==1
            negative.write_text(original.replace(marker,marker.replace('LOG_ERR','LOG_DBG')))
            mutated_defs=dict(defs, FW_LIGHT_SOURCE='"'+str(negative)+'"')
            changed=compile_trace(ROOT/'tests'/source,temp/'negative-keyboard',mutated_defs,
                                  [*include_dirs,ROOT/DRIVER])
            assert changed != outputs[1], 'log-level mutation escaped differential oracle'
            print('PASS negative log oracle: changing PWM error level breaks the old/new ordered trace')


def main():
    BUILD.mkdir(exist_ok=True)
    scope_checks()
    subprocess.run([os.sys.executable, '-B', str(ROOT/'tests/test_refactor_sensor_structure.py')], check=True)
    subprocess.run([os.sys.executable, '-B', str(ROOT/'tests/test_refactor_cmake.py')], check=True)
    with tempfile.TemporaryDirectory(prefix='refactor-', dir=BUILD) as directory:
        temp = Path(directory)
        snapshot = temp / 'reference'
        snapshot.mkdir()
        materialize_reference(snapshot)
        # Retain, and actually execute, the old pre-Xbox restoration assertions.
        env = os.environ.copy()
        env['GIT_DIR'] = subprocess.check_output(['git','rev-parse','--absolute-git-dir'], cwd=ROOT, text=True).strip()
        env['GIT_WORK_TREE'] = str(snapshot)
        subprocess.run([os.sys.executable, '-B', str(snapshot / 'tests/check_tap_successor_scope.py')],
                       cwd=snapshot, env=env, check=True)
        trace_checks(snapshot, temp)
    print('PASS refactor equivalence: original historical gate + exact scope + immutable bodies + old/new host traces; NOT hardware acceptance')


if __name__ == '__main__': main()
