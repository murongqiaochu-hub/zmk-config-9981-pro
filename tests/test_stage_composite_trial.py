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
