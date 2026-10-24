#!/usr/bin/env python3
"""Execute the real board CMake, independently preserving its v2.3.3 trace.

Recording Zephyr APIs only; no SDK checkout writes. Native file(SHA256) evaluates
real frozen fixtures. Git identity/dirty results are injected adversarially.
"""
from pathlib import Path
import itertools
import os
import shutil
import subprocess
import tempfile
from run_ble_single_host_tests import ROOT, MOCKS, BUILD, fixture_checks

BASE = '432d6c857382769963afd65de0f803a303a67e59'
BOARD = 'config/boards/arm/bbp9981'
HARNESS = r'''
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CURRENT_SOURCE_DIR "${BOARD_ROOT}")
set(ZEPHYR_BASE "${SDK_ROOT}/zephyr")
set(APPLICATION_SOURCE_DIR "${SDK_ROOT}/app")
set(CMAKE_SOURCE_DIR "${SDK_ROOT}/app")
set(CMAKE_NM "SDK/arm-nm")
set(CONFIG_BOARD_BBP9981 ON)
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
# The diagnostic's deferred matrix-target discovery needs a full Zephyr project;
# here record the exact DEFER request, do not pretend to execute that callback.
function(cmake_language)
  message(STATUS "EVENT cmake_language ${ARGV}")
endfunction()
function(execute_process)
  set(args ${ARGV})
  list(FIND args RESULT_VARIABLE ri)
  math(EXPR ri "${ri}+1")
  list(GET args ${ri} result_name)
  set(result 0)
  if("${ARGV}" MATCHES "hal/nordic")
    set(kind hal)
    set(revision "884c4d61746bc35fbd379c169fc87ddb56c6461d")
  elseif("${ARGV}" MATCHES "${SDK_ROOT}/app")
    set(kind zmk)
    set(revision "edf5c0814fd3ea202e43aad2d68fd32e882a518c")
  else()
    set(kind zephyr)
    set(revision "dacab4875df72109b96cc8977547a0dc04875bcd")
  endif()
  if("${ARGV}" MATCHES "rev-parse")
    list(FIND args OUTPUT_VARIABLE oi)
    math(EXPR oi "${oi}+1")
    list(GET args ${oi} out_name)
    if(FAILURE STREQUAL "${kind}_revision")
      set(revision "0000000000000000000000000000000000000000")
    elseif(FAILURE STREQUAL "${kind}_missing")
      set(revision "")
      set(result 1)
    endif()
    set(${out_name} "${revision}" PARENT_SCOPE)
  elseif(FAILURE STREQUAL "${kind}_dirty")
    set(result 1)
  endif()
  set(${result_name} ${result} PARENT_SCOPE)
endfunction()
include("${BOARD_ROOT}/CMakeLists.txt")
'''


def run(harness, board, sdk, flags, failure=''):
    command = ['cmake', f'-DBOARD_ROOT={board}', f'-DSDK_ROOT={sdk}', f'-DFAILURE={failure}']
    command += [f'-D{k}={int(v)}' for k, v in flags.items()]
    return subprocess.run([*command, '-P', str(harness)], cwd=ROOT, text=True, capture_output=True)


def events(result, board, sdk):
    return [line.replace(str(board), 'BOARD').replace(str(sdk), 'SDK')
            for line in result.stdout.splitlines() if line.startswith('-- EVENT ')]


def compare(harness, old, sdk, flags):
    before = run(harness, old, sdk, flags)
    after = run(harness, ROOT / BOARD, sdk, flags)
    assert before.returncode == after.returncode == 0, (flags, before.stderr, after.stderr)
    original = events(before, old, sdk)
    actual = events(after, ROOT / BOARD, sdk)
    enabled = bool(flags['CONFIG_BBP9981_BLE_SINGLE_HOST'] and flags['CONFIG_ZMK_BLE'] and
                   (not flags.get('CONFIG_ZMK_SPLIT') or flags.get('CONFIG_ZMK_SPLIT_ROLE_CENTRAL')))
    source = '-- EVENT source custom_driver/ble_single_host.c'
    prefix = ('-- EVENT property SOURCE;SDK/app/src/ble.c;DIRECTORY;SDK/app;APPEND;PROPERTY;'
              'COMPILE_OPTIONS;-include;BOARD/custom_driver/ble_single_host_prefix.h')
    elf_proof = ('-- EVENT property GLOBAL;APPEND;PROPERTY;extra_post_build_commands;'
                 'COMMAND;SDK/arm-nm;--defined-only;$<TARGET_FILE:zephyr_final>')
    assert actual.count(source) == int(enabled), (flags, actual)
    assert actual.count(prefix) == int(enabled), (flags, actual)
    assert actual.count(elf_proof) == int(enabled), (flags, actual)
    filtered = [line for line in actual if line not in {source, prefix, elf_proof}]
    assert filtered == original, ('Existing ordered source/prefix/diagnostic trace changed', flags, original, filtered)
    assert sum('ble_single_host_prefix.h' in line for line in actual) == int(enabled)


def main():
    fixture_checks()
    if shutil.which('cmake') is None:
        assert not os.environ.get('CI'), 'CI must execute the single-host CMake matrix'
        print('SKIP local single-host executed CMake matrix: cmake unavailable; CI requires it')
        return
    BUILD.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cmake-', dir=BUILD) as directory:
        temp = Path(directory)
        old = temp / 'old'
        old.mkdir()
        paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', BASE, BOARD], cwd=ROOT, text=True).splitlines()
        for relative in paths:
            if not (relative.endswith('.cmake') or relative == BOARD + '/CMakeLists.txt'):
                continue
            target = old / Path(relative).relative_to(BOARD)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(subprocess.check_output(['git', 'show', BASE + ':' + relative], cwd=ROOT))
        sdk = temp / 'sdk'
        fixtures = {
            'src/hog.c': ROOT / 'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c',
            'src/usb_hid.c': ROOT / 'tests/sdk_v0_3_0_usb_hid.c',
            'src/usb.c': ROOT / 'tests/sdk_v0_3_0_usb.c',
            'include/zmk/hid.h': ROOT / 'tests/hid_snapshot_mocks/zmk/hid.h',
            'src/ble.c': MOCKS / 'sdk_v0_3_0_ble.c',
            'include/zmk/ble.h': MOCKS / 'zmk/ble.h',
            'include/zmk/ble/profile.h': MOCKS / 'zmk/ble/profile.h',
            'include/zmk/events/ble_active_profile_changed.h': MOCKS / 'zmk/events/ble_active_profile_changed.h',
        }
        for relative, fixture in fixtures.items():
            target = sdk / 'app' / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(fixture.read_bytes())
        harness = temp / 'record.cmake'
        harness.write_text(HARNESS)
        count = 0
        for gp, tap, usb, ble, micro in itertools.product([0, 1], repeat=5):
            for diag, policy in itertools.product([0, 1], repeat=2):
                flags = dict(CONFIG_BBP9981_GAMEPAD=gp, CONFIG_A320_TAP_OUTPUT=tap,
                             CONFIG_ZMK_USB=usb, CONFIG_ZMK_BLE=ble, CONFIG_A320_MICROTAP=micro,
                             CONFIG_BBP9981_MENU_DIAG=diag, CONFIG_BBP9981_BLE_SINGLE_HOST=policy,
                             CONFIG_ZMK_SPLIT=0, CONFIG_ZMK_SPLIT_BLE=0, CONFIG_ZMK_SPLIT_ROLE_CENTRAL=0)
                compare(harness, old, sdk, flags)
                count += 1
        for central, diag, policy in itertools.product([0, 1], repeat=3):
            flags = dict(CONFIG_BBP9981_GAMEPAD=1, CONFIG_A320_TAP_OUTPUT=1,
                         CONFIG_ZMK_USB=1, CONFIG_ZMK_BLE=1, CONFIG_A320_MICROTAP=1,
                         CONFIG_BBP9981_MENU_DIAG=diag, CONFIG_BBP9981_BLE_SINGLE_HOST=policy,
                         CONFIG_ZMK_SPLIT=1, CONFIG_ZMK_SPLIT_BLE=1, CONFIG_ZMK_SPLIT_ROLE_CENTRAL=central)
            compare(harness, old, sdk, flags)
            count += 1
        flags = dict(CONFIG_BBP9981_GAMEPAD=0, CONFIG_A320_TAP_OUTPUT=0,
                     CONFIG_ZMK_USB=0, CONFIG_ZMK_BLE=1, CONFIG_A320_MICROTAP=0,
                     CONFIG_BBP9981_MENU_DIAG=0, CONFIG_BBP9981_BLE_SINGLE_HOST=1,
                     CONFIG_ZMK_SPLIT=0, CONFIG_ZMK_SPLIT_BLE=0, CONFIG_ZMK_SPLIT_ROLE_CENTRAL=0)
        for failure in ['zmk_missing', 'zmk_revision', 'zmk_dirty',
                        'zephyr_missing', 'zephyr_revision', 'zephyr_dirty']:
            result = run(harness, ROOT / BOARD, sdk, flags, failure)
            assert result.returncode != 0, ('Inactive SDK identity gate', failure)
        for relative in ['src/ble.c', 'include/zmk/ble.h', 'include/zmk/ble/profile.h',
                         'include/zmk/events/ble_active_profile_changed.h']:
            target = sdk / 'app' / relative
            good = target.read_bytes()
            # Same-length corruption proves the real file(SHA256) gate is live,
            # independent of the mocked git diff/identity responses.
            target.write_bytes(bytes([good[0] ^ 1]) + good[1:])
            result = run(harness, ROOT / BOARD, sdk, flags)
            assert result.returncode != 0 and 'Single-host BLE pinned source changed' in result.stderr, (relative, result.stderr)
            off = dict(flags, CONFIG_BBP9981_BLE_SINGLE_HOST=0)
            assert run(harness, ROOT / BOARD, sdk, off).returncode == 0, 'OFF must not apply new BLE source guard'
            target.write_bytes(good)
    print(f'PASS single-host executed CMake: {count} GP/tap/USB/BLE/micro/diag/policy/role combinations; '
          'only one source-local BLE auth prefix, all old ordered events exact; 6 git + 4 SHA negatives; no SDK checkout writes')


if __name__ == '__main__':
    main()
