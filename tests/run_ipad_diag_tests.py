#!/usr/bin/env python3
"""Additive diagnostic tests; no SDK files/oracles modified, no device access."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CC = os.environ.get('CC', 'cc')


def main():
    from ipad_diag_scope import selftest
    selftest()
    build = ROOT / '.host-test-build'
    assert not build.is_symlink()
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ipad-diag-', dir=build) as name:
        temp = Path(name).resolve()
        assert temp.parent == build.resolve() and temp.name.startswith('ipad-diag-')
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-fno-sanitize-recover=all']
        for sanitize in ('undefined', 'address,undefined'):
            exe = temp / ('runtime-' + sanitize)
            layout = ['-fno-pie', '-no-pie'] if sanitize.startswith('address') else []
            subprocess.run([CC, *flags, '-fsanitize=' + sanitize, *layout,
                            '-I' + str(ROOT / 'tests/ipad_diag_mocks'),
                            str(ROOT / 'tests/test_ipad_diag.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        for bad_flag, reason in [('-DCONFIG_LOG_MODE_DEFERRED=0', 'requires deferred logging'),
                                 ('-DCONFIG_LOG_BLOCK_IN_THREAD=1', 'must never block a producer')]:
            bad = subprocess.run([CC, *flags, bad_flag,
                                  '-I' + str(ROOT / 'tests/ipad_diag_mocks'),
                                  str(ROOT / 'tests/test_ipad_diag.c'), '-o', str(temp / 'negative')],
                                 capture_output=True, text=True)
            assert bad.returncode != 0 and reason in bad.stderr, bad.stderr
        overlay = temp / 'overlay/zmk'
        overlay.mkdir(parents=True)
        (overlay / 'hid.h').write_bytes((ROOT / 'tests/hid_snapshot_mocks/zmk/hid.h').read_bytes())
        legacy_path = ROOT / 'tests/test_gamepad_ble.c'
        legacy = legacy_path.read_text()
        # Mirror preserves ALL assertions; only enlarge the pre-existing unused
        # scratch buffer and add/undo the source-only notify alias around SDK code.
        scratch = '    uint8_t copied_desc[sizeof(zmk_hid_report_desc)], out[128];'
        assert legacy.count(scratch) == 1
        mirror = legacy.replace(scratch, scratch.replace('128', '512'))
        anchor = '#if BBP_GAMEPAD_TEST_WITH_SNAPSHOT\n#include "../config/'
        assert mirror.count(anchor) == 1
        mirror = mirror.replace(anchor, '#include "../config/boards/arm/bbp9981/custom_driver/ipad_diag_hog.h"\n' + anchor)
        anchor = '#undef bt_gatt_attr_read\n'
        assert mirror.count(anchor) == 1
        mirror = mirror.replace(anchor, anchor + '#undef bt_gatt_notify_cb\n')
        (temp / 'test_gamepad_ble.c').write_text(mirror)
        metadata = (ROOT / 'tests/test_hid_metadata.c').read_text()
        assert metadata.count('int main(int argc, char **argv)') == 1
        (temp / 'test_hid_metadata.c').write_text(metadata.replace('int main(int argc, char **argv)', 'int metadata_main(int argc, char **argv)'))
        (temp / 'test_ipad_diag_hog.c').write_bytes((ROOT / 'tests/test_ipad_diag_hog.c').read_bytes())
        includes = ['-I' + str(p) for p in [temp / 'overlay', ROOT / 'tests/gamepad_ble_mocks',
                                         ROOT / 'tests/hid_snapshot_mocks', ROOT / 'tests']]
        defines = ['-DCONFIG_ZMK_POINTING=1', '-DCONFIG_ZMK_HID_INDICATORS=1',
                   '-DCONFIG_ZMK_USB_BOOT=0', '-DCONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE=6',
                   '-DCONFIG_ZMK_HID_KEYBOARD_NKRO_EXTENDED_REPORT=0',
                   '-DCONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL=1',
                   '-DCONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_BASIC=0',
                   '-DCONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6',
                   '-DCONFIG_BBP9981_BLE_HID_METADATA_COMPAT=1',
                   '-DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=0', '-DCONFIG_ZMK_HID_REPORT_TYPE_HKRO=1',
                   '-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=0', '-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT=1']
        for on in (0, 1):
            exe = temp / ('hog-' + str(on))
            subprocess.run([CC, *flags, '-Wno-unused-parameter', '-fsanitize=undefined',
                            *defines, f'-DCONFIG_BBP9981_IPAD_DIAG={on}', *includes,
                            str(temp / 'test_ipad_diag_hog.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        assert legacy_path.read_text() == legacy
    print('PASS IPDIAG runtime UBSan/ASan and true-HOG observer ON/OFF; HOST ONLY')


if __name__ == '__main__':
    main()
