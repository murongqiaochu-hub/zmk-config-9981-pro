#!/usr/bin/env python3
"""Test isolated staging, real stock HOG TU compilation, and service registration.
Native ELF marker tests validate macro placement, NOT an ARM iterable table.
"""
from pathlib import Path
import hashlib
import importlib.util
import itertools
import json
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('pure_stage',ROOT/'tools/stage_pure_gamepad.py')
stager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stager)

def check_cmake(target, temp):
    if shutil.which('cmake') is None:
        assert not os.environ.get('CI'), 'CI must execute candidate CMake contracts'
        print('SKIP candidate CMake interpreter: cmake unavailable; native prefix tests still run')
        return
    from test_refactor_cmake import HARNESS, run
    sdk=temp/'sdk'
    fixtures={'src/hog.c':'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c',
              'src/usb_hid.c':'tests/sdk_v0_3_0_usb_hid.c',
              'src/usb.c':'tests/sdk_v0_3_0_usb.c',
              'include/zmk/hid.h':'tests/hid_snapshot_mocks/zmk/hid.h'}
    for name, source in fixtures.items():
        path=sdk/'app'/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes((ROOT/source).read_bytes())
    harness=temp/'candidate.cmake'
    recorder='''function(zephyr_compile_definitions)
  message(STATUS "EVENT compile_definitions ${ARGV}")
endfunction()
'''
    harness.write_text(HARNESS.replace('include("${BOARD_ROOT}/CMakeLists.txt")',
                                      recorder+'include("${BOARD_ROOT}/CMakeLists.txt")'))
    board=target/'config/boards/arm/bbp9981'
    required=['CONFIG_BBP9981_GAMEPAD','CONFIG_ZMK_BLE','CONFIG_A320_TAP_OUTPUT','CONFIG_ZMK_POINTING']
    flags=dict(CONFIG_ZMK_USB=1,CONFIG_A320_MICROTAP=1,CONFIG_BBP9981_CURSOR_SPEED=1)
    for values in itertools.product([0,1],repeat=4):
        current={**flags,**dict(zip(required,values))}
        result=run(harness,board,sdk,current)
        if not all(values):
            assert result.returncode!=0 and 'Pure BLE candidate requires' in result.stderr, result.stderr
            continue
        assert result.returncode==0,result.stderr
        lines=result.stdout.splitlines()
        assert lines.count('-- EVENT compile_definitions BBP9981_PURE_BLE_CANDIDATE=1')==1
        assert lines.count('-- EVENT source '+str(board)+'/../../../../experiments/gamepad_protocol/pure_runtime.c')==1
        hog=[x for x in lines if x.startswith('-- EVENT property') and '/src/hog.c;' in x]
        assert len(hog)==1 and 'pure_hog_guard.h' in hog[0], hog
        assert not any('gamepad_ble_extension.h' in x or 'trackpad_hog_snapshot.h' in x for x in lines)
        baseline=run(harness,ROOT/'config/boards/arm/bbp9981',sdk,current)
        assert baseline.returncode==0,baseline.stderr
        def usb_properties(output, root):
            return [x.replace(str(root),'BOARD') for x in output.splitlines()
                    if x.startswith('-- EVENT property') and '/src/usb' in x]
        assert usb_properties(result.stdout,board)==usb_properties(baseline.stdout,ROOT/'config/boards/arm/bbp9981')
    good={**flags,**dict.fromkeys(required,1)}
    for failure in ['missing_revision','zephyr_revision','zephyr_dirty','hal_revision','hal_dirty']:
        assert run(harness,board,sdk,good,failure).returncode!=0, failure
    for name in fixtures:
        path=sdk/'app'/name;original=path.read_bytes();path.write_bytes(original+b'\n/* changed */\n')
        result=run(harness,board,sdk,good)
        assert result.returncode!=0 and 'pinned ZMK source changed' in result.stderr,name
        path.write_bytes(original)
    print('PASS executed candidate CMake: 16 precondition combinations, exact single-TU prefix/source wiring, USB trace unchanged, 5 identity and 4 SHA negatives; recording Zephyr API model, not ARM configure')

def main():
    with tempfile.TemporaryDirectory(prefix='9981-profile-') as directory:
        temp = Path(directory)
        target = temp/'candidate'
        manifest = stager.stage(target)
        assert {x['path'] for x in manifest['modified_config']} == {stager.TAP,stager.GAMEPAD}
        assert not manifest['build_performed'] and not manifest['identity_changed']
        assert not manifest['usb_changed'] and not manifest['keymap_changed']
        for name, details in manifest['files'].items():
            assert hashlib.sha256((target/name).read_bytes()).hexdigest()==details['sha256']
            if name.startswith('config/') and name not in (stager.TAP,stager.GAMEPAD):
                assert (target/name).read_bytes()==(ROOT/name).read_bytes()
        original = (target/'CANDIDATE.json').read_bytes()
        try:
            stager.stage(target)
        except FileExistsError:
            pass
        else:
            raise AssertionError('stage overwrote existing directory')
        assert (target/'CANDIDATE.json').read_bytes()==original
        try:
            stager.stage(ROOT/'forbidden-stage')
        except ValueError:
            pass
        else:
            raise AssertionError('stage inside live tree accepted')
        descriptor=(ROOT/'experiments/gamepad_protocol/stadia_descriptor.h').read_bytes()
        try:
            stager.descriptor_digest(descriptor.replace(b'0x05, 0x01',b'0x05, 0x02',1))
        except ValueError:
            pass
        else:
            raise AssertionError('changed descriptor silently retained old manifest hash')
        previous = stager.OLD_BLE
        stager.OLD_BLE = 'intentionally-unrecognized-integration'
        try:
            try:
                stager.stage(temp/'bad')
            except ValueError:
                pass
            else:
                raise AssertionError('unknown integration accepted')
            assert not (temp/'bad').exists()
        finally:
            stager.OLD_BLE = previous
        result = subprocess.run([os.sys.executable,'-B',str(ROOT/'tools/stage_pure_gamepad.py'),
                                 '--out',str(temp/'unconfirmed')],capture_output=True,text=True)
        assert result.returncode!=0 and not (temp/'unconfirmed').exists()
        print('PASS stage: two exact CMake substitutions; every other config byte preserved; no overwrite/source-tree write/heuristic fallback/implicit profile')

        check_cmake(target,temp)

        # Give the faithful mock iterable macro a native section marker. Stock
        # HOG must NOT instantiate it; the separate pure HIDS TU MUST do so.
        probe = temp/'iterable_probe.h'
        probe.write_text('''#include <zephyr/bluetooth/gatt.h>
#undef STRUCT_SECTION_ITERABLE
#define STRUCT_SECTION_ITERABLE(type,name) struct type name __attribute__((section(".bbp_test_registered_hids")))
''')
        flags = [os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                 '-Wno-unused-parameter','-Wno-unused-function','-ffunction-sections','-fdata-sections',
                 '-fsanitize=undefined','-fno-sanitize-recover=all',
                 '-I'+str(ROOT/'tests/gamepad_ble_mocks'),'-I'+str(ROOT/'tests'),
                 '-include',str(probe)]
        source = (ROOT/'tests/test_gamepad_pure_hog_guard.c').read_text()
        source = source.replace('../experiments/gamepad_protocol/pure_hog_guard.h',
                                str(target/'experiments/gamepad_protocol/pure_hog_guard.h'))
        guarded = temp/'guarded_hog.c';guarded.write_text(source)
        marker = b'.bbp_test_registered_hids'
        cases = [[],['-DCONFIG_ZMK_HID_INDICATORS=0'],
                 ['-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=1'],
                 ['-DCONFIG_ZMK_POINTING=0'],
                 ['-DCONFIG_ZMK_POINTING=0','-DCONFIG_ZMK_HID_INDICATORS=0']]
        for index, extra in enumerate(cases):
            obj=temp/f'guarded{index}.o';exe=temp/f'guarded{index}'
            subprocess.run([*flags,*extra,'-c',str(guarded),'-o',str(obj)],check=True)
            assert marker not in obj.read_bytes(), 'stock HOG registered in pure profile'
            subprocess.run([flags[0],'-fsanitize=undefined',str(obj),'-Wl,--gc-sections','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        pure=temp/'pure.o'
        subprocess.run([*flags,'-c',str(ROOT/'tests/test_gamepad_pure_hids.c'),'-o',str(pure)],check=True)
        assert marker in pure.read_bytes(), 'pure service failed to use iterable registration'
        # Counterfactual: reintroducing iterable registration into the guard must
        # produce the forbidden section. Only the disposable staged copy changes.
        guard_path=target/'experiments/gamepad_protocol/pure_hog_guard.h'
        guard=guard_path.read_text()
        old='const struct bt_gatt_service_static _name = BT_GATT_SERVICE(attr_##_name)'
        assert guard.count(old)==1
        guard_path.write_text(guard.replace(old,'const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, _name) = BT_GATT_SERVICE(attr_##_name)'))
        bad=temp/'bad_registration.o'
        subprocess.run([*flags,'-c',str(guarded),'-o',str(bad)],check=True)
        assert marker in bad.read_bytes(), 'registration regression counterfactual was not observable'
        guard_path.write_text(guard)
        print('PASS native registration probe: stock HOG excluded in 5 configurations; pure HIDS registered; old-service registration counterfactual detected; not ARM evidence')
        wiring=temp/'wired'
        subprocess.run([flags[0],'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                        '-fsanitize=undefined','-fno-sanitize-recover=all','-DBBP_EXP_TEST_REAL_WIRING=1',
                        '-I'+str(ROOT/'tests/gamepad_ble_mocks'),
                        str(ROOT/'tests/test_gamepad_pure_transport.c'),'-o',str(wiring)],check=True)
        subprocess.run([str(wiring)],check=True)

if __name__ == '__main__':
    main()
