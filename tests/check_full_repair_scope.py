#!/usr/bin/env python3
"""Exact v2.3.3 repair inverse projection; historical proofs are not relaxed."""
from pathlib import Path
from functools import lru_cache
import hashlib, subprocess
ROOT=Path(__file__).resolve().parents[1]
BASE='432d6c8'
P='config/boards/arm/bbp9981/custom_driver/'
CM='config/boards/arm/bbp9981/cmake/gamepad.cmake'
RUNTIME=P+'gamepad_runtime.c'
SERVICE=P+'gamepad_ble_service.c'
SERVICE_SHA='1bf7c4198b3bd6022f1b7aae653e838d687f0eee85375f25914661a5608136b6'
OLD_LAYER=b'    return zmk_keymap_highest_layer_active() == BBP_GAMEPAD_LAYER;'
NEW_LAYER=b'''    /* The SDK returns an order index; Studio can reorder stable layer IDs. */
    return zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()) == BBP_GAMEPAD_LAYER;'''
OLD_CM=b'''        # AFTER the existing mouse snapshot prefix. Tail attrs keep5/9/13.
        set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/hog.c"
            DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
            "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/gamepad_ble_extension.h")'''
NEW_CM=b'''        # A separate HIDS instance; never rewrite the stock keyboard/mouse HOG.
        zephyr_library_sources(custom_driver/gamepad_ble_service.c)'''
def git(*args):
    return subprocess.check_output(['git',*args],cwd=ROOT)
def tree_paths(*paths):
    return [p for p in git('ls-tree','-r','--name-only','-z',BASE,*paths).decode('utf-8').split('\0') if p]
@lru_cache(None)
def original(path): return git('show',BASE+':'+path)
def check(root=ROOT):
    paths=set(tree_paths('config'))
    files=list((root/'config').rglob('*'))
    assert not any(p.is_symlink() for p in files),'config symlink'
    actual={p.relative_to(root).as_posix() for p in files if p.is_file()}
    assert actual==paths|{SERVICE},('config paths',actual^(paths|{SERVICE}))
    assert hashlib.sha256((root/SERVICE).read_bytes()).hexdigest()==SERVICE_SHA,'service bytes'
    view={}
    for p in sorted(paths):
        data=(root/p).read_bytes()
        if p in (CM,RUNTIME):
            old,new=(OLD_CM,NEW_CM) if p==CM else (OLD_LAYER,NEW_LAYER)
            assert data.count(new)==1 and old not in data,('exact substitution',p)
            data=data.replace(new,old,1)
        assert data==original(p),'unapproved config drift: '+p
        view[p]=data
    for p in tree_paths('tests'):
        if Path(p).suffix in ('.c','.h'):
            assert (root/p).read_bytes()==original(p),'frozen oracle/fixture: '+p
    for p in tree_paths('.github/workflows'):
        assert (root/p).read_bytes()==original(p),'workflow drift: '+p
    return view
if __name__=='__main__':
    print('PASS exact v233 inverse projection:',len(check()),'baseline config files; frozen C/H and workflows')
