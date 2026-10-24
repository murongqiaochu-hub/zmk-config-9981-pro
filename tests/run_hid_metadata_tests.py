#!/usr/bin/env python3
"""Additive real-SDK HID metadata tests; no device, pairing or radio claims."""
from pathlib import Path
import hashlib
import itertools
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = '1c61b817f7e2d52fd2bbea888d1867f354e4c2a8'
DRIVER = 'config/boards/arm/bbp9981/'
CC = os.environ.get('CC', 'cc')


def scope_checks():
    from hid_metadata_scope import selftest
    selftest()
    changed = set(subprocess.check_output(
        ['git', 'diff', '--name-only', BASE, '--', 'config'], cwd=ROOT, text=True).splitlines())
    required = {'config/bbp9981.conf', DRIVER + 'Kconfig.board',
                DRIVER + 'custom_driver/gamepad_ble_extension.h'}
    assert required <= changed <= required | {DRIVER + 'CMakeLists.txt'}, changed
    subprocess.run(['git', 'diff', '--exit-code', BASE, '--', 'tests/test_gamepad_ble.c',
                    'tests/run_gamepad_tests.py', 'tests/gamepad_ble_mocks',
                    'tests/hid_snapshot_mocks', 'tests/sdk_v0_3_0_usb_hid.c',
                    'tests/sdk_v0_3_0_usb.c'], cwd=ROOT, check=True)
    conf = (ROOT / 'config/bbp9981.conf').read_text()
    for entry in ['CONFIG_BBP9981_BLE_HID_METADATA_COMPAT=y', 'CONFIG_BT_DIS_PNP_VID_SRC=2',
                  'CONFIG_BT_DIS_SW_REV=y', 'CONFIG_BT_DIS_SW_REV_STR="2.2.1-ipad-hid1"']:
        assert entry in conf, entry
    fixtures = {
        'tests/hid_snapshot_mocks/zmk/hid.h': 'dd161a6631a9dad59f320389a0c146712b2c953215f4a97a8d0d64b0b3ffdee7',
        'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c': '2e8c12bb4197173d238a6be1e33abf2db5dedbd4a6cf29a83d5e2f6c79d4919b',
        'tests/sdk_v0_3_0_usb_hid.c': '3c7a934a2b5eac808889cd14cbc2d8b40e1dcc35a4ce0fbf6e6ac3a12b51ac8a',
        'tests/sdk_v0_3_0_usb.c': '4b182d9a942c6a09d4c6849f2c9504fd251b7bf5d1cd741e5d05dc82534c138e',
    }
    for name, digest in fixtures.items():
        assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest, name


def check_map(raw, nkro, smooth):
    pos = size = count = report = 0
    stack, bits = [], {}
    while pos < len(raw):
        prefix = raw[pos]
        pos += 1
        assert prefix != 0xfe, 'Unexpected HID long item'
        length = (0, 1, 2, 4)[prefix & 3]
        assert pos + length <= len(raw), 'Truncated HID item'
        value = int.from_bytes(raw[pos:pos + length], 'little')
        pos += length
        kind, tag = (prefix >> 2) & 3, prefix >> 4
        if kind == 1:
            if tag == 7:
                size = value
            elif tag == 8:
                report = value
            elif tag == 9:
                count = value
            elif tag == 10:
                stack.append((size, count, report))
            elif tag == 11:
                size, count, report = stack.pop()
        elif kind == 0 and tag == 8:
            bits[report] = bits.get(report, 0) + size * count
    assert not stack
    assert bits == {1: 120 if nkro else 64, 2: 96, 3: 72, 4: 72}, bits
    if not nkro and not smooth:
        assert len(raw) == 337, len(raw)  # Actual stock196 + unchanged gamepad141.


def main():
    scope_checks()
    build = ROOT / '.host-test-build'
    assert not build.is_symlink(), 'Refuse symlinked build directory'
    build.mkdir(exist_ok=True)
    assert build.resolve() == ROOT / '.host-test-build'
    with tempfile.TemporaryDirectory(prefix='hid-metadata-', dir=build) as directory:
        owned = Path(directory).resolve()
        assert owned.parent == build.resolve() and owned.name.startswith('hid-metadata-')
        overlay = owned / 'overlay'
        target = overlay / 'zmk/hid.h'
        target.parent.mkdir(parents=True)
        target.write_bytes((ROOT / 'tests/hid_snapshot_mocks/zmk/hid.h').read_bytes())
        # Compile a private mirror: the unused legacy raw-Info combination has
        # a 128-byte scratch buffer, smaller than the real 196-byte descriptor.
        # Enlarge ONLY that uniquely anchored declaration; preserve every oracle.
        canonical = ROOT / 'tests/test_gamepad_ble.c'
        legacy = canonical.read_bytes()
        legacy_sha = hashlib.sha256(legacy).hexdigest()
        before = b'    uint8_t copied_desc[sizeof(zmk_hid_report_desc)], out[128];'
        after = b'    uint8_t copied_desc[sizeof(zmk_hid_report_desc)], out[512];'
        assert legacy.count(before) == 1 and legacy.count(after) == 0
        mirrored = legacy.replace(before, after, 1)
        assert mirrored.replace(after, before, 1) == legacy
        (owned / 'test_gamepad_ble.c').write_bytes(mirrored)
        harness = owned / 'test_hid_metadata.c'
        harness.write_bytes((ROOT / 'tests/test_hid_metadata.c').read_bytes())
        includes = ['-I' + str(p) for p in [overlay, ROOT / 'tests/gamepad_ble_mocks',
                                          ROOT / 'tests/hid_snapshot_mocks', ROOT / 'tests']]
        common = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                  '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                  '-DCONFIG_ZMK_POINTING=1', '-DCONFIG_ZMK_HID_INDICATORS=1',
                  '-DCONFIG_ZMK_USB_BOOT=0', '-DCONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE=6',
                  '-DCONFIG_ZMK_HID_KEYBOARD_NKRO_EXTENDED_REPORT=0',
                  '-DCONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL=1',
                  '-DCONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_BASIC=0',
                  '-DCONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6']
        cases = [(on, nkro, smooth, snapshot, 'undefined')
                 for on, nkro, smooth, snapshot in itertools.product((0, 1), repeat=4)]
        cases.append((1, 0, 0, 1, 'address,undefined'))
        for index, (on, nkro, smooth, snapshot, sanitizer) in enumerate(cases):
            exe = owned / ('case-' + str(index))
            flags = [f'-DCONFIG_BBP9981_BLE_HID_METADATA_COMPAT={on}',
                     f'-DCONFIG_ZMK_HID_REPORT_TYPE_NKRO={nkro}',
                     f'-DCONFIG_ZMK_HID_REPORT_TYPE_HKRO={1 - nkro}',
                     f'-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING={smooth}',
                     f'-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT={snapshot}',
                     '-fsanitize=' + sanitizer]
            # Debian GCC ASan+PIE intermittently crashes before main in this host:
            # an empty control program reproduces it; non-PIE keeps ASan enabled.
            # Do not retry failed firmware tests or suppress sanitizer diagnostics.
            asan_layout = ['-fno-pie', '-no-pie'] if sanitizer.startswith('address') else []
            subprocess.run([CC, *common, *flags, *asan_layout, *includes, str(harness),
                            '-o', str(exe)], cwd=owned, check=True)
            subprocess.run([str(exe)], cwd=owned, check=True)
            check_map(subprocess.check_output([str(exe), '--descriptor'], cwd=owned), nkro, smooth)
            if not on and not nkro and not smooth and not snapshot:
                # Expected assertion abort must not leave a core outside our owned temp tree.
                import resource
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
                bad = subprocess.run([str(exe), '--require-corrected-info'], cwd=owned,
                                     text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                assert bad.returncode != 0 and 'required corrected HID Information' in bad.stderr, bad
                print('PASS live negative oracle: OFF rejected specifically at corrected Info assertion')
        assert canonical.read_bytes() == legacy
        assert hashlib.sha256(canonical.read_bytes()).hexdigest() == legacy_sha
        print('PASS canonical legacy oracle unchanged SHA256=' + legacy_sha +
              '; private copy differs only by unique unused scratch-buffer capacity')
    print('PASS HID metadata: 16 UBSan ON/OFF/HKRO/NKRO/smooth/snapshot cases + native ASan; '
          'full target337B map, IDs1/2/3/4 body64/96/72/72; old oracles/USB sources frozen; HOST ONLY')


if __name__ == '__main__':
    main()
