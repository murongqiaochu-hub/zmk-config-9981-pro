#!/usr/bin/env python3
"""Execute old/new board CMake with recording Zephyr APIs and reviewed SDK stubs.
No SDK tree is edited. Tests both positive prefix ordering and fatal identity gates.
"""
from pathlib import Path
import itertools
import os
import shutil
import subprocess
import tempfile
from refactor_support import ROOT, BASE, reference

BOARD = 'config/boards/arm/bbp9981'
HARNESS = r'''
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CURRENT_SOURCE_DIR "${BOARD_ROOT}")
set(ZEPHYR_BASE "${SDK_ROOT}/zephyr")
set(APPLICATION_SOURCE_DIR "${SDK_ROOT}/app")
set(CMAKE_SOURCE_DIR "${SDK_ROOT}/app")
set(CONFIG_BOARD_BBP9981 ON)
set(CONFIG_BBP9981_MENU_DIAG OFF)
function(zephyr_library)
  message(STATUS "EVENT library")
endfunction()
function(zephyr_library_sources)
  foreach(source IN LISTS ARGN)
    message(STATUS "EVENT source ${source}")
  endforeach()
endfunction()
function(zephyr_library_include_directories)
  message(STATUS "EVENT include ${ARGV}")
endfunction()
function(set_property)
  message(STATUS "EVENT property ${ARGV}")
endfunction()
function(execute_process)
  set(args ${ARGV})
  list(FIND args RESULT_VARIABLE result_index)
  math(EXPR result_index "${result_index}+1")
  list(GET args ${result_index} result_name)
  set(result 0)
  if("${ARGV}" MATCHES "rev-parse")
    list(FIND args OUTPUT_VARIABLE out_index)
    math(EXPR out_index "${out_index}+1")
    list(GET args ${out_index} out_name)
    if("${ARGV}" MATCHES "hal/nordic")
      set(revision "884c4d61746bc35fbd379c169fc87ddb56c6461d")
      if(FAILURE STREQUAL "hal_revision")
        set(revision "0000000000000000000000000000000000000000")
      endif()
    else()
      set(revision "dacab4875df72109b96cc8977547a0dc04875bcd")
      if(FAILURE STREQUAL "zephyr_revision")
        set(revision "0000000000000000000000000000000000000000")
      endif()
      if(FAILURE STREQUAL "missing_revision")
        set(revision "")
        set(result 1)
      endif()
    endif()
    set(${out_name} "${revision}" PARENT_SCOPE)
  elseif("${ARGV}" MATCHES "hal/nordic" AND FAILURE STREQUAL "hal_dirty")
    set(result 1)
  elseif(NOT "${ARGV}" MATCHES "hal/nordic" AND FAILURE STREQUAL "zephyr_dirty")
    set(result 1)
  endif()
  set(${result_name} ${result} PARENT_SCOPE)
endfunction()
include("${BOARD_ROOT}/CMakeLists.txt")
'''


def run(harness, board, sdk, flags, failure=''):
    command = ['cmake', f'-DBOARD_ROOT={board}', f'-DSDK_ROOT={sdk}', f'-DFAILURE={failure}']
    command += [f'-D{k}={int(v)}' for k,v in flags.items()]
    return subprocess.run([*command, '-P', str(harness)], text=True, capture_output=True)


def events(result, board, sdk):
    lines = [line for line in result.stdout.splitlines() if line.startswith('-- EVENT')]
    # Preserve the two original refactor exceptions. v2.3.3's one always-built
    # opaque adapter is separately exact-scoped and asserted below, not hidden
    # by changing the immutable historical BASE or accepting arbitrary sources.
    lines = [line for line in lines if line not in [
        '-- EVENT source custom_driver/trackpad_mode.c', '-- EVENT source custom_driver/gamepad_runtime.c',
        '-- EVENT source custom_driver/behavior_cursor_speed.c']]
    return [line.replace(str(board), 'BOARD').replace(str(sdk), 'SDK') for line in lines]


def main():
    from user_preferences_scope import verified_v232_view
    verified_v232_view()  # Verify the one new adapter before projecting its event.
    if shutil.which('cmake') is None:
        assert not os.environ.get('CI'), 'CI must execute the real CMake contract matrix'
        print('SKIP executed CMake matrix locally: cmake unavailable; exact expanded-token gate still runs; CI requires real CMake')
        return
    with tempfile.TemporaryDirectory(prefix='9981-cmake-') as directory:
        temp=Path(directory); old=temp/'old'; old.mkdir(); sdk=temp/'sdk'
        (old/'CMakeLists.txt').write_bytes(reference(BOARD+'/CMakeLists.txt'))
        fixtures = {'src/hog.c':'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c',
                    'src/usb_hid.c':'tests/sdk_v0_3_0_usb_hid.c',
                    'src/usb.c':'tests/sdk_v0_3_0_usb.c',
                    'include/zmk/hid.h':'tests/hid_snapshot_mocks/zmk/hid.h'}
        for target, source in fixtures.items():
            path=sdk/'app'/target; path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes((ROOT/source).read_bytes())
        harness=temp/'contract.cmake';harness.write_text(HARNESS)
        count=0
        for gp,tap,usb,ble,micro in itertools.product([0,1],repeat=5):
            flags=dict(CONFIG_BBP9981_GAMEPAD=gp,CONFIG_A320_TAP_OUTPUT=tap,
                       CONFIG_ZMK_USB=usb,CONFIG_ZMK_BLE=ble,CONFIG_A320_MICROTAP=micro)
            before=run(harness,old,sdk,flags); after=run(harness,ROOT/BOARD,sdk,flags)
            assert before.returncode==after.returncode==0, (flags,before.stderr,after.stderr)
            assert events(before,old,sdk)==events(after,ROOT/BOARD,sdk), flags
            assert after.stdout.splitlines().count('-- EVENT source custom_driver/behavior_cursor_speed.c')==1
            assert '-- EVENT source custom_driver/trackpad_cursor_speed.c' not in after.stdout
            count+=1
        # Actual current CMake: adapter must link even with Kconfig OFF, whereas
        # only the settings/motion service is optional. Do not fake DTS config.
        for speed in [0,1]:
            flags=dict(CONFIG_BBP9981_GAMEPAD=1,CONFIG_A320_TAP_OUTPUT=1,
                       CONFIG_ZMK_USB=1,CONFIG_ZMK_BLE=1,CONFIG_A320_MICROTAP=1,
                       CONFIG_BBP9981_CURSOR_SPEED=speed)
            result=run(harness,ROOT/BOARD,sdk,flags)
            assert result.returncode==0,result.stderr
            lines=result.stdout.splitlines()
            assert lines.count('-- EVENT source custom_driver/behavior_cursor_speed.c')==1
            assert lines.count('-- EVENT source custom_driver/trackpad_cursor_speed.c')==speed
        flags=dict(CONFIG_BBP9981_GAMEPAD=1,CONFIG_A320_TAP_OUTPUT=1,
                   CONFIG_ZMK_USB=1,CONFIG_ZMK_BLE=1,CONFIG_A320_MICROTAP=1)
        failures=['missing_revision','zephyr_revision','zephyr_dirty','hal_revision','hal_dirty']
        for failure in failures:
            for board in [old,ROOT/BOARD]:
                result=run(harness,board,sdk,flags,failure)
                assert result.returncode!=0, (failure,board)
        for target in fixtures:
            path=sdk/'app'/target; good=path.read_bytes();path.write_bytes(good+b'\n/* changed */\n')
            for board in [old,ROOT/BOARD]:
                result=run(harness,board,sdk,flags)
                assert result.returncode!=0 and 'pinned ZMK source changed' in result.stderr, target
            path.write_bytes(good)
    print(f'PASS CMake contract: {count} GP/tap/USB/BLE/micro combinations identical source/preinclude traces; '
          '5 revision/dirty + 4 SHA negatives still fail closed; no real SDK writes')
    print('PASS CMake cursor adapter: actual OFF/ON always links one adapter; only ON links speed service')


if __name__=='__main__': main()
