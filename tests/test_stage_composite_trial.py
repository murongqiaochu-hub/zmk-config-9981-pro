#!/usr/bin/env python3
"""Trial stager gates; local source checks, not firmware evidence."""
from pathlib import Path
import importlib.util
import sys
import tempfile
import os
import shutil
import subprocess
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import stage_composite_trial as trial
import stage_composite_candidate as old

def reject(call):
    try: call()
    except (ValueError,FileExistsError): return
    raise AssertionError('unsafe stage accepted')

def main():
    with tempfile.TemporaryDirectory(prefix='9981-trial-gate-') as temp:
        target=Path(temp)/'new-stage'
        reject(lambda:trial.stage(target))
        reject(lambda:trial.stage(target,acknowledge_hid_database_change=True))
        reject(lambda:trial.stage(target,accept_experimental_device_trial=True))
        if not trial.TRIAL_SAFETY_REVIEWED:
            reject(lambda:trial.stage(target,accept_experimental_device_trial=True,acknowledge_hid_database_change=True))
        assert not target.exists()
        # Exercise source-only staging with the review decision mocked in this
        # process. This is NOT approval of the trial and never configures CMake.
        with patch.object(trial,'TRIAL_SAFETY_REVIEWED',True):
            manifest=trial.stage(target,accept_experimental_device_trial=True,acknowledge_hid_database_change=True)
        assert manifest['profile']=='ble-composite-trial' and manifest['device_test_ready'] is False
        assert manifest['build_performed'] is False and manifest['flashed'] is False
        assert {x['path'] for x in manifest['modified_config']}=={trial.GAMEPAD,trial.BOARD_CMAKE}
        cmake=(target/trial.GAMEPAD).read_text()
        assert 'composite_ble_trial.h' in cmake and 'composite_ble_candidate.h' not in cmake
        assert 'composite_trial_lifecycle.c' in cmake
        assert 'BBP9981_COMPOSITE_DEVICE_TRIAL_LIFECYCLE=1' in cmake
        assert 'zephyr_library_compile_definitions' not in cmake
        assert cmake.count('"-DBBP9981_COMPOSITE_DEVICE_TRIAL=1"')==1
        cmake_bin=os.environ.get('CMAKE') or shutil.which('cmake')
        isolated='/dev/shm/dsh-runtime/9981-cmake-3.31-isolated/cmake/data/bin/cmake'
        if not cmake_bin and Path(isolated).is_file(): cmake_bin=isolated
        assert cmake_bin, 'CMake required for same-process/reused-build guard test'
        build=Path(temp)/'cmake-build';build.mkdir()
        script=Path(temp)/'fresh-guard.cmake'
        board_file=target/trial.BOARD_CMAKE
        script.write_text('set(CMAKE_BINARY_DIR "'+str(build)+'")\n'+
                          ('include("'+str(board_file)+'")\n')*2)
        first=subprocess.run([cmake_bin,'-P',str(script)],cwd=ROOT,text=True,capture_output=True)
        assert first.returncode==0,first.stdout+first.stderr
        reused=subprocess.run([cmake_bin,'-P',str(script)],cwd=ROOT,text=True,capture_output=True)
        assert reused.returncode!=0 and 'NEW build directory and fresh configure' in reused.stderr
        assert 'bbp-composite-trial-configured' in (target/trial.BOARD_CMAKE).read_text()
        # Real CMake property regression: Zephyr already tracks the absolute
        # .conf/keymap paths. The original ../ lock spelling produced duplicate
        # Ninja regeneration outputs. Keep a failing alias control, without
        # configuring firmware or weakening the content hash checks.
        locked={name:(target/name).read_bytes() for name in trial.LOCKED_SOURCE}
        guard=trial.locked_source_guard(locked)
        original_alias=guard.replace('get_filename_component(_composite_source_path ',
                                     'set(_composite_source_path ').replace(' ABSOLUTE)',')')
        for label,content,expect_alias in [('old-alias',original_alias,True),('normalized',guard,False)]:
            deps=Path(temp)/(label+'.deps')
            dependency_script=Path(temp)/(label+'.cmake')
            dependency_script.write_text('set(CMAKE_CURRENT_SOURCE_DIR "'+str(target/trial.BOARD)+'")\n'+
                'set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "'+str(target/'config/bbp9981.conf')+'" "'+str(target/'config/bbp9981.keymap')+'")\n'+
                content+'get_property(deps DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)\n'+
                'file(WRITE "'+str(deps)+'" "${deps}")\n')
            checked=subprocess.run([cmake_bin,'-P',str(dependency_script)],cwd=ROOT,text=True,capture_output=True)
            assert checked.returncode==0,checked.stdout+checked.stderr
            paths=set(deps.read_text().split(';'))
            resolved={str(Path(path).resolve()) for path in paths}
            assert (len(paths)!=len(resolved))==expect_alias,(label,paths)
            if not expect_alias: assert all('/../' not in path for path in paths)
        # Normalization must not turn the hash guard into a merely structural check.
        changed=target/'config/bbp9981.conf';before=changed.read_bytes()
        changed.write_bytes(before+b'\n# tampered trial input\n')
        mismatch=subprocess.run([cmake_bin,'-P',str(dependency_script)],cwd=ROOT,text=True,capture_output=True)
        assert mismatch.returncode!=0 and 'Composite staged source changed' in mismatch.stderr
        changed.write_bytes(before)
        assert (target/trial.TAP).read_bytes()==(ROOT/trial.TAP).read_bytes()
        for path in ['config/bbp9981.conf','config/bbp9981.keymap',trial.BOARD+'/bbp9981.keymap']:
            assert (target/path).read_bytes()==(ROOT/path).read_bytes()
        with patch.object(trial,'TRIAL_SAFETY_REVIEWED',True):
            reject(lambda:trial.stage(target,accept_experimental_device_trial=True,acknowledge_hid_database_change=True))
            reject(lambda:trial.stage(ROOT/'bad-stage',accept_experimental_device_trial=True,acknowledge_hid_database_change=True))
    assert old.CANDIDATE.endswith('/composite_ble_candidate.h')
    assert 'NOT CMAKE_SCRIPT_MODE_FILE' in old.HOST_ONLY_BOARD_GUARD
    assert trial.CANDIDATE.endswith('/composite_ble_trial.h')
    assert trial.LIFECYCLE.endswith('/composite_trial_lifecycle.c')
    assert trial.CANDIDATE in trial.LOCKED_SOURCE and trial.LIFECYCLE in trial.LOCKED_SOURCE
    assert 'BBP9981_COMPOSITE_DEVICE_TRIAL=1' in trial.NEW_PREFIX
    assert 'composite_ble_candidate.h' not in trial.NEW_PREFIX
    assert 'att.c' in trial.PRECONDITION and 'CONFIG_BT_EATT' in trial.PRECONDITION
    assert 'GIT_CONFIG_COUNT' in trial.GIT_PRELUDE
    print('PASS trial explicit gates and unchanged old host-only guard; positive stage requires actual reviewed trial')
if __name__=='__main__': main()
