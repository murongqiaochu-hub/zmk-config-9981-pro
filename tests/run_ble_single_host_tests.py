#!/usr/bin/env python3
"""Compile the actual pinned BLE TU + source-local auth prefix + policy service.

Independent build subtree; never runs the full host suite, changes fixtures,
contacts a controller/settings store, builds ARM firmware, pushes or flashes.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOCKS = ROOT / 'tests/ble_single_host_mocks'
DRIVER = ROOT / 'config/boards/arm/bbp9981/custom_driver'
BUILD = ROOT / '.host-test-build/ble-single-host'
CC = os.environ.get('CC', 'cc')
PIN = 'edf5c0814fd3ea202e43aad2d68fd32e882a518c'
# These acceptance identities come from the fixed upstream source, never from
# a hash generated from whatever fixture happens to be in the edited tree.
EXPECTED = {
    'sdk_v0_3_0_ble.c': (27201, '2611eee5e49169ebb0f145ef052a75084929ef72', 'ab936cdb01c2c9ddd3c90ac1c231fb18b791f92e2f5ab8e934b8d8bbd23291d0'),
    'zmk/ble.h': (1524, '92b2107d88b12d2779c02305053032372386391a', 'f1a212524ca101289d21fcfe9f2e27de0f61a741582878974d7b7b262f6ec500'),
    'zmk/ble/profile.h': (265, '0a57f16fa4b5a752d3c51d97358891c725256635', 'f680f4603b13e2a9b6e00639a60d62e80ae3add18440f802e3e7ad8940f28aef'),
    'zmk/events/ble_active_profile_changed.h': (367, '620e19b4b882d0cc5799ee1107d6b68dd99eb37b', 'aed2b7d6c3ba6a28319501cdcce25aee7d6fa6254c29a6bbc6b864cf8f81559b'),
}
STUBS = [
    'zephyr/device.h', 'zephyr/init.h', 'zephyr/kernel.h', 'zephyr/irq.h',
    'zephyr/logging/log.h', 'zephyr/settings/settings.h',
    'zephyr/sys/ring_buffer.h', 'zephyr/sys/util.h', 'zephyr/sys/atomic.h',
    'zephyr/bluetooth/addr.h', 'zephyr/bluetooth/bluetooth.h',
    'zephyr/bluetooth/conn.h', 'zephyr/bluetooth/hci.h',
    'zephyr/bluetooth/uuid.h', 'zephyr/bluetooth/gatt.h',
    'zephyr/bluetooth/hci_types.h', 'zmk/keys.h',
    'zmk/split/bluetooth/uuid.h', 'zmk/event_manager.h',
]
CASES = ['selection', 'fast', 'inactive', 'pairing', 'candidate-release',
         'candidate-pairing', 'pair-cancel', 'pair-failed', 'pair-race', 'roles', 'rpa-active', 'rpa-inactive',
         'rpa-expire', 'rpa-empty', 'retry', 'retry-already', 'work-failure',
         'reuse', 'failures']
COMMON = ['-std=c11', '-Wall', '-Wextra', '-Werror',
          '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
          '-fno-sanitize-recover=all']


def identity(data):
    return (len(data), hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest(),
            hashlib.sha256(data).hexdigest())


def verify_pin(commit, relative, data):
    assert commit == PIN, 'Unreviewed ZMK revision'
    assert identity(data) == EXPECTED[relative], 'Pinned BLE fixture changed: ' + relative


def must_reject(fn, label):
    try:
        fn()
    except AssertionError:
        return
    raise AssertionError('Inactive identity negative: ' + label)


def fixture_checks():
    meta = json.loads((MOCKS / 'fixtures.json').read_text())
    assert meta['commit'] == PIN and meta['tag'] == 'v0.3.0'
    assert set(meta['files']) == set(EXPECTED)
    for relative, expected in EXPECTED.items():
        entry = meta['files'][relative]
        assert (entry['size'], entry['git_blob'], entry['sha256']) == expected, relative
        data = (MOCKS / relative).read_bytes()
        verify_pin(meta['commit'], relative, data)
        must_reject(lambda: verify_pin('0' * 40, relative, data), relative + ': commit')
        must_reject(lambda: verify_pin(PIN, relative, data + b'\n'), relative + ': size')
        changed = bytes([data[0] ^ 1]) + data[1:]
        assert len(changed) == len(data)
        must_reject(lambda: verify_pin(PIN, relative, changed), relative + ': same-size blob/SHA')
    print('PASS BLE SDK pin: four original v0.3.0 files, size/Git-blob/SHA256; wrong revision, length and same-length content negatives', flush=True)


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run_case(binary, case, asan=False, negative=False):
    env = os.environ.copy()
    if asan:
        env['ASAN_OPTIONS'] = 'detect_stack_use_after_return=1:abort_on_error=0:exitcode=91:disable_coredump=1'
    result = subprocess.run([str(binary), case], cwd=ROOT, env=env, text=True,
                            capture_output=True, preexec_fn=no_core)
    if negative:
        assert result.returncode != 0 and 'Assertion' in result.stderr, (case, result.stdout, result.stderr)
        print('PASS live negative: unmodified stock BLE TU fails the new disconnect-inactive oracle', flush=True)
    else:
        if result.returncode:
            raise AssertionError(f'{binary.name} {case}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
        print(result.stdout.strip(), flush=True)
    return result.stdout


def compile_binary(temp, stubs, name, definitions, with_policy=False, asan=False):
    folder = temp / name
    folder.mkdir()
    flags = [*COMMON, '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if asan else [*COMMON, '-fsanitize=undefined']
    flags += [f'-D{k}={v}' for k, v in definitions.items()]
    flags += [f'-DTEST_POLICY_PRESENT={int(with_policy)}', '-I' + str(MOCKS),
              '-I' + str(stubs), '-I' + str(DRIVER), '-include', str(MOCKS / 'test_env.h')]
    sources = [MOCKS / 'test_env.c', ROOT / 'tests/test_ble_single_host.c']
    if definitions.get('CONFIG_ZMK_BLE', 1):
        sources.append(MOCKS / 'sdk_v0_3_0_ble.c')
    if with_policy:
        sources.append(DRIVER / 'ble_single_host.c')
    objects = []
    for index, source in enumerate(sources):
        target = folder / f'{index}-{source.stem}.o'
        extra = []
        if with_policy and source.name == 'sdk_v0_3_0_ble.c':
            extra = ['-include', str(DRIVER / 'ble_single_host_prefix.h')]
        subprocess.run([CC, *flags, *extra, '-c', str(source), '-o', str(target)], cwd=ROOT, check=True)
        if source.name == 'ble_single_host.c':
            undefined = subprocess.check_output(['nm', '-u', str(target)], text=True)
            forbidden = ['bt_le_adv_start', 'bt_le_adv_stop', 'bt_unpair', 'settings_save_one',
                         'settings_delete', 'zmk_endpoints_select_transport']
            for symbol in forbidden:
                assert symbol not in undefined, 'Policy unexpectedly mutates advertising/settings/route: ' + symbol
        objects.append(target)
    binary = folder / name
    subprocess.run([CC, *flags, *map(str, objects), '-o', str(binary)], cwd=ROOT, check=True)
    return binary


def config(settings=1, usb=1, ble=1, split=0, central=0, peripheral=0, flag=None):
    result = dict(CONFIG_SETTINGS=settings, CONFIG_ZMK_USB=usb, CONFIG_ZMK_BLE=ble,
                  CONFIG_ZMK_SPLIT=split, CONFIG_ZMK_SPLIT_BLE=split,
                  CONFIG_ZMK_SPLIT_ROLE_CENTRAL=central)
    if flag is not None:
        result['CONFIG_BBP9981_BLE_SINGLE_HOST'] = flag
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-only', action='store_true')
    parser.add_argument('--no-asan', action='store_true', help='Diagnostic-only local subset, not full acceptance')
    args = parser.parse_args()
    fixture_checks()
    BUILD.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='suite-', dir=BUILD) as directory:
        temp = Path(directory)
        stubs = temp / 'include'
        for relative in STUBS:
            target = stubs / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('#pragma once\n#include <test_env.h>\n')
        baseline = {}
        for settings in [0, 1]:
            binary = compile_binary(temp, stubs, f'stock{settings}', config(settings=settings))
            baseline[settings] = run_case(binary, 'stock')
            if settings == 1:
                run_case(binary, 'expect-policy', negative=True)
        if args.baseline_only:
            print('PASS stock-only BLE fixture baseline; production policy has NOT been compiled or accepted')
            return
        for path in ['ble_single_host.c', 'ble_single_host.h', 'ble_single_host_prefix.h']:
            assert (DRIVER / path).is_file(), 'Production service not ready: ' + path
        for settings in [0, 1]:
            for defined_zero in [False, True]:
                binary = compile_binary(temp, stubs, f'off{settings}{int(defined_zero)}',
                                        config(settings=settings, flag=0 if defined_zero else None), with_policy=True)
                output = run_case(binary, 'stock')
                old_trace = next(line for line in baseline[settings].splitlines() if line.startswith('TRACE '))
                new_trace = next(line for line in output.splitlines() if line.startswith('TRACE '))
                assert old_trace == new_trace, ('OFF changed actual stock trace', old_trace, new_trace)
        for settings in [0, 1]:
            for usb in [0, 1]:
                for central in [0, 1]:
                    binary = compile_binary(temp, stubs, f'on{settings}{usb}{central}',
                                            config(settings=settings, usb=usb, split=central,
                                                   central=central, flag=1), with_policy=True)
                    for case in CASES:
                        run_case(binary, case)
                    run_case(binary, 'pair-final-gate')
                    if settings:
                        run_case(binary, 'duplicate')
                        run_case(binary, 'same-index-peer')
                        run_case(binary, 'pair-backend-block')
        for settings in [0, 1]:
            binary = compile_binary(temp, stubs, f'bleoff{settings}', config(settings=settings, ble=0, flag=1), with_policy=True)
            run_case(binary, 'off')
        peripheral = compile_binary(temp, stubs, 'split-peripheral', config(split=1, peripheral=1, flag=1), with_policy=True)
        run_case(peripheral, 'stock')
        if not args.no_asan:
            binary = compile_binary(temp, stubs, 'asan-on', config(flag=1), with_policy=True, asan=True)
            for case in [*CASES, 'duplicate', 'same-index-peer', 'pair-final-gate', 'pair-backend-block']:
                run_case(binary, case, asan=True)
            off = compile_binary(temp, stubs, 'asan-off', config(flag=0), with_policy=True, asan=True)
            run_case(off, 'stock', asan=True)
    subprocess.run([os.sys.executable, '-B', str(ROOT / 'tests/test_ble_single_host_cmake.py')], cwd=ROOT, check=True)
    print('PASS single-host BLE: actual frozen SDK + real auth prefix/service, independent OFF traces/BLE-OFF/split-peripheral, settings/USB/split-central matrix, bounded retries and identity/pairing races; host mocks only, NOT radio/UI/device acceptance')


if __name__ == '__main__':
    main()
