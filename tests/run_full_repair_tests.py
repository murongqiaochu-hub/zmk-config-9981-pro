#!/usr/bin/env python3
"""Actual layer contract, old-code counterfactual, exact scope mutations, new HIDS."""
from pathlib import Path
import os, shutil, subprocess, tempfile
from check_full_repair_scope import check, ROOT, P, CM, RUNTIME, SERVICE
BUILD=ROOT/'.host-test-build'
BUILD.mkdir(exist_ok=True)
flags=[os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(BUILD/'gamepad_runtime_include')]
exe=BUILD/'test_gamepad_layer_identity'
subprocess.run([*flags,str(ROOT/'tests/test_gamepad_layer_identity.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
# Same fixture/test/compiler; only undo the production expression in a private copy.
with tempfile.TemporaryDirectory(prefix='layer-counterfactual-',dir=BUILD) as d:
    t=Path(d); assert t.resolve().parent==BUILD.resolve()
    (t/'tests').mkdir()
    for name in ['test_gamepad_layer_identity.c','test_gamepad_runtime.c','gamepad_runtime_env.h']:
        shutil.copy2(ROOT/'tests'/name,t/'tests'/name)
    shutil.copytree(ROOT/P,t/P)
    p=t/RUNTIME;s=p.read_text();new='zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active())'
    assert s.count(new)==1
    p.write_text(s.replace(new,'zmk_keymap_highest_layer_active()',1))
    oldexe=t/'old-contract'
    subprocess.run([*flags,str(t/'tests/test_gamepad_layer_identity.c'),'-o',str(oldexe)],check=True)
    import resource
    def no_core(): resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    failed=subprocess.run([str(oldexe)],capture_output=True,text=True,preexec_fn=no_core)
    assert failed.returncode!=0 and 'bbp_gamepad_layer_active' in failed.stderr, failed.stderr
    print('PASS counterfactual: original layer-index comparison fails the identical reordered-layer assertion; fixed code passes')
with tempfile.TemporaryDirectory(prefix='repair-scope-',dir=BUILD) as d:
    t=Path(d); assert t.resolve().parent==BUILD.resolve()
    shutil.copytree(ROOT/'config',t/'config')
    (t/'.github/workflows').mkdir(parents=True)
    shutil.copy2(ROOT/'.github/workflows/main.yml',t/'.github/workflows/main.yml')
    check(t)
    mutations=[CM,RUNTIME,SERVICE,'config/bbp9981.conf','config/bbp9981.keymap','config/boards/arm/bbp9981/bbp9981.keymap',P+'a320_0x57.c',P+'trackpad_microtap_runtime.c',P+'trackpad_tap_output.c',P+'gamepad_usb_extension.h',P+'gamepad_ble_extension.h','.github/workflows/main.yml']
    for name in mutations:
        p=t/name;before=p.read_bytes();p.write_bytes(before+b'\n/* rejected drift */\n')
        try:
            try: check(t)
            except AssertionError: pass
            else: raise AssertionError('scope accepted mutation: '+name)
        finally: p.write_bytes(before)
    print('PASS full repair scope rejects',len(mutations),'independent byte mutations; worktree untouched')
subprocess.run([os.sys.executable,'-B',str(ROOT/'tests/run_gamepad_ble_service_tests.py')],check=True)
print('PASS full repair tests; host verification, not iPad or over-air acceptance')
