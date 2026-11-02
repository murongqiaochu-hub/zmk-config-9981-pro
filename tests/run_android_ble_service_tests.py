#!/usr/bin/env python3
"""Actual independent Android HIDS + frozen stock HOG, HOST ONLY.

Only temporary copies of legacy plumbing are adapted to the new 10-byte wire
ABI. Frozen oracle/SDK files are hash-checked, never rewritten. The production
service and codec are compiled, not a hand-built replacement service. Bluetooth,
CCC scheduling, notification completion, radio and linker ordering remain mocks.
"""
from pathlib import Path
import hashlib
import os
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
DRIVER = ROOT / "config/boards/arm/bbp9981/custom_driver"
SERVICE = DRIVER / "gamepad_ble_service.c"
HEADER = DRIVER / "gamepad_android_report.h"
FROZEN = {
    "test_gamepad_ble.c": "cde962a29b02e5c576be1508e4ad031759cf7e5f3ee6229dfce95db181a5d66d",
    "test_gamepad_ble_service.c": "413aaa8c83aab1a634665bdf44342250ba68ccc6fcd88a2106c0dfc619e5b3b6",
    "gamepad_ble_mocks/sdk_v0_3_0_hog.c": "2e8c12bb4197173d238a6be1e33abf2db5dedbd4a6cf29a83d5e2f6c79d4919b",
    "gamepad_ble_service_mocks/zmk/hid.h": "dbc9b4b9dba737df899e2123c2584bb617b9d156b82ceaf36074470166420e24",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text, old, new):
    assert text.count(old) == 1, (old, text.count(old))
    return text.replace(old, new, 1)


def between(text, start, end):
    assert text.count(start) == text.count(end) == 1, (start, end)
    return text[text.index(start):text.index(end)]


def fixtures(service):
    old = (TESTS / "test_gamepad_ble.c").read_text()
    plumbing = old[:old.index("static struct bbp_gamepad_report_body read_gamepad(")]
    plumbing = replace_once(plumbing,
        '#include "../config/boards/arm/bbp9981/custom_driver/gamepad_ble_extension.h"',
        f'#include "{service}"')
    plumbing = plumbing.replace('../config/', str(ROOT / 'config') + '/')
    plumbing = replace_once(plumbing, '&hog_svc.attrs[hog_svc.attr_count - 3U]',
                            '&zz_bbp_gamepad_svc.attrs[8]')
    plumbing = replace_once(plumbing, '&hog_svc.attrs[hog_svc.attr_count - 2U]',
                            '&zz_bbp_gamepad_svc.attrs[9]')
    plumbing = replace_once(plumbing, 'assert(params->len == 9U);',
                            'assert(params->len == 10U);')
    plumbing = replace_once(plumbing, '(*subscription_hook)(void);',
                            '(*subscription_hook)(void), (*invalidation_hook)(void);')
    plumbing = replace_once(plumbing, 'report_hook = active_hook = subscription_hook = NULL;',
                            'report_hook = active_hook = subscription_hook = invalidation_hook = NULL;')
    plumbing = replace_once(plumbing, '    current_report = bbp_gamepad_neutral();\n}',
                            '    current_report = bbp_gamepad_neutral();\n    run_hook(&invalidation_hook);\n}')
    plumbing = replace_once(plumbing, 'peers[i].dst.a.val[0] = (uint8_t)i;',
                            'peers[i].dst.a.val[0] = (uint8_t)i;\n'
                            '        bbp_gamepad_ble_conn_callbacks.connected(&peers[i], 0);')
    plumbing += between(old, 'static void bump_epoch(void)',
                        'static void test_pinned_service_old_attrs_and_tail(void)')
    stock = between(old, 'static void test_pinned_service_old_attrs_and_tail(void)',
                    'static void test_tail_contract_fails_closed(void)')
    stock = stock[:stock.index('    assert(hog_svc.attr_count == next + 4U);')]
    stock += '    assert(hog_svc.attr_count == next);\n}\n'
    plumbing += stock + between(old, 'static void test_original_stock_senders_remain(void)',
                                'int main(void)')
    # Preserve applicable pre-existing assertions byte-for-byte. They do not
    # compare the old nine-byte transport format or obsolete tail positions.
    preserved = between(old, 'static void test_read_wrong_peer_route_and_reconnected_object(void)',
                        'static void test_send_value_body_and_caller_reference(void)')
    preserved += between(old, 'static void test_send_refuses_unsubscribed_or_not_connected(void)',
                         'static void test_trigger_only_requires_current_context_and_peer(void)')
    preserved += between(old, 'static void test_send_rechecks_epoch_and_active_before_submission(void)',
                         'static void test_neutral_releases_only_old_captured_peer(void)')
    preserved += between(old, 'static void test_zero_filled_is_not_neutral_and_errors_are_passthrough(void)',
                         'static void test_original_stock_senders_remain(void)')
    independent = (TESTS / 'test_gamepad_ble_service.c').read_text()
    preserved += between(independent, 'static ssize_t cp(struct bt_conn *conn, uint8_t value)',
                         'int main(void)')
    return plumbing, preserved


def no_core_dump():
    # Negative controls intentionally assert; never leave core files in source.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    for relative, expected in FROZEN.items():
        assert sha(TESTS / relative) == expected, 'frozen input changed: ' + relative
    production_before = {str(p.relative_to(ROOT)): sha(p) for p in (SERVICE, HEADER)}
    print('HOST ONLY: actual service+codec; frozen stock hog.c; strict UBSan/ASan, snapshot=0/1', flush=True)
    for name, value in production_before.items():
        print('SOURCE_SHA256', name, value, flush=True)
    with tempfile.TemporaryDirectory(prefix='bbp-android-hids-') as directory:
        temp = Path(directory)
        def run(service, sanitizer, snapshot, label, negative=False):
            plumbing, preserved = fixtures(service)
            (temp / 'android_plumbing.h').write_text(plumbing)
            (temp / 'android_preserved_checks.h').write_text(preserved)
            exe = temp / label
            command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', f'-fsanitize={sanitizer}',
                       '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                       f'-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT={snapshot}']
            if sanitizer == 'address':
                command += ['-fno-pie', '-no-pie']
            for path in (TESTS / 'android_ble_mocks', TESTS / 'gamepad_ble_service_mocks',
                         TESTS / 'gamepad_ble_mocks', TESTS, DRIVER, temp):
                command += ['-I' + str(path)]
            command += [str(TESTS / 'test_android_ble_service.c'), '-o', str(exe)]
            subprocess.run(command, cwd=temp, check=True)
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            result = subprocess.run([str(exe)], cwd=temp, env=env, text=True,
                                    capture_output=True, preexec_fn=no_core_dump)
            if negative:
                assert result.returncode != 0 and 'Assertion' in result.stderr, (
                    label, result.returncode, result.stdout, result.stderr)
                print('PASS negative', label, 'rejected by assertion', flush=True)
            else:
                if result.returncode:
                    print(result.stdout, end='')
                    print(result.stderr, end='')
                    result.check_returncode()
                print(f'PASS {label}: {result.stdout.strip()}', flush=True)
        for sanitizer in ('undefined', 'address'):
            for snapshot in (0, 1):
                run(SERVICE, sanitizer, snapshot, f'{sanitizer}-snapshot{snapshot}')
        service_text = SERVICE.read_text()
        header_text = HEADER.read_text()
        mutations = {
            'wrong-input-id': ('header', '0xa1, 0x01, 0x85, 0x04, 0x15, 0x00',
                              '0xa1, 0x01, 0x85, 0x03, 0x15, 0x00'),
            'wrong-wire-length': ('service', '.attr = value, .data = wire, .len = sizeof(wire),',
                                  '.attr = value, .data = wire, .len = sizeof(wire)-1U,'),
            'legacy-tail-value': ('service', 'const struct bt_gatt_attr *value = &service->attrs[8];',
                                  'const struct bt_gatt_attr *value = &service->attrs[service->attr_count-3U];'),
            'wrong-output-ref': ('service', '{BBP_ANDROID_OUTPUT_ID, 0x02}',
                                '{BBP_ANDROID_OUTPUT_ID, 0x01}'),
            'wrong-neutral-axis': ('header', '{15U, 0U, 0U, 128U, 128U, 128U, 128U, 0U, 0U, 0U}',
                                   '{15U, 0U, 0U, 127U, 128U, 128U, 128U, 0U, 0U, 0U}'),
        }
        for label, (kind, before, after) in mutations.items():
            mutant = temp / ('mutant-' + label + '.c')
            source = service_text
            if kind == 'header':
                changed_header = temp / ('mutant-' + label + '.h')
                changed_header.write_text(replace_once(header_text, before, after))
                source = replace_once(source, '#include "gamepad_android_report.h"',
                                      f'#include "{changed_header}"')
            else:
                source = replace_once(source, before, after)
            mutant.write_text(source)
            run(mutant, 'undefined', 1, label, negative=True)
    for relative, expected in FROZEN.items():
        assert sha(TESTS / relative) == expected, 'frozen input changed during run: ' + relative
    assert production_before == {str(p.relative_to(ROOT)): sha(p) for p in (SERVICE, HEADER)}, \
        'production changed during test; rerun exact reviewed snapshot'
    print('PASS total: 4 positive sanitizer/snapshot builds; 5 compiled negative controls; '
          'frozen inputs/production bytes unchanged; no ARM/radio/OS acceptance', flush=True)


if __name__ == '__main__':
    main()
