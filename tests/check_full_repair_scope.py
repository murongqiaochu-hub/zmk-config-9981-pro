#!/usr/bin/env python3
"""Exact full-function candidate: two reviewed substitutions and one new service."""
from pathlib import Path
import hashlib, subprocess
ROOT = Path(__file__).resolve().parents[1]
BASE = '1c61b817f7e2d52fd2bbea888d1867f354e4c2a8'
P = 'config/boards/arm/bbp9981/custom_driver/'
CM = 'config/boards/arm/bbp9981/CMakeLists.txt'
RUNTIME = P + 'behavior_gamepad.c'
SERVICE = P + 'gamepad_ble_service.c'
SERVICE_SHA = '1bf7c4198b3bd6022f1b7aae653e838d687f0eee85375f25914661a5608136b6'
OLD_CM = b'''            # AFTER the existing mouse snapshot prefix. Tail attrs keep5/9/13.
            set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/hog.c"
                DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
                "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/gamepad_ble_extension.h")'''
NEW_CM = b'''            # A separate HIDS instance; never rewrite the stock keyboard/mouse HOG.
            zephyr_library_sources(custom_driver/gamepad_ble_service.c)'''
OLD_LAYER = b'    return zmk_keymap_highest_layer_active() == BBP_GAMEPAD_LAYER;'
NEW_LAYER = b'''    /* The SDK returns an order index; Studio can reorder stable layer IDs. */
    return zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()) == BBP_GAMEPAD_LAYER;'''
INACTIVE = {P+'gamepad_usb_neutral.c': 'b942152', P+'gamepad_ble_disabled.c': 'd05c9e1'}
def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)
def check(root=ROOT):
    base = set(git('ls-tree','-r','--name-only',BASE,'config').decode().splitlines())
    files = list((root/'config').rglob('*'))
    assert not any(p.is_symlink() for p in files), 'no config symlinks'
    actual = {p.relative_to(root).as_posix() for p in files if p.is_file()}
    assert actual == base | set(INACTIVE) | {SERVICE}, 'exact config path set'
    assert hashlib.sha256((root/SERVICE).read_bytes()).hexdigest() == SERVICE_SHA, 'reviewed service bytes'
    for p,rev in INACTIVE.items():
        assert (root/p).read_bytes() == git('show',rev+':'+p), p
        assert Path(p).name.encode() not in (root/CM).read_bytes(), 'inactive adapter compiled'
    projected = {}
    for p in sorted(base):
        data = (root/p).read_bytes()
        if p == CM:
            assert data.count(NEW_CM)==1 and OLD_CM not in data
            data = data.replace(NEW_CM,OLD_CM,1)
        if p == RUNTIME:
            assert data.count(NEW_LAYER)==1 and OLD_LAYER not in data
            data = data.replace(NEW_LAYER,OLD_LAYER,1)
        assert data == git('show',BASE+':'+p), 'unapproved original config drift: '+p
        projected[p]=data
    workflow = '.github/workflows/main.yml'
    assert (root/workflow).read_bytes() == git('show','1014c94:'+workflow), 'CI authorization gate drift'
    return projected
if __name__=='__main__':
    print('PASS complete repair exact scope:',len(check()),'original config files preserved after two explicit inverse substitutions; one reviewed service; inactive adapters unchanged')
