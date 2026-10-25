#!/usr/bin/env python3
"""Frozen legacy host on exact projection AND actual v233 split-runtime/service."""
from pathlib import Path
import os, shutil, subprocess, tempfile, resource
from check_full_repair_scope import check, ROOT, BASE, P, CM, RUNTIME, SERVICE, git, tree_paths

def run(args, **kw): return subprocess.run(args,check=True,**kw)
def legacy_host(view):
    with tempfile.TemporaryDirectory(prefix='v233-frozen-host-') as d:
        t=Path(d)
        # Copy tracked baseline artifacts, then current tests; never alter production.
        for name in tree_paths():
            if name.startswith('tests/'): continue
            p=t/name;p.parent.mkdir(parents=True,exist_ok=True)
            p.write_bytes(git('show',BASE+':'+name))
        shutil.copytree(ROOT/'tests',t/'tests',ignore=shutil.ignore_patterns('__pycache__'))
        # Private repository/index/HEAD: a shared .git would expose the actual
        # service path through git ls-files and invalidate the historical view.
        # Only object lookup is shared; every Git write below targets temp.
        env={k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
        env.update(BBP_REPAIR_PROJECTED='1',PYTHONDONTWRITEBYTECODE='1')
        common=Path(subprocess.check_output(
            ['git','rev-parse','--path-format=absolute','--git-common-dir'],
            cwd=ROOT,env=env,text=True).strip())
        assert common.is_absolute() and (common/'objects').is_dir()
        run(['git','init','-q',str(t)],env=env)
        info=t/'.git/objects/info';info.mkdir(parents=True,exist_ok=True)
        (info/'alternates').write_text(str(common/'objects')+'\n')
        run(['git','symbolic-ref','HEAD','refs/heads/frozen-v233'],cwd=t,env=env)
        run(['git','update-ref','refs/heads/frozen-v233',BASE],cwd=t,env=env)
        run(['git','read-tree',BASE],cwd=t,env=env)
        for name,data in view.items(): (t/name).write_bytes(data)
        indexed=subprocess.check_output(['git','ls-files','-z','config'],cwd=t,env=env)
        assert set(indexed.decode('utf-8').split('\0'))-{''}==set(view)
        assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=t,env=env)==git('rev-parse',BASE)
        print('RUN frozen historical full host on byte-exact 432d6c8 projection',flush=True)
        run([os.sys.executable,'-B',str(t/'tests/run_host_tests.py')],cwd=t,env=env)

def negatives():
    with tempfile.TemporaryDirectory(prefix='v233-scope-') as d:
        t=Path(d)
        for folder in ('config','tests','.github/workflows'):
            shutil.copytree(ROOT/folder,t/folder,ignore=shutil.ignore_patterns('__pycache__'))
        check(t)
        names=[CM,RUNTIME,SERVICE,'config/bbp9981.conf','config/bbp9981.keymap',
               'config/boards/arm/bbp9981/bbp9981.keymap',P+'a320_0x57.c',
               P+'trackpad_microtap_runtime.c',P+'trackpad_tap_output.c',
               P+'gamepad_usb_extension.h',P+'gamepad_ble_extension.h','.github/workflows/main.yml']
        for name in names:
            p=t/name; data=p.read_bytes();p.write_bytes(data+b'\n/* rejected drift */\n')
            try:
                try: check(t)
                except AssertionError: pass
                else: raise AssertionError('accepted mutation '+name)
            finally: p.write_bytes(data)
        print('PASS 12 independent scope negatives')

def actual_runtime():
    # Existing C oracle and env stay byte-exact. An added keymap header supplies
    # only the missing SDK mapping API, not the production layer predicate.
    build=ROOT/'.host-test-build';build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='v233-runtime-') as d:
        t=Path(d); inc=t/'include';inc.mkdir()
        names=['zephyr/device.h','zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/logging/log.h',
               'drivers/behavior.h','zmk/behavior.h','zmk/endpoints.h','zmk/keymap.h',
               'zmk/event_manager.h','zmk/events/layer_state_changed.h','zmk/events/endpoint_changed.h',
               'zmk/usb.h','zmk/events/usb_conn_state_changed.h','zephyr/bluetooth/conn.h',
               'zmk/ble.h','zmk/events/ble_active_profile_changed.h']
        for name in names:
            p=inc/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('/* test SDK include */\n')
        (inc/'zmk/keymap.h').write_text('''#pragma once
#include <stdint.h>
typedef uint8_t zmk_keymap_layer_id_t;
typedef uint8_t zmk_keymap_layer_index_t;
#define ZMK_KEYMAP_LAYER_ID_INVAL UINT8_MAX
static uint8_t layer_order[4]={0,1,2,3};
static inline uint8_t zmk_keymap_layer_index_to_id(uint8_t i) {
    return i<4 ? layer_order[i] : ZMK_KEYMAP_LAYER_ID_INVAL;
}
''')
        shutil.copytree(inc,build/'gamepad_runtime_include',dirs_exist_ok=True)
        import run_gamepad_tests
        run_gamepad_tests.main(repair_actual=True)  # Full actual core/runtime/driver/USB/HOG and six separate-TU links.
        flags=[os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
               '-Wno-unused-parameter','-Wno-unused-function','-fsanitize=undefined',
               '-fno-sanitize-recover=all','-I'+str(inc)]
        # Real split runtime and thin behavior, compiled together by frozen oracle.
        for name in ('test_gamepad_runtime','test_gamepad_refactor_api'):
            exe=t/name;run([*flags,str(ROOT/'tests'/(name+'.c')),'-o',str(exe)]);run([str(exe)])
        source=(ROOT/'tests/test_gamepad_layer_identity.c').read_text()
        # Wrap fixture reset rather than editing the frozen oracle/env.
        marker='#undef main\n'
        assert source.count(marker)==1
        source=source.replace(marker,marker+'''static void identity_fixture(bool ble) {
    for(unsigned i=0;i<4;i++) layer_order[i]=i;
    fixture(ble);
}
#define fixture identity_fixture
''',1)
        source=source.replace('assert(gamepad_runtime_original_main() == 0);',
          'for(unsigned i=0;i<4;i++) layer_order[i]=i;\n    assert(gamepad_runtime_original_main() == 0);')
        (t/'tests').mkdir()
        for name in ('test_gamepad_runtime.c','gamepad_runtime_env.h'):
            shutil.copy2(ROOT/'tests'/name,t/'tests'/name)
        shutil.copytree(ROOT/'config',t/'config')
        test=t/'tests/test_gamepad_layer_identity.c';test.write_text(source)
        exe=t/'identity';run([*flags,str(test),'-o',str(exe)]);run([str(exe)])
        p=t/RUNTIME;data=p.read_text();new='zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active())'
        assert data.count(new)==1;p.write_text(data.replace(new,'zmk_keymap_highest_layer_active()',1))
        exe=t/'counterfactual';run([*flags,str(test),'-o',str(exe)])
        def no_core(): resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        bad=subprocess.run([str(exe)],capture_output=True,text=True,preexec_fn=no_core)
        assert bad.returncode!=0 and 'bbp_gamepad_layer_active' in bad.stderr,bad.stderr
        print('PASS actual split runtime identity/reordering; same test rejects old production comparison')

def main():
    view=check()
    run([os.sys.executable,'-B',str(ROOT/'tests/check_manual_sync.py')])
    negatives();legacy_host(view);actual_runtime()
    run([os.sys.executable,'-B',str(ROOT/'tests/run_gamepad_ble_service_tests.py')])
    run([os.sys.executable,'-B',str(ROOT/'tests/test_full_repair_cmake.py')])
    print('PASS v233 host regressions: frozen legacy host + actual split runtime and HIDS; CMake status reported separately above (mandatory in CI); HOST ONLY')
if __name__=='__main__': main()
