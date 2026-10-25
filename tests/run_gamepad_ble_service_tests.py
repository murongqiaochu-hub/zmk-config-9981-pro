#!/usr/bin/env python3
"""Strict UBSan host contracts for independent Game Pad HIDS + real stock hog.c.

Reuse the archival test's host plumbing and unchanged transport assertions in a
TEMPORARY header, not its obsolete preinclude/map-extension assertions. No file
in the archival suite is modified. The actual production service and pinned
hog.c are included unchanged. Radio/encryption/linker ordering are not emulated.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"


def between(text, start, end):
    assert text.count(start) == 1 and text.count(end) == 1, (start, end)
    return text[text.index(start):text.index(end)]


def fixture():
    old = (TESTS / "test_gamepad_ble.c").read_text()
    plumbing = old[:old.index("static void test_pinned_service_old_attrs_and_tail(void)")]
    plumbing = plumbing.replace('gamepad_ble_extension.h', 'gamepad_ble_service.c')
    plumbing = plumbing.replace('../config/', str(ROOT / 'config') + '/')
    plumbing = plumbing.replace('&hog_svc.attrs[hog_svc.attr_count - 3U]',
                                '&zz_bbp_gamepad_svc.attrs[zz_bbp_gamepad_svc.attr_count - 3U]')
    plumbing = plumbing.replace('&hog_svc.attrs[hog_svc.attr_count - 2U]',
                                '&zz_bbp_gamepad_svc.attrs[zz_bbp_gamepad_svc.attr_count - 2U]')
    plumbing = plumbing.replace('(*subscription_hook)(void);',
                                '(*subscription_hook)(void), (*invalidation_hook)(void);')
    plumbing = plumbing.replace('report_hook = active_hook = subscription_hook = NULL;',
                                'report_hook = active_hook = subscription_hook = invalidation_hook = NULL;')
    marker_reset = '    current_report = bbp_gamepad_neutral();\n}'
    assert plumbing.count(marker_reset) == 1
    plumbing = plumbing.replace(marker_reset,
        '    current_report = bbp_gamepad_neutral();\n    run_hook(&invalidation_hook);\n}')
    marker = 'peers[i].dst.a.val[0] = (uint8_t)i;'
    assert plumbing.count(marker) == 1
    plumbing = plumbing.replace(marker, marker + '\n        bbp_gamepad_ble_conn_callbacks.connected(&peers[i], 0);')
    stock = between(old, 'static void test_pinned_service_old_attrs_and_tail(void)',
                    'static void test_tail_contract_fails_closed(void)')
    stock = stock[:stock.index('    assert(hog_svc.attr_count == next + 4U);')]
    stock += '    assert(hog_svc.attr_count == next);\n}\n'
    tail = between(old, 'static void test_tail_contract_fails_closed(void)',
                   'static void test_report_map_all_offsets_crossing_and_end(void)')
    tail = tail.replace('hog_svc', 'zz_bbp_gamepad_svc')
    transport = between(old, 'static void test_read_nine_byte_body_and_offsets(void)', 'int main(void)')
    return plumbing + stock + tail + transport


def main():
    with tempfile.TemporaryDirectory(prefix='bbp-hids-') as directory:
        temp = Path(directory)
        (temp / 'legacy_contract_fixture.h').write_text(fixture())
        # Real196 configuration, plus its existing mouse-snapshot integration.
        # Do not claim smooth/indicator-off descriptors match real196 hardware.
        for snapshot in (0, 1):
            exe = temp / f'service-{snapshot}'
            command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                       f'-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT={snapshot}',
                       '-I' + str(TESTS / 'gamepad_ble_service_mocks'),
                       '-I' + str(TESTS / 'gamepad_ble_mocks'), '-I' + str(TESTS), '-I' + str(temp),
                       str(TESTS / 'test_gamepad_ble_service.c'), '-o', str(exe)]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(exe)], cwd=ROOT, check=True)
    print('PASS independent HIDS: strict UBSan real196+141, snapshot=0/1; HOST ONLY')


if __name__ == '__main__':
    main()
