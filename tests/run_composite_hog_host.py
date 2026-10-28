#!/usr/bin/env python3
"""Compile the pinned HOG TU and GATT mock with one host-only composite table.

The HID mock descriptor is only 45 bytes. Its map callback is intercepted ONLY
for that exact object/length and the actual map attribute; SHA-checked stock196
is supplied by this runner. CCC/ATT and notification behavior are deterministic
C mocks, not a Zephyr stack, security implementation, radio or OS acceptance.
"""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from run_composite_map_host import PURE_SHA, parse, sample_validate, validate, must_reject

ROOT = Path(__file__).resolve().parents[1]
FIXED = {
    'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c': '2e8c12bb4197173d238a6be1e33abf2db5dedbd4a6cf29a83d5e2f6c79d4919b',
    'tests/gamepad_ble_mocks/zephyr/bluetooth/gatt.h': '8ade9c0e5c5fdbc1739bab8c28631c76a5b64e8e660a663696fcb5ce462d29c3',
    'tests/gamepad_ble_mocks/zmk/hid.h': 'f117b4dee81bc873b7f08b99f1182a39c663fd9b822bb76080ee291a188e43d6',
}
STOCK_SHA = '5c7e4106019e5baf9027c8a24e334c58f5e09c65cd3ff46b338c551c61da4ee3'
EXPECTED_REFS = [(1,1,8), (2,1,12), (3,1,16), (1,2,19), (5,2,24), (4,1,28)]


def check_fixed():
    for name, expected in FIXED.items():
        actual = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
        if actual != expected:
            raise AssertionError(f'pinned fixture changed: {name} ({actual})')
    stock = bytes.fromhex((ROOT / 'tests/hid_map_fixtures/stock_196.hex').read_text())
    assert len(stock) == 196 and hashlib.sha256(stock).hexdigest() == STOCK_SHA
    return stock


def main():
    if not __debug__:
        raise RuntimeError('the oracle must not run under Python -O')
    stock = check_fixed()
    cc = shlex.split(os.environ.get('CC', 'cc'))
    assert cc, 'empty CC'
    common = [*cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
              '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
              '-I' + str(ROOT / 'tests/gamepad_ble_mocks')]
    model = ROOT / 'tests/test_composite_hog_host.c'
    guards = ['-DBBP_COMPOSITE_HOST_ONLY=1', '-DBBP_COMPOSITE_HOG_HOST_ONLY=1']
    with tempfile.TemporaryDirectory(prefix='9981-composite-hog-') as directory:
        temp = Path(directory)
        # Verify the exact temporary-directory target before its automatic cleanup.
        assert temp.resolve().parent == Path(tempfile.gettempdir()).resolve()
        assert temp.name.startswith('9981-composite-hog-') and not temp.is_symlink()
        for omitted, expected in [(guards[0], 'requires BBP_COMPOSITE_HOST_ONLY=1'),
                                  (guards[1], 'requires BBP_COMPOSITE_HOG_HOST_ONLY=1')]:
            for wrong in (None, omitted[:-1] + '0'):
                flags = [g for g in guards if g != omitted] + ([wrong] if wrong else [])
                cmd = [*common, *flags, '-c', str(model), '-o', str(temp/'guard.o')]
                result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                assert result.returncode != 0 and expected in result.stderr, (omitted, wrong, result.stderr)
        for pointing, indicators, smooth in ((0,1,0), (1,0,0), (1,1,1)):
            source = '#include "' + str(ROOT/'experiments/gamepad_protocol/composite_hog_host.h') + '"\n'
            cmd = [*cc, '-I' + str(ROOT/'tests/gamepad_ble_mocks'), *guards,
                   '-DCONFIG_ZMK_POINTING=' + str(pointing),
                   '-DCONFIG_ZMK_HID_INDICATORS=' + str(indicators),
                   '-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=' + str(smooth), '-E', '-x', 'c', '-']
            result = subprocess.run(cmd, input=source, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.PIPE, text=True)
            assert result.returncode != 0 and 'requires pointing=1, indicators=1, smooth=0' in result.stderr
        print('PASS dual host-only compile gates (omitted/zero) and fixed-configuration guards', flush=True)
        for sanitizer in ('undefined', 'address'):
            exe = temp / ('hog-' + sanitizer)
            flags = ['-fsanitize=' + sanitizer]
            if sanitizer == 'undefined':
                flags.append('-fno-sanitize-recover=all')
            subprocess.run([*common, *flags, *guards, str(model), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], input=stock, check=True)
            print('PASS ' + sanitizer + ' host build/run', flush=True)

            def dump(arg):
                return subprocess.check_output([str(exe), arg], input=stock)
            raw, pure, sample = (dump('--map'), dump('--pure'), dump('--sample'))
            refs_with_index = [tuple(map(int, row.split())) for row in dump('--refs').decode().splitlines()]
            assert refs_with_index == EXPECTED_REFS, refs_with_index
            assert len(pure) == 182 and hashlib.sha256(pure).hexdigest() == PURE_SHA
            expected = stock + pure[:7] + b'\x04\x15\x00' + pure[8:]
            assert raw == expected and len(raw) == 380
            refs = [(report_id, report_type) for report_id, report_type, _ in refs_with_index]
            fields = validate(raw, stock, pure, refs)
            sample_validate(sample, fields)
            assert sample == bytes([15,4,64,255,128,128,128,255,0,0])

            no_min = raw[:204] + b'\x55\x00' + raw[206:]
            hat = next(f for f in parse(no_min)[1][4,1]
                       if f['page'] == 1 and f['usage'] == 0x39 and not f['flags'] & 1)
            assert len(no_min) == 380 and hat['lo'] == -32768
            must_reject(no_min, stock, pure, refs, 'missing global logical minimum reset')
            collision = raw[:203] + b'\x03' + raw[204:]
            totals, _, _, owners = parse(collision)
            assert totals[3,1] == 152 and len(owners[3,1]) == 2
            must_reject(collision, stock, pure, refs, 'mouse/gamepad input collision')
            must_reject(raw, stock, pure, refs[:-1], 'missing gamepad reference')
            must_reject(raw, stock, pure, refs + [(4,1)], 'duplicate gamepad reference')
            must_reject(raw, stock, pure, [(1,1),(2,1),(3,1),(1,1),(5,2),(4,1)], 'missing LED ref')
            must_reject(raw, stock, pure, [(1,1),(2,1),(3,1),(1,2),(5,2),(3,1)], 'wrong gamepad input id')
            must_reject(raw, stock, pure, [(1,1),(2,1),(3,1),(1,2),(5,1),(4,1)], 'wrong output type')
            print('PASS actual GATT refs versus complete HID stream; collision/minimum/reference negatives', flush=True)
        assert check_fixed() == stock, 'fixed fixtures changed during test'
    print('LIMIT host-only: mock per-peer CCC/ATT/synchronous notify; no real Zephyr, ARM, OS, radio, pairing or device acceptance')


if __name__ == '__main__':
    main()
