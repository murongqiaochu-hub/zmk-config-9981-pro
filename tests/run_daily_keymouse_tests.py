#!/usr/bin/env python3
"""CURRENT daily keyboard/mouse regressions AND separately frozen history.

Default runs the actual current production sources first, then all unchanged
11ee89e historical tests in a private temporary tree/repository. Historical
controller tests are compatibility evidence only, never current-product proof.
No ARM build, remote operation, hardware, settings or user-data access.
"""
import argparse
import ast
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile
from daily_scope import ROOT, BASE, BOARD, DRIVER, check, negatives, git, blob, paths

CC = os.environ.get('CC', 'cc')
COMMON = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
          '-Wno-unused-function', '-Wno-unused-variable', '-fno-sanitize-recover=all']
CASES = ('ctrl sensor pointer contact_probe probe_runtime bus_probe bus_runtime tap '
         'tap_click tap_output hog_snapshot sdk_usb tap_pipeline tap_driver combo '
         'combo_symmetric aa_scroll_gesture_key aa_layer_owner scroll_gesture_pipeline '
         'keyboard_led keyboard_light_memory keyboard_low_pwm trackpad_led '
         'trackpad_low_pwm trackpad_light_memory indicator_pwm menu_diag menu_endpoints '
         'menu_matrix diag_hog microtap shoulder micro_runtime shoulder_runtime '
         'micro_driver micro_cross_source').split()
SPECIAL = {'aa_layer_owner': 'aa_layer_mocks', 'tap_output': 'tap_output_mocks',
           'hog_snapshot': 'hog_snapshot_mocks', 'sdk_usb': 'sdk_snapshot_mocks',
           'menu_matrix': 'menu_diag_mocks', 'diag_hog': 'hog_snapshot_mocks',
           'menu_diag': 'menu_endpoints_mocks', 'menu_endpoints': 'menu_endpoints_mocks'}
BUILT = 0


def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def make_stubs(build):
    """Reuse the frozen declaration list, never execute the old runner prefix."""
    tree = ast.parse(blob('tests/run_host_tests.py'))
    headers = [ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
               and any(isinstance(target, ast.Name) and target.id == 'HEADERS' for target in node.targets)]
    assert len(headers) == 1
    include = build / 'include'
    for name in headers[0]:
        path = include / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('/* Host declarations supplied by the byte-frozen test_env.h. */\n')
    return include


def compile_run(source, target, includes=(), flags=(), extras=(), asan=False,
                negative=False, root=ROOT, expect_source=None):
    global BUILT
    sanitizer = ['-fsanitize=address,undefined', '-no-pie', '-fno-omit-frame-pointer'] if asan else ['-fsanitize=undefined']
    dependency = target.with_suffix('.d')
    command = [CC, *COMMON, *sanitizer, *flags, '-MMD', '-MF', str(dependency),
               *['-I' + str(p) for p in includes], str(root / 'tests' / source),
               *[str(root / p) for p in extras], '-o', str(target)]
    run(command, cwd=root)
    deps = dependency.read_text()
    if expect_source:
        assert str(root / expect_source) in deps or expect_source in deps, ('current source not compiled', source, expect_source)
    # Every current executable, including reused frozen oracles, must be free
    # of live/undefined controller symbols, not just run with a disabled define.
    symbols = subprocess.check_output(['nm', '-a', str(target)], text=True)
    assert not re.search(r'bbp_gamepad_|gamepad_suppresses_mouse|BBP_GAMEPAD', symbols), source
    env = os.environ.copy()
    if asan:
        env['ASAN_OPTIONS'] = 'detect_stack_use_after_return=1:abort_on_error=0:exitcode=91:disable_coredump=1'
    result = subprocess.run([str(target)], cwd=root, env=env, preexec_fn=no_core,
                            capture_output=negative, text=True)
    if negative:
        assert result.returncode != 0 and 'Assertion' in result.stderr, (source, result.returncode, result.stderr)
    else:
        assert result.returncode == 0, (source, result.returncode)
    BUILT += 1


def ordinary_current(build, stubs):
    print('RUN DAILY CURRENT: frozen mouse oracles compile actual current config directly; no production projection', flush=True)
    for name in CASES:
        flags = ['-DTEST_TWO_LEDS=1'] if name in ('keyboard_led', 'keyboard_light_memory', 'keyboard_low_pwm') else []
        include = ROOT / 'tests' / SPECIAL[name] if name in SPECIAL else stubs
        extras = [DRIVER + 'trackpad_tap_pipeline.c'] if name == 'tap_pipeline' else []
        compile_run('test_' + name + '.c', build / name, [include], flags, extras)
    compile_run('test_trackpad_low_pwm.c', build / 'trackpad-two', [stubs], ['-DTEST_TWO_LEDS=1'])
    compile_run('test_tap_output.c', build / 'tap-no-usb-hook', [ROOT / 'tests/tap_output_mocks'], ['-DTP_TEST_NO_USB_HOOK=1'])
    for label, flag in [('settings-off', '-DCONFIG_SETTINGS=0'), ('fault-mock', '-DMD_TEST_WRAPPER_FAILURE=1')]:
        compile_run('test_menu_endpoints.c', build / ('endpoints-' + label), [ROOT / 'tests/menu_endpoints_mocks'], [flag])
    # Retain the real, required endpoint-snapshot link gate.
    result = subprocess.run([CC, *COMMON, '-I' + str(ROOT / 'tests/menu_endpoints_mocks'),
                             '-DMD_TEST_NO_ENDPOINT_SNAPSHOT=1', str(ROOT / 'tests/test_menu_diag.c'),
                             '-o', str(build / 'missing-snapshot')], text=True, capture_output=True)
    assert result.returncode != 0 and 'bbp_menu_diag_endpoints_snapshot' in result.stderr and 'undefined reference' in result.stderr
    print('PASS DAILY CURRENT: ordinary sensor/scroll/tap/USB/HOG/shoulder/owner/lighting/diagnostics C suites + missing-snapshot link negative', flush=True)


def actual_preferences(build, stubs):
    mocks = ROOT / 'tests/cursor_speed_mocks'
    for settings in (0, 1):
        for metadata in (0, 1):
            flags = [f'-DCONFIG_SETTINGS={settings}', f'-DCONFIG_ZMK_BEHAVIOR_METADATA={metadata}', '-pedantic']
            compile_run('test_cursor_speed.c', build / f'service{settings}{metadata}', [mocks], flags)
            for zero in (False, True):
                extra = ['-DCONFIG_BBP9981_CURSOR_SPEED=0'] if zero else []
                target = build / f'disabled{settings}{metadata}{int(zero)}'
                compile_run('test_cursor_speed_disabled.c', target, [mocks], flags + extra)
                assert 'bbp_cursor_speed_' not in subprocess.check_output(['nm', '-u', str(target)], text=True)
    compile_run('test_cursor_filter.c', build / 'filter', flags=['-Wconversion', '-Wsign-conversion', '-pedantic'])
    compile_run('test_cursor_driver.c', build / 'steady-direct', [stubs], ['-DTEST_CURSOR_MICRO=0'], expect_source=DRIVER + 'a320_0x57.c')
    compile_run('test_cursor_hog.c', build / 'steady-hog', [ROOT / 'tests/hog_snapshot_mocks'])
    compile_run('test_cursor_presets_math.c', build / 'presets-math', [stubs])
    for micro in (0, 1):
        compile_run('daily_cursor_presets_driver.c', build / f'daily-driver{micro}', [stubs],
                    [f'-DTEST_CURSOR_MICRO={micro}'], expect_source=DRIVER + 'a320_0x57.c')
    media = ['-DTEST_TWO_LEDS=1', '-DCONFIG_SETTINGS=1']
    compile_run('test_media_breathe.c', build / 'media-on', [stubs], media + ['-DCONFIG_BBP9981_MEDIA_BREATHE=1'])
    for label, flags in [('led', []), ('pwm', ['-DTEST_MEDIA_OFF_LOW_PWM=1']), ('memory', ['-DTEST_MEDIA_OFF_LIGHT_MEMORY=1'])]:
        compile_run('test_media_breathe.c', build / ('media-off-' + label), [stubs], media + flags)
    compile_run('daily_tap_output.c', build / 'daily-bridge', [ROOT / 'tests/tap_output_mocks'], expect_source=DRIVER + 'trackpad_tap_output.c')
    # The firmware settings-ON service and independent settings-OFF adapter have
    # separate native C suites. The motion integration uses only mocked RAM.
    for source, label, includes, flags in [
        ('test_cursor_speed.c', 'service', [mocks], ['-DCONFIG_SETTINGS=1', '-DCONFIG_ZMK_BEHAVIOR_METADATA=1', '-pedantic']),
        ('test_cursor_speed_disabled.c', 'disabled', [mocks], ['-DCONFIG_SETTINGS=1', '-DCONFIG_ZMK_BEHAVIOR_METADATA=1', '-pedantic']),
        ('test_cursor_filter.c', 'filter', [], ['-Wconversion', '-Wsign-conversion', '-pedantic']),
        ('test_cursor_hog.c', 'hog', [ROOT / 'tests/hog_snapshot_mocks'], []),
        ('test_cursor_presets_math.c', 'math', [stubs], []),
        ('daily_cursor_presets_driver.c', 'driver', [stubs], []),
        ('daily_tap_output.c', 'bridge', [ROOT / 'tests/tap_output_mocks'], []),
        ('test_media_breathe.c', 'media', [stubs], media + ['-DCONFIG_BBP9981_MEDIA_BREATHE=1']),
    ]:
        compile_run(source, build / (label + '-asan'), includes, flags, asan=True)
    compile_run('test_cursor_driver.c', build / 'negative-filter', [stubs],
                ['-DTEST_CURSOR_MICRO=0', '-DTEST_NEGATIVE_BYPASS=1'], negative=True)
    compile_run('daily_cursor_presets_driver.c', build / 'negative-frame-gain', [stubs],
                ['-DTEST_CURSOR_MICRO=1', '-DTEST_NEGATIVE_LIVE_GAIN=1'], negative=True)
    print('PASS DAILY CURRENT: 5 gains/Ctrl, epoch/frame coherence, RAW tap thresholds, filter/USB/BLE ownership, MEDIA slow breathe, settings/adapter variants, UBSan+ASan; all executables have no controller symbols', flush=True)


def suppression_negatives(build, stubs):
    # Mutate ONLY disposable copies. Reinsert a layer-3 suppression at each of
    # the three removed ownership gates; compile successfully, then require the
    # same new positive oracle to fail. No old controller behavior is developed.
    with tempfile.TemporaryDirectory(prefix='daily-negative-') as directory:
        temp = Path(directory)
        shutil.copytree(ROOT / 'tests', temp / 'tests', ignore=shutil.ignore_patterns('__pycache__'))
        shutil.copytree(ROOT / 'config', temp / 'config')
        cases = [
            ('a320_0x57.c', '#define a320_report_rel input_report_rel',
             '#define a320_report_rel(dev,code,value,sync,timeout) do { if (zmk_keymap_highest_layer_active()!=3) { input_report_rel(dev,code,value,sync,timeout); } } while (0)',
             'daily_cursor_presets_driver.c', stubs, 'XY'),
            ('trackpad_microtap_runtime.c', 'static void drain(bool allow_click, tp_microtap_emit_fn emit, void *ctx) {',
             'static void drain(bool allow_click, tp_microtap_emit_fn emit, void *ctx) {\n    if (zmk_keymap_highest_layer_active()==3) { return; }',
             'daily_cursor_presets_driver.c', stubs, 'RAW-drain'),
            ('trackpad_tap_output.c', 'static bool candidate_valid_locked(void) {',
             'static bool candidate_valid_locked(void) {\n    if (zmk_keymap_highest_layer_active()==3) { return false; }',
             'daily_tap_output.c', temp / 'tests/tap_output_mocks', 'queued-DOWN'),
        ]
        for name, old, new, source, include, label in cases:
            path = temp / DRIVER / name
            data = path.read_bytes()
            assert data.count(old.encode()) == 1
            path.write_bytes(data.replace(old.encode(), new.encode(), 1))
            try:
                compile_run(source, build / ('negative-media-' + label), [include], root=temp, negative=True)
            finally:
                path.write_bytes(data)
    print('PASS DAILY live counterexamples: same actual-current oracles reject reintroduced MEDIA XY, RAW-drain and queued-DOWN suppression after successful C compilation', flush=True)


def frozen_history():
    """Run EXACT full 11ee89e suite from independent index/HEAD/config/tests."""
    print('RUN FROZEN HISTORY ONLY: full 11ee89e host suite in private temporary repo; NOT current daily behavior', flush=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('GIT_') and key != 'BBP_REPAIR_PROJECTED'}
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    with tempfile.TemporaryDirectory(prefix='daily-frozen-11ee89e-') as directory:
        temp = Path(directory)
        for name in paths(''):
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(blob(name))
        common = Path(git('rev-parse', '--path-format=absolute', '--git-common-dir').decode().strip())
        assert common.is_absolute() and (common / 'objects').is_dir()
        run(['git', 'init', '-q', str(temp)], env=env)
        (temp / '.git/objects/info/alternates').write_text(str(common / 'objects') + '\n')
        run(['git', 'symbolic-ref', 'HEAD', 'refs/heads/frozen-daily-baseline'], cwd=temp, env=env)
        run(['git', 'update-ref', 'refs/heads/frozen-daily-baseline', BASE], cwd=temp, env=env)
        run(['git', 'read-tree', BASE], cwd=temp, env=env)
        assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=temp, env=env).decode().strip() == BASE
        run([os.sys.executable, '-B', str(temp / 'tests/run_host_tests.py')], cwd=temp, env=env)
    print('PASS FROZEN HISTORY ONLY: complete unmodified 11ee89e suite; old controller cases are historical compatibility, not daily-product tests', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-history', action='store_true', help='development only: skip explicitly labelled isolated historical suite')
    args = parser.parse_args()
    assert not (args.skip_history and os.environ.get('CI')), 'CI must run the complete frozen historical suite'
    check(); negatives()
    with tempfile.TemporaryDirectory(prefix='daily-current-host-') as directory:
        build = Path(directory)
        stubs = make_stubs(build)
        ordinary_current(build, stubs)
        actual_preferences(build, stubs)
        suppression_negatives(build, stubs)
    from daily_cmake import main as cmake_main
    cmake_ran = cmake_main()
    print(f'PASS DAILY CURRENT: {BUILT} compiled/run executables (including expected-failure negatives), exact scope + frozen tests; CMake=' + ('PASS' if cmake_ran else 'SKIP/unavailable'), flush=True)
    if args.skip_history:
        print('SKIP FROZEN HISTORY explicitly requested; NOT a complete daily regression run', flush=True)
    else:
        frozen_history()
    # Recheck after running, so tests cannot silently rewrite config/fixtures.
    check()
    print('PASS daily host regression; physical sensor/USB/BLE/HID enumeration/flash/device behavior NOT tested', flush=True)


if __name__ == '__main__':
    main()
