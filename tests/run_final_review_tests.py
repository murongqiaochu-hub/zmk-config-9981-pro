#!/usr/bin/env python3
"""Final v2.3.1 freeze + conditional recovery boundaries; no device operations."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FROZEN_BUILD = 'd607df8b55ab676aa8910e787fb0a18e9e221389'


def main():
    paths = subprocess.check_output(['git','ls-tree','-r','--name-only',FROZEN_BUILD,'config'],
                                    cwd=ROOT,text=True).splitlines()
    from pointer_steady_scope import verified_legacy_view
    actual = verified_legacy_view()
    assert set(actual) == set(paths), 'Historical seal view must not add or remove config files'
    for path in paths:
        before = subprocess.check_output(['git','show',FROZEN_BUILD+':'+path],cwd=ROOT)
        assert actual[path] == before, 'Historical firmware seal changed: '+path
    print(f'PASS historical v2.3.1 seal: verified feature-OFF view restores all {len(paths)} configs to ARM build {FROZEN_BUILD}; enabled v2.3.2 is separately scoped/tested, NOT binary-identical',flush=True)
    build = ROOT/'.host-test-build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='final-review-',dir=build) as folder:
        for boot in [0,1]:
            exe = Path(folder)/f'usb{boot}'
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                '-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable',
                '-fsanitize=undefined','-fno-sanitize-recover=all',f'-DCONFIG_ZMK_USB_BOOT={boot}',
                '-I'+str(ROOT/'tests/sdk_snapshot_mocks'),str(ROOT/'tests/test_final_usb_recovery.c'),
                '-o',str(exe)],cwd=ROOT,check=True)
            modes = ['keyboard','consumer','mouse','mutable-mouse']
            if boot: modes.append('boot-keyboard')
            for mode in modes: subprocess.run([str(exe),mode],cwd=ROOT,check=True)
    print('PASS final boundaries: 4500 simulated abort/reconnect cycles; native completion can restore shared permit, NOT automatic FAULT recovery or hardware proof; mutable native DMA risk deliberately remains')


if __name__ == '__main__':
    main()
