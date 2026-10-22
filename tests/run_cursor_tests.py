#!/usr/bin/env python3
"""Actual v2.3.2 feature-ON driver/HOG plus fixed-baseline OFF proof.

No physical sensor, radio, device, pairing/settings or host preferences touched.
"""
from pathlib import Path
import os
import resource
import subprocess
import tempfile
from pointer_steady_scope import ROOT, DRIVER, sealed, restore, verified_legacy_view

CC = os.environ.get('CC', 'cc')
COMMON = ['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
          '-Wno-unused-function','-Wno-unused-variable','-fno-sanitize-recover=all']


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def build_run(source, target, includes=(), flags=(), asan=False, negative=False):
    sanitizers = ['-fsanitize=address,undefined','-no-pie','-fno-omit-frame-pointer'] if asan else ['-fsanitize=undefined']
    subprocess.run([CC,*COMMON,*sanitizers,*flags,*['-I'+str(p) for p in includes],
                    str(ROOT/'tests'/source),'-o',str(target)],cwd=ROOT,check=True)
    env = os.environ.copy()
    if asan:
        env['ASAN_OPTIONS'] = 'detect_stack_use_after_return=1:abort_on_error=0:exitcode=91:disable_coredump=1'
    run = subprocess.run([str(target)],cwd=ROOT,env=env,preexec_fn=no_core,
                         capture_output=negative,text=True,check=not negative)
    if negative:
        assert run.returncode != 0 and 'Assertion' in run.stderr, run.stderr
        print('PASS live negative: actual driver compiles but +/-1 suppression oracle rejects a bypassed filter',flush=True)


def main():
    view = verified_legacy_view()
    print(f'PASS explicit new authorization: {len(view)}-file historical tree restored exactly; original BASEs/oracles retained',flush=True)
    sensor = DRIVER+'a320_0x57.c'
    current = (ROOT/sensor).read_bytes()
    changed = current.replace(b'MOTION_GPIO_PIN 2', b'MOTION_GPIO_PIN 3')
    assert changed != current and restore(sensor,changed) != sealed(sensor)
    print('PASS scope counterexample: changing original motion GPIO remains outside authorized additions',flush=True)
    build = ROOT/'.host-test-build'
    build.mkdir(exist_ok=True)
    stubs = build/'include'
    assert (stubs/'zephyr/kernel.h').is_file(), 'Run tests/run_host_tests.py once to create the ordinary host SDK stubs'
    with tempfile.TemporaryDirectory(prefix='cursor-steady-',dir=build) as directory:
        temp = Path(directory)
        strict = ['-Wconversion','-Wsign-conversion','-pedantic']
        build_run('test_cursor_filter.c',temp/'filter',flags=strict)
        for micro in [0,1]:
            build_run('test_cursor_driver.c',temp/f'driver{micro}',[stubs],[f'-DTEST_CURSOR_MICRO={micro}'])
        build_run('test_cursor_hog.c',temp/'hog',[ROOT/'tests/hog_snapshot_mocks'])
        # Non-PIE avoids the separately reproduced pre-main ASan PIE failure on
        # this host, without disabling any RAM/undefined-behavior checks.
        build_run('test_cursor_filter.c',temp/'filter-asan',flags=strict,asan=True)
        build_run('test_cursor_driver.c',temp/'driver-asan',[stubs],asan=True)
        build_run('test_cursor_hog.c',temp/'hog-asan',[ROOT/'tests/hog_snapshot_mocks'],asan=True)
        build_run('test_cursor_driver.c',temp/'negative',[stubs],
                  ['-DTEST_NEGATIVE_BYPASS=1','-DTEST_CURSOR_MICRO=0'],negative=True)
    print('PASS cursor steady: strict UBSan + ASan, exhaustive core, actual direct/micro A320, unchanged raw tap/gamepad/wheel paths and bounded BLE ownership/barriers; NOT physical jitter/host-speed acceptance')


if __name__ == '__main__':
    main()
