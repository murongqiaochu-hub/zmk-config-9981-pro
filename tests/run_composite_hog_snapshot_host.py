#!/usr/bin/env python3
"""HOST ONLY: fixed real snapshot preinclude + composite preinclude + HOG.

Compile both cursor-steady modes, including the observed 20/5/20 queue sizes.
The ATT/CCC/connection/notification/workqueue/locks are simplified mocks;
the 196-byte stock map is SHA-verified input, not the short HID mock's map.
No real radio, asynchronous Zephyr scheduler, security or OS is exercised.
"""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from run_composite_hog_host import check_fixed, EXPECTED_REFS, ROOT
from run_composite_map_host import PURE_SHA, validate

SNAPSHOT_SHA = '1fa43bbb07c8cda86fb7a8ce5e715bb07a7910d2c00edab5be81435a84627d93'
DIAG_SHA = 'df1f618fd272feca0f3bf28bfc6528a0750add6e3e51f226e478548445a5e843'


def verify_snapshot():
    fixed = {
        'config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h': SNAPSHOT_SHA,
        'config/boards/arm/bbp9981/custom_driver/menu_click_diag.h': DIAG_SHA,
    }
    for name, expected in fixed.items():
        actual = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        if actual != expected:
            raise AssertionError(f'snapshot source changed: {name} ({actual})')


def main():
    if not __debug__:
        raise RuntimeError('the oracle must not run under Python -O')
    verify_snapshot()
    stock = check_fixed()
    cc = shlex.split(os.environ.get('CC', 'cc'))
    assert cc, 'empty CC'
    source = ROOT/'tests/test_composite_hog_snapshot_host.c'
    flags = [*cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
             '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
             '-I'+str(ROOT/'tests/gamepad_ble_mocks')]
    guards = ['-DBBP_COMPOSITE_HOST_ONLY=1', '-DBBP_COMPOSITE_HOG_HOST_ONLY=1']
    base_config = ['-DCONFIG_ZMK_POINTING=1', '-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=0',
                   '-DCONFIG_ZMK_HID_INDICATORS=1', '-DCONFIG_ZMK_BLE_THREAD_STACK_SIZE=512',
                   '-DCONFIG_ZMK_BLE_THREAD_PRIORITY=5', '-DCONFIG_ZMK_BLE_INIT_PRIORITY=50',
                   '-DCONFIG_ZMK_LOG_LEVEL=0']
    queue4 = ['-DCONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE=4',
              '-DCONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE=4',
              '-DCONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE=4']
    variants = [
        ('steady-off-4-4-4', queue4),
        ('steady-on-4-4-4', [*queue4, '-DCONFIG_A320_CURSOR_STEADY=1']),
        ('steady-on-20-5-20', ['-DCONFIG_A320_CURSOR_STEADY=1',
                               '-DCONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE=20',
                               '-DCONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE=5',
                               '-DCONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE=20']),
        ('steady-diag-tap-studio-on-20-5-20',
         ['-DCONFIG_A320_CURSOR_STEADY=1', '-DCONFIG_BBP9981_MENU_DIAG=1',
          '-DCONFIG_A320_TAP_OUTPUT=1', '-DCONFIG_ZMK_STUDIO=1',
          '-DCONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE=20',
          '-DCONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE=5',
          '-DCONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE=20']),
    ]
    with tempfile.TemporaryDirectory(prefix='9981-snapshot-composite-') as directory:
        temp = Path(directory)
        assert temp.resolve().parent == Path(tempfile.gettempdir()).resolve()
        assert temp.name.startswith('9981-snapshot-composite-') and not temp.is_symlink()
        # This temporary prelude supplies only the official peek declaration that
        # the fixed gamepad kernel mock lacks. Implementation lives in the test.
        prelude = temp/'peek-declaration.h'
        prelude.write_text('#include <zephyr/kernel.h>\nint k_msgq_peek(struct k_msgq *, void *);\n')
        snapshot = ROOT/'config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h'
        composite = ROOT/'experiments/gamepad_protocol/composite_hog_host.h'
        # Real compiler preincludes, in the same relative order as the board's
        # tap_extensions.cmake then gamepad.cmake: snapshot first, composite second.
        preincludes = ['-include', str(prelude), '-include', str(snapshot), '-include', str(composite)]
        for missing, expected in [(guards[0], 'requires BBP_COMPOSITE_HOST_ONLY=1'),
                                  (guards[1], 'requires BBP_COMPOSITE_HOG_HOST_ONLY=1')]:
            result = subprocess.run([*flags, *base_config, *queue4,
                                     *[v for v in guards if v != missing], *preincludes,
                                     '-c', str(source), '-o', str(temp/'guard.o')],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            assert result.returncode != 0 and expected in result.stderr, result.stderr
        missing_snapshot = subprocess.run([*flags, *base_config, *queue4, *guards,
                                           '-include', str(prelude), '-include', str(composite),
                                           '-c', str(source), '-o', str(temp/'guard.o')],
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert missing_snapshot.returncode != 0 and 'real snapshot force-include is required' in missing_snapshot.stderr
        print('PASS force-include order: actual snapshot then composite; missing snapshot or either host flag fails', flush=True)
        for variant, config in variants:
            for sanitizer in ('undefined', 'address'):
                exe = temp/(variant+'-'+sanitizer)
                sanitizers = ['-fsanitize='+sanitizer]
                if sanitizer == 'undefined':
                    sanitizers.append('-fno-sanitize-recover=all')
                print(f'RUN {variant} {sanitizer}', flush=True)
                subprocess.run([*flags,*base_config,*guards,*config,*preincludes,*sanitizers,str(source),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],input=stock,check=True)
                raw = subprocess.check_output([str(exe),'--map'],input=stock)
                pure = subprocess.check_output([str(exe),'--pure'],input=stock)
                refs = [tuple(map(int,row.split())) for row in
                        subprocess.check_output([str(exe),'--refs'],input=stock).decode().splitlines()]
                assert refs==EXPECTED_REFS, refs
                assert len(pure)==182 and hashlib.sha256(pure).hexdigest()==PURE_SHA
                assert raw==stock+pure[:7]+b'\x04\x15\x00'+pure[8:] and len(raw)==380
                validate(raw,stock,pure,[(report_id,typ) for report_id,typ,_ in refs])
                print(f'PASS {variant} {sanitizer}: pinned HOG fixture refs match the full composite map',flush=True)
        assert check_fixed()==stock
        verify_snapshot()
    print('LIMIT mock queues/scheduler/connection/notify only; no real Zephyr CCC, async races, epoch, ARM, OS or radio')


if __name__=='__main__':
    main()
