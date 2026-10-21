#!/usr/bin/env python3
"""v2.3.1 permits ONE production fix, not a new refactor/keymap baseline.
The original v2.2.9 refactor reference remains unchanged in refactor_support.py.
"""
from pathlib import Path
import subprocess
from refactor_support import ROOT, DRIVER

STABILITY_BASE = '2b2c7dc3cacfc64e5ec4a2b64c306f1caf2a1d6e'
SNAPSHOT = DRIVER + 'trackpad_usb_snapshot.h'


def reference(path):
    return subprocess.check_output(['git','show',STABILITY_BASE+':'+path],cwd=ROOT)


def verify_stability_scope():
    paths=subprocess.check_output(['git','ls-tree','-r','--name-only',STABILITY_BASE,'config'],
                                  cwd=ROOT,text=True).splitlines()
    current={str(p.relative_to(ROOT)) for p in (ROOT/'config').rglob('*') if p.is_file()}
    assert current==set(paths),'Stability release must not add/remove config files'
    changed={p for p in paths if (ROOT/p).read_bytes()!=reference(p)}
    assert changed=={SNAPSHOT},('Unreviewed production scope',changed)
    # Real SDK source fixtures and all pre-existing behavior oracles stay exact.
    for path in ['tests/sdk_v0_3_0_usb.c','tests/sdk_v0_3_0_usb_hid.c',
                 'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c',
                 'tests/test_pointer.c','tests/test_micro_runtime.c',
                 'tests/test_gamepad_runtime.c','tests/test_keyboard_light_memory.c',
                 'tests/test_trackpad_light_memory.c']:
        assert (ROOT/path).read_bytes()==reference(path),path
    return len(paths)-1


if __name__=='__main__':
    count=verify_stability_scope()
    print(f'PASS stability scope: {count} production configs byte-identical to v2.3.0; only USB click snapshot RAM/protocol gate may change; frozen SDK and behavior oracles retained')
