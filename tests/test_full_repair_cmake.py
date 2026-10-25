#!/usr/bin/env python3
"""Actual new-service CMake traces versus exact 432d6c8 projection, 32 cases."""
from pathlib import Path
import itertools, tempfile, os, shutil
from check_full_repair_scope import ROOT, check, CM, P
from test_refactor_cmake import HARNESS, run, events, BOARD

def main():
    view=check()
    if shutil.which('cmake') is None:
        assert not os.environ.get('CI'), 'CI must execute actual new-service CMake32+9'
        print('SKIP actual CMake32+9 locally unavailable; mandatory in CI')
        return
    with tempfile.TemporaryDirectory(prefix='repair-cmake-') as d:
        t=Path(d);old=t/'old';sdk=t/'sdk'
        for name,data in view.items():
            if name.startswith(BOARD+'/'):
                p=old/Path(name).relative_to(BOARD);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        fixtures={'src/hog.c':'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c',
                  'src/usb_hid.c':'tests/sdk_v0_3_0_usb_hid.c',
                  'src/usb.c':'tests/sdk_v0_3_0_usb.c',
                  'include/zmk/hid.h':'tests/hid_snapshot_mocks/zmk/hid.h'}
        for name,source in fixtures.items():
            p=sdk/'app'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/source).read_bytes())
        harness=t/'contract.cmake';harness.write_text(HARNESS)
        for gp,tap,usb,ble,micro in itertools.product((0,1),repeat=5):
            flags=dict(CONFIG_BBP9981_GAMEPAD=gp,CONFIG_A320_TAP_OUTPUT=tap,CONFIG_ZMK_USB=usb,
                       CONFIG_ZMK_BLE=ble,CONFIG_A320_MICROTAP=micro)
            before=run(harness,old,sdk,flags);after=run(harness,ROOT/BOARD,sdk,flags)
            assert before.returncode==after.returncode==0,(flags,before.stderr,after.stderr)
            a=events(before,old,sdk);b=events(after,ROOT/BOARD,sdk)
            legacy=[line for line in a if 'gamepad_ble_extension.h' in line]
            service='-- EVENT source custom_driver/gamepad_ble_service.c'
            assert len(legacy)==gp*ble and b.count(service)==gp*ble,flags
            assert not any('gamepad_ble_extension.h' in line for line in b),flags
            expected=[service if line in legacy else line for line in a]
            assert b==expected,(flags,expected,b)
        flags=dict(CONFIG_BBP9981_GAMEPAD=1,CONFIG_A320_TAP_OUTPUT=1,CONFIG_ZMK_USB=1,
                   CONFIG_ZMK_BLE=1,CONFIG_A320_MICROTAP=1)
        for failure in ('missing_revision','zephyr_revision','zephyr_dirty','hal_revision','hal_dirty'):
            result=run(harness,ROOT/BOARD,sdk,flags,failure)
            assert result.returncode!=0,failure
        for name in fixtures:
            p=sdk/'app'/name;data=p.read_bytes();p.write_bytes(data+b'\n/* drift */\n')
            result=run(harness,ROOT/BOARD,sdk,flags)
            assert result.returncode!=0 and 'pinned ZMK source changed' in result.stderr,name
            p.write_bytes(data)
    print('PASS actual new-service CMake32: only BLE prefix replaced, all other trace/order and 9 SDK negative gates preserved')
if __name__=='__main__':main()
