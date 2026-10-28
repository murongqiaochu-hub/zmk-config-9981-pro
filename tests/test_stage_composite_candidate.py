#!/usr/bin/env python3
"""Validate isolated composite source staging, never build/flash a device.

All CMake checks use the real interpreter with recording Zephyr/SDK stubs.
Source staging is not an ARM build, a live GATT service or OS acceptance.
"""
from pathlib import Path
from contextlib import contextmanager, redirect_stdout
from io import StringIO
import hashlib
import importlib.util
import itertools
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('composite_stage', ROOT/'tools/stage_composite_candidate.py')
stager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stager)


def reject(action, exc, label):
    try:
        action()
    except exc:
        return
    raise AssertionError('negative unexpectedly accepted: ' + label)


def stage_candidate(target):
    return stager.stage(target, acknowledge_hid_database_change=True)


@contextmanager
def hostile_git_environment(temp):
    """Only disposable sentinels; never point a test at a user's index."""
    foreign = temp/'foreign-git'; foreign.mkdir()
    index = temp/'foreign-index'; sentinel = b'NOT A GIT INDEX: DO NOT TOUCH\n'
    index.write_bytes(sentinel)
    hooks = temp/'foreign-hooks'; hooks.mkdir()
    hook = hooks/'pre-commit'; marker = temp/'foreign-hook-ran'
    hook.write_text('#!/bin/sh\nprintf touched > "'+str(marker)+'"\n')
    hook.chmod(0o700)
    poison = dict(GIT_DIR=str(foreign), GIT_WORK_TREE=str(temp), GIT_INDEX_FILE=str(index),
                  GIT_COMMON_DIR=str(foreign), GIT_OBJECT_DIRECTORY=str(foreign),
                  GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='core.hooksPath', GIT_CONFIG_VALUE_0=str(hooks))
    prior = {key:os.environ.get(key) for key in poison}
    try:
        os.environ.update(poison)
        yield
    finally:
        for key,value in prior.items():
            if value is None: os.environ.pop(key,None)
            else: os.environ[key] = value
        assert index.read_bytes() == sentinel, 'foreign index was modified'
        assert not marker.exists(), 'foreign hook ran'


@contextmanager
def hostile_build_environment(temp):
    """Inject only disposable compiler/toolchain sentinels for isolation tests."""
    foreign = temp/'foreign-compiler'; marker = temp/'foreign-compiler-ran'
    foreign.write_text('#!/bin/sh\nprintf touched > "'+str(marker)+'"\nexit 1\n')
    foreign.chmod(0o700)
    toolchain = temp/'foreign-toolchain.cmake'; tool_marker = temp/'foreign-toolchain-ran'
    toolchain.write_text('file(WRITE "'+str(tool_marker)+'" "touched")\n')
    mapfile = temp/'foreign-link.map'
    poison = dict(CC=str(foreign), CXX=str(foreign), CFLAGS='-Wl,-Map,'+str(mapfile),
                  LDFLAGS='-Wl,-Map,'+str(mapfile), CMAKE_TOOLCHAIN_FILE=str(toolchain),
                  CMAKE_PROJECT_INCLUDE=str(toolchain), MAKEFLAGS='-j99')
    prior = {key:os.environ.get(key) for key in poison}
    try:
        os.environ.update(poison)
        yield
    finally:
        for key,value in prior.items():
            if value is None: os.environ.pop(key,None)
            else: os.environ[key] = value
        assert not marker.exists() and not tool_marker.exists() and not mapfile.exists(), 'foreign build setting ran'


def isolated_build_environment(temp):
    """Host-only synthetic build: never inherit toolchains or output flags."""
    paths = [shutil.which(name) for name in ('cmake','make','cc','git')]
    if any(path is None for path in paths):
        raise RuntimeError('required host test tool unavailable')
    home = temp/'build-home'; home.mkdir()
    scratch = temp/'build-tmp'; scratch.mkdir()
    env = {'PATH':os.pathsep.join(dict.fromkeys([str(Path(path).parent) for path in paths]+['/usr/bin','/bin'])),
           'HOME':str(home), 'TMPDIR':str(scratch), 'LANG':'C', 'LC_ALL':'C'}
    cmake_package = Path(paths[0]).parent.parent
    if (cmake_package/'cmake/__init__.py').is_file():
        env['PYTHONPATH'] = str(cmake_package)
    env.update({key:value for key,value in stager.git_environment().items() if key.startswith('GIT_')})
    return env


def cmake_contract(stage, temp):
    if not shutil.which('cmake'):
        if os.environ.get('CI'):
            raise AssertionError('CI must execute composite CMake contracts')
        print('SKIP composite CMake interpreter: cmake unavailable; staging checks still run')
        return
    from test_refactor_cmake import HARNESS
    def run(harness, board, sdk, flags, failure='', env=None):
        is_stage = Path(board).resolve() == (stage/'config/boards/arm/bbp9981').resolve()
        command = ['cmake', '-DBBP9981_COMPOSITE_HOST_CMAKE_TEST=1',
                   f'-DENFORCE_GIT_CLEAN={int(is_stage)}', f'-DBOARD_ROOT={board}',
                   f'-DSDK_ROOT={sdk}', f'-DFAILURE={failure}']
        command += [f'-D{k}={int(value)}' for k,value in flags.items()]
        return subprocess.run([*command,'-P',str(harness)], text=True, capture_output=True,
                              env=stager.git_environment() if env is None else env)
    sdk = temp/'sdk'
    fixtures = {'src/hog.c':'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c',
                'src/usb_hid.c':'tests/sdk_v0_3_0_usb_hid.c',
                'src/usb.c':'tests/sdk_v0_3_0_usb.c',
                'include/zmk/hid.h':'tests/hid_snapshot_mocks/zmk/hid.h'}
    for relative, source in fixtures.items():
        path = sdk/'app'/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((ROOT/source).read_bytes())
    harness = temp/'contract.cmake'
    recorder = '''function(zephyr_compile_definitions)
  message(STATUS "EVENT compile_definitions ${ARGV}")
endfunction()
'''
    ble_hash = '''  if("${ARGV}" MATCHES "hash-object")
    list(FIND args OUTPUT_VARIABLE out_index)
    math(EXPR out_index "${out_index}+1")
    list(GET args ${out_index} out_name)
    if("${ARGV}" MATCHES "subsys/bluetooth/host/att.c")
      set(which "att")
      set(blob "5af1301f054a0d3a04fd0f61f66bd1b94316983c")
    elseif("${ARGV}" MATCHES "subsys/bluetooth/host/gatt.c")
      set(which "gatt_c")
      set(blob "069082ec67b0959ef4371f595ba307878ecc0ef6")
    elseif("${ARGV}" MATCHES "include/zephyr/bluetooth/gatt.h")
      set(which "gatt_h")
      set(blob "7d4ac72495b08242a3e80b713b7581ec9b6f921b")
    else()
      set(which "unknown")
      set(blob "")
      set(result 1)
    endif()
    if(FAILURE STREQUAL "${which}_hash")
      set(blob "0000000000000000000000000000000000000000")
    elseif(FAILURE STREQUAL "${which}_missing")
      set(blob "")
      set(result 1)
    endif()
    set(${out_name} "${blob}" PARENT_SCOPE)
  elseif("${ARGV}" MATCHES "rev-parse")'''
    ble_dirty = '''  elseif("${ARGV}" MATCHES "subsys/bluetooth/host/att.c" AND FAILURE STREQUAL "att_dirty")
    set(result 1)
  elseif("${ARGV}" MATCHES "subsys/bluetooth/host/gatt.c" AND FAILURE STREQUAL "gatt_c_dirty")
    set(result 1)
  elseif("${ARGV}" MATCHES "include/zephyr/bluetooth/gatt.h" AND FAILURE STREQUAL "gatt_h_dirty")
    set(result 1)
  elseif(NOT "${ARGV}" MATCHES "hal/nordic" AND FAILURE STREQUAL "zephyr_dirty")'''
    rev_marker = '  if("${ARGV}" MATCHES "rev-parse")'
    dirty_marker = '  elseif(NOT "${ARGV}" MATCHES "hal/nordic" AND FAILURE STREQUAL "zephyr_dirty")'
    assert HARNESS.count(rev_marker) == 1 and HARNESS.count(dirty_marker) == 1
    text = HARNESS.replace(rev_marker,ble_hash,1).replace(dirty_marker,ble_dirty,1)
    early = '''function(execute_process)
  if(ENFORCE_GIT_CLEAN)
    foreach(key IN ITEMS GIT_DIR GIT_WORK_TREE GIT_COMMON_DIR GIT_INDEX_FILE GIT_OBJECT_DIRECTORY)
      if(DEFINED ENV{${key}})
        message(FATAL_ERROR "Git environment was not cleared before first SDK check: ${key}")
      endif()
    endforeach()
    if(NOT "$ENV{GIT_CONFIG_COUNT}" STREQUAL "2" OR
       NOT "$ENV{GIT_CONFIG_KEY_0}" STREQUAL "core.fsmonitor" OR
       NOT "$ENV{GIT_CONFIG_VALUE_0}" STREQUAL "false" OR
       NOT "$ENV{GIT_CONFIG_KEY_1}" STREQUAL "core.hooksPath" OR
       NOT "$ENV{GIT_CONFIG_VALUE_1}" STREQUAL "/dev/null")
      message(FATAL_ERROR "Git local fsmonitor/hook override missing before SDK check")
    endif()
  endif()
  set(args ${ARGV})'''
    marker = 'function(execute_process)\n  set(args ${ARGV})'
    assert text.count(marker) == 1
    text = text.replace(marker,early,1)
    harness.write_text(text.replace('include("${BOARD_ROOT}/CMakeLists.txt")',
                                    recorder+'include("${BOARD_ROOT}/CMakeLists.txt")'))
    board = stage/'config/boards/arm/bbp9981'
    required = ['CONFIG_BBP9981_GAMEPAD', 'CONFIG_ZMK_BLE', 'CONFIG_A320_TAP_OUTPUT',
                'CONFIG_ZMK_POINTING', 'CONFIG_ZMK_HID_INDICATORS', 'CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION',
                'CONFIG_ZMK_HID_REPORT_TYPE_HKRO', 'CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL']
    flags = dict(CONFIG_ZMK_USB=1, CONFIG_A320_MICROTAP=1, CONFIG_BBP9981_CURSOR_SPEED=1)
    for values in itertools.product((0,1), repeat=len(required)+1):
        current = {**flags, **dict(zip(required,values[:-1])),
                   'CONFIG_ZMK_POINTING_SMOOTH_SCROLLING':values[-1]}
        result = run(harness, board, sdk, current)
        good = all(values[:-1]) and not values[-1]
        if not good:
            assert result.returncode != 0 and 'Composite BLE candidate requires' in result.stderr, result.stderr
            continue
        assert result.returncode == 0, result.stderr
        lines = result.stdout.splitlines()
        hog = [x for x in lines if x.startswith('-- EVENT property') and '/src/hog.c;' in x]
        assert len(hog) == 2 and 'trackpad_hog_snapshot.h' in hog[0] and 'composite_ble_candidate.h' in hog[1], hog
        assert hog[1].count('-DBBP9981_COMPOSITE_EXPERIMENT=1') == 1
        assert [x for x in lines if 'BBP9981_COMPOSITE_EXPERIMENT=1' in x] == [hog[1]]
        assert not any('gamepad_ble_extension.h' in x or 'pure_hog_guard.h' in x for x in hog)
        dependencies = [x for x in lines if x.startswith('-- EVENT property DIRECTORY;') and 'CMAKE_CONFIGURE_DEPENDS' in x]
        for relative in ('src/hog.c','src/usb.c','src/usb_hid.c','include/zmk/hid.h'):
            assert any(str(sdk/'app'/relative) in x for x in dependencies), relative
        for relative in ('subsys/bluetooth/host/att.c','subsys/bluetooth/host/gatt.c',
                         'include/zephyr/bluetooth/gatt.h','drivers/usb/device/usb_dc_nrfx.c',
                         'subsys/usb/device/class/hid/core.c','subsys/usb/device/usb_device.c',
                         'subsys/usb/device/usb_descriptor.c','include/zephyr/usb/usb_device.h',
                         'include/zephyr/usb/class/usb_hid.h'):
            assert any(str(sdk/'zephyr'/relative) in x for x in dependencies), relative
        for relative in ('nrfx/drivers/src/nrfx_usbd.c','nrfx/drivers/include/nrfx_usbd.h','nrfx/hal/nrf_usbd.h'):
            assert any('/modules/hal/nordic/'+relative in x for x in dependencies), relative
        baseline = run(harness, ROOT/'config/boards/arm/bbp9981', sdk, current)
        assert baseline.returncode == 0, baseline.stderr
        def usb_events(output, root):
            # Compare actual SOURCE compile options only, not new DIRECTORY
            # configure-dependency tracking for SDK USB source files.
            return [line.replace(str(root),'BOARD') for line in output.splitlines()
                    if line.startswith('-- EVENT property SOURCE;') and '/src/usb' in line]
        expected_usb = usb_events(baseline.stdout, ROOT/'config/boards/arm/bbp9981')
        actual_usb = usb_events(result.stdout, board)
        assert len(expected_usb) == 3 and actual_usb == expected_usb, (actual_usb,expected_usb)
    good = {**flags, **dict.fromkeys(required,1), 'CONFIG_ZMK_POINTING_SMOOTH_SCROLLING':0, 'CONFIG_BT_EATT':0}
    poison_dir = temp/'poison-cmake'; poison_dir.mkdir()
    with hostile_git_environment(poison_dir):
        poisoned = run(harness, board, sdk, good, env=os.environ.copy())
        assert poisoned.returncode == 0, poisoned.stderr
    eatt = run(harness, board, sdk, {**good,'CONFIG_BT_EATT':1})
    assert eatt.returncode != 0 and 'Composite BLE candidate requires' in eatt.stderr
    for usb in (0,1):
        current = {**good, 'CONFIG_ZMK_USB':usb}
        result = run(harness, board, sdk, current)
        assert result.returncode == 0, result.stderr
    for failure in ('missing_revision','zephyr_revision','zephyr_dirty','hal_revision','hal_dirty'):
        assert run(harness, board, sdk, good, failure).returncode != 0, failure
    for which in ('att','gatt_c','gatt_h'):
        for suffix, message in (('_hash','exact pinned Zephyr BLE stack file'),
                                ('_missing','exact pinned Zephyr BLE stack file'),
                                ('_dirty','clean pinned Zephyr BLE stack file')):
            failure = which + suffix
            result = run(harness, board, sdk, good, failure)
            assert result.returncode != 0 and message in result.stderr, (failure,result.stderr)
    for relative in fixtures:
        path = sdk/'app'/relative
        original = path.read_bytes()
        path.write_bytes(original+b'\n/* deliberate negative */\n')
        result = run(harness, board, sdk, good)
        assert result.returncode != 0 and 'pinned ZMK source changed' in result.stderr, relative
        path.write_bytes(original)
    for relative in stager.LOCKED_SOURCE:
        path = stage/relative
        original = path.read_bytes()
        # Whitespace is valid in both C and CMake, so a parser failure cannot
        # accidentally stand in for the intended locked-source hash rejection.
        path.write_bytes(original+b'\n')
        result = run(harness, board, sdk, good)
        assert result.returncode != 0 and 'Composite staged source changed' in result.stderr, (relative,result.stderr)
        path.write_bytes(original)
    print('PASS composite CMake: 512 configurations fail closed except exact target including enforced subscription, snapshot-before-candidate, USB unchanged, 5 SDK identity + 9 BLE stack + 4 SDK fixture + 13 stage-source negatives; Zephyr APIs are recording stubs')


def real_ble_guard(temp):
    """Exercise three CMake BLE-file guards with real git on synthetic bytes.

    Synthetic blobs are substituted only in a temporary CMake script; this
    proves guard mechanics, not the external fixed SDK's actual identity.
    """
    if not shutil.which('cmake'):
        if os.environ.get('CI'):
            raise AssertionError('CI must execute real BLE-file guards')
        print('SKIP real BLE-file guards: cmake unavailable')
        return False
    zephyr = temp/'synthetic-zephyr'; zephyr.mkdir()
    fixtures = {
        'subsys/bluetooth/host/att.c':'5af1301f054a0d3a04fd0f61f66bd1b94316983c',
        'subsys/bluetooth/host/gatt.c':'069082ec67b0959ef4371f595ba307878ecc0ef6',
        'include/zephyr/bluetooth/gatt.h':'7d4ac72495b08242a3e80b713b7581ec9b6f921b',
    }
    originals = {}
    for index, relative in enumerate(fixtures):
        path = zephyr/relative; path.parent.mkdir(parents=True,exist_ok=True)
        originals[relative] = f'/* synthetic fixture {index}, not Zephyr source */\n'.encode()
        path.write_bytes(originals[relative])
    clean_env = stager.git_environment()
    cmake_env = os.environ.copy()  # Intentionally poisoned by caller; PRECONDITION must scrub it.
    git = ['git','-c','core.hooksPath='+os.devnull,'-c','core.fsmonitor=false']
    subprocess.run([*git,'init','-q',str(zephyr)],check=True,env=clean_env)
    def actual_git(*args):
        return subprocess.check_output([*git,'-C',str(zephyr),*args],env=clean_env,text=True).strip()
    assert Path(actual_git('rev-parse','--show-toplevel')).resolve() == zephyr.resolve()
    assert Path(actual_git('rev-parse','--absolute-git-dir')).resolve() == (zephyr/'.git').resolve()
    assert Path(actual_git('rev-parse','--path-format=absolute','--git-path','index')).resolve() == (zephyr/'.git/index').resolve()
    assert not (zephyr/'.git').is_symlink() and not (zephyr/'.git/index').is_symlink()
    subprocess.run([*git,'-C',str(zephyr),'add','--',*fixtures],check=True,env=clean_env)
    subprocess.run([*git,'-C',str(zephyr),'-c','user.name=Host Test','-c','user.email=host-test@localhost',
                    '-c','commit.gpgsign=false','commit','-q','-m','synthetic fixtures'],check=True,env=clean_env)
    # Prove that a repository-local fsmonitor *would* run without our override.
    control_hook = temp/'fsmonitor-control'; control_marker = temp/'fsmonitor-control-ran'
    control_hook.write_text('#!/bin/sh\nprintf touched > "'+str(control_marker)+'"\nprintf "token\\0"\n')
    control_hook.chmod(0o700)
    subprocess.run([*git,'-C',str(zephyr),'config','--local','core.fsmonitor',str(control_hook)],check=True,env=clean_env)
    subprocess.run(['git','-c','core.hooksPath='+os.devnull,'-C',str(zephyr),'status','--porcelain'],
                   check=True,env=clean_env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert control_marker.exists(), 'fsmonitor control did not run in synthetic Git repo'
    guard_hook = temp/'fsmonitor-guard'; guard_marker = temp/'fsmonitor-guard-ran'
    guard_hook.write_text('#!/bin/sh\nprintf touched > "'+str(guard_marker)+'"\nprintf "token\\0"\n')
    guard_hook.chmod(0o700)
    subprocess.run([*git,'-C',str(zephyr),'config','--local','core.fsmonitor',str(guard_hook)],check=True,env=clean_env)
    precondition = stager.PRECONDITION
    for relative, pinned in fixtures.items():
        blob = actual_git('hash-object','--no-filters',relative)
        assert precondition.count(pinned) == 1 and len(blob) == 40
        precondition = precondition.replace(pinned,blob,1)
    config = '''set(CONFIG_BBP9981_GAMEPAD ON)
set(CONFIG_ZMK_BLE ON)
set(CONFIG_A320_TAP_OUTPUT ON)
set(CONFIG_ZMK_POINTING ON)
set(CONFIG_ZMK_HID_INDICATORS ON)
set(CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION ON)
set(CONFIG_BT_EATT OFF)
set(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING OFF)
set(CONFIG_ZMK_HID_REPORT_TYPE_HKRO ON)
set(CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL ON)
function(zephyr_compile_definitions)
endfunction()
'''
    harness = temp/'real-ble.cmake'
    post_check = '''execute_process(COMMAND git -C "${ZEPHYR_BASE}" status --porcelain
    RESULT_VARIABLE _status OUTPUT_QUIET ERROR_QUIET)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "synthetic Git status failed")
endif()
'''
    harness.write_text('set(ZEPHYR_BASE "'+str(zephyr)+'")\n' + config + precondition + post_check)
    clean = subprocess.run(['cmake','-P',str(harness)],capture_output=True,text=True,env=cmake_env)
    assert clean.returncode == 0, clean.stderr
    assert not guard_marker.exists(), 'local fsmonitor ran despite configure-only override'
    for relative, original in originals.items():
        path = zephyr/relative
        assert path.read_bytes() == original
        path.write_bytes(original+b'/* modified */\n')
        changed = subprocess.run(['cmake','-P',str(harness)],capture_output=True,text=True,env=cmake_env)
        assert changed.returncode != 0 and 'exact pinned Zephyr BLE stack file' in changed.stderr, relative
        subprocess.run([*git,'-C',str(zephyr),'update-index','--assume-unchanged','--',relative],check=True,env=clean_env)
        hidden = subprocess.run([*git,'-C',str(zephyr),'diff','--quiet','HEAD','--',relative],env=clean_env)
        assert hidden.returncode == 0, 'synthetic assume-unchanged control did not hide git diff'
        guarded = subprocess.run(['cmake','-P',str(harness)],capture_output=True,text=True,env=cmake_env)
        assert guarded.returncode != 0 and 'exact pinned Zephyr BLE stack file' in guarded.stderr, relative
        subprocess.run([*git,'-C',str(zephyr),'update-index','--no-assume-unchanged','--',relative],check=True,env=clean_env)
        assert path.read_bytes() == original+b'/* modified */\n'
        path.write_bytes(original)
        assert subprocess.run(['cmake','-P',str(harness)],capture_output=True,env=cmake_env).returncode == 0
    assert not guard_marker.exists(), 'local fsmonitor ran during guarded checks'
    print('PASS real CMake/git BLE gates: three files verified, modified/assume-unchanged rejected, repository-local fsmonitor suppressed')
    return True


def firmware_configure_is_forbidden(stage, temp):
    """The unsafe stage permits only explicit cmake -P host recorder mode."""
    if not shutil.which('cmake') or not shutil.which('make'):
        if os.environ.get('CI'):
            raise AssertionError('CI must execute the host-only configure gate')
        print('SKIP host-only configure gate: cmake/make unavailable')
        return False
    root = temp/'forbidden-config'; root.mkdir()
    source = root/'source'; source.mkdir()
    board = stage/stager.BOARD_CMAKE
    (source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(host_guard NONE)\n'
        +'include("'+str(board)+'")\n')
    env = isolated_build_environment(root)
    for index,extra in enumerate(([], ['-DCMAKE_SCRIPT_MODE_FILE=pretend.cmake'])):
        build = root/('build-'+str(index))
        result = subprocess.run(['cmake','-S',str(source),'-B',str(build),
                                 '-DBBP9981_COMPOSITE_HOST_CMAKE_TEST=1',*extra],
                                capture_output=True,text=True,env=env)
        assert result.returncode != 0 and 'not ready for firmware configuration or build' in result.stderr, result.stderr
    script = root/'no-optin.cmake'; script.write_text('include("'+str(board)+'")\n')
    result = subprocess.run(['cmake','-P',str(script)],capture_output=True,text=True,env=env)
    assert result.returncode != 0 and 'not ready for firmware configuration or build' in result.stderr, result.stderr
    print('PASS actual non-script CMake configure rejected even with host-test flag; script mode also requires explicit flag')
    return True


def incremental_lock_test(stage, temp):
    """Prove CMake reconfigures on changes to stage and SDK source bytes.

    Uses a tiny native C program and the disposable synthetic Git repo, not a
    Zephyr/ARM/firmware build. The source and mtime changes are temp-only.
    """
    if not shutil.which('cmake') or not shutil.which('make'):
        if os.environ.get('CI'):
            raise AssertionError('CI must execute incremental source-lock proof')
        print('SKIP real incremental lock: cmake/make unavailable')
        return False
    zephyr = temp/'synthetic-zephyr'
    git = ['git','-c','core.hooksPath='+os.devnull,'-c','core.fsmonitor=false','-C',str(zephyr)]
    replacements = {
        'subsys/bluetooth/host/att.c':'5af1301f054a0d3a04fd0f61f66bd1b94316983c',
        'subsys/bluetooth/host/gatt.c':'069082ec67b0959ef4371f595ba307878ecc0ef6',
        'include/zephyr/bluetooth/gatt.h':'7d4ac72495b08242a3e80b713b7581ec9b6f921b',
    }
    precondition = stager.PRECONDITION
    for relative, pinned in replacements.items():
        blob = subprocess.check_output([*git,'hash-object','--no-filters',relative],
                                       env=stager.git_environment(),text=True).strip()
        assert precondition.count(pinned) == 1
        precondition = precondition.replace(pinned,blob,1)
    source_guard = stager.locked_source_guard({name:(stage/name).read_bytes() for name in stager.LOCKED_SOURCE})
    source_guard = source_guard.replace('${CMAKE_CURRENT_SOURCE_DIR}/../../../../',str(stage)+'/')
    source = temp/'incremental-source'; source.mkdir()
    build = temp/'incremental-build'
    app = temp/'synthetic-app'; app.mkdir()
    app_files = ('src/hog.c','src/usb.c','src/usb_hid.c','include/zmk/hid.h')
    for index,relative in enumerate(app_files):
        path=app/relative; path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(f'/* synthetic ZMK fixture {index} */\n'.encode())
    hog_sha=hashlib.sha256((app/'src/hog.c').read_bytes()).hexdigest()
    (source/'main.c').write_text('int main(void){return 0;}\n')
    config = '''set(CONFIG_BBP9981_GAMEPAD ON)
set(CONFIG_ZMK_BLE ON)
set(CONFIG_A320_TAP_OUTPUT ON)
set(CONFIG_ZMK_POINTING ON)
set(CONFIG_ZMK_HID_INDICATORS ON)
set(CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION ON)
set(CONFIG_BT_EATT OFF)
set(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING OFF)
set(CONFIG_ZMK_HID_REPORT_TYPE_HKRO ON)
set(CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL ON)
'''
    hog_guard = ('file(SHA256 "${APPLICATION_SOURCE_DIR}/src/hog.c" _synthetic_hog)\n'
                 +'if(NOT _synthetic_hog STREQUAL "'+hog_sha+'")\n'
                 +'  message(FATAL_ERROR "Synthetic pinned HOG changed")\nendif()\n')
    (source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(host_lock C)\n'
        +'set(ZEPHYR_BASE "'+str(zephyr)+'")\nset(APPLICATION_SOURCE_DIR "'+str(app)+'")\n'
        +config+precondition+source_guard+hog_guard+'add_executable(host_lock main.c)\n')
    env = isolated_build_environment(temp)
    configure = ['cmake','-G','Unix Makefiles','-S',str(source),'-B',str(build)]
    make = ['cmake','--build',str(build)]
    subprocess.run(configure,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    subprocess.run(make,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    for relative,root,message in (
        (stager.CANDIDATE,stage,'Composite staged source changed'),
        ('subsys/bluetooth/host/att.c',zephyr,'exact pinned Zephyr BLE stack file'),
        ('src/hog.c',app,'Synthetic pinned HOG changed'),
    ):
        path=root/relative; original=path.read_bytes(); stat=path.stat()
        try:
            path.write_bytes(original+b'\n')
            future=max(time.time_ns(),stat.st_mtime_ns)+2_000_000_000
            os.utime(path,ns=(stat.st_atime_ns,future))
            result=subprocess.run(make,env=env,text=True,capture_output=True)
            assert result.returncode!=0 and message in result.stdout+result.stderr, (relative,result.stdout,result.stderr)
        finally:
            path.write_bytes(original)
            os.utime(path,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        subprocess.run(configure,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        subprocess.run(make,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    print('PASS actual CMake/Make host incremental reconfigure: changed stage header, SDK ATT and synthetic ZMK HOG force recheck and fail')
    return True


def skip_messages_contract(temp):
    """A missing host tool must never be reported as a completed build check."""
    which = shutil.which
    ci = os.environ.pop('CI', None)
    try:
        shutil.which = lambda name: None if name in ('cmake','make') else which(name)
        output = StringIO()
        with redirect_stdout(output):
            assert real_ble_guard(temp) is False
            assert firmware_configure_is_forbidden(temp, temp) is False
            assert incremental_lock_test(temp, temp) is False
        assert output.getvalue().count('SKIP') == 3 and 'PASS' not in output.getvalue()
    finally:
        shutil.which = which
        if ci is not None: os.environ['CI'] = ci
    print('PASS unavailable host tools report SKIP rather than a false build-check PASS')


def main():
    if not __debug__:
        raise RuntimeError('the oracle must not run under Python -O')
    with tempfile.TemporaryDirectory(prefix='9981-composite-stage-') as directory:
        temp = Path(directory)
        assert temp.resolve().parent == Path(tempfile.gettempdir()).resolve()
        assert temp.name.startswith('9981-composite-stage-') and not temp.is_symlink()
        target = temp/'candidate'
        no_ack = subprocess.run([sys.executable,'-B',str(ROOT/'tools/stage_composite_candidate.py'),
                                 '--out',str(target)], capture_output=True, text=True)
        assert no_ack.returncode != 0 and '--acknowledge-hid-database-change' in no_ack.stderr and not target.exists()
        reject(lambda: stager.stage(target), ValueError, 'direct API without acknowledgement')
        reject(lambda: stager.stage(target, acknowledge_hid_database_change='yes'), ValueError, 'non-boolean acknowledgement')
        assert not target.exists()
        with hostile_git_environment(temp):
            manifest = stage_candidate(target)
            git_checks_ran = real_ble_guard(temp)
        print('PASS hostile GIT_DIR/WORK_TREE/INDEX/CONFIG redirections ignored by source stage; foreign sentinel untouched')
        if git_checks_ran:
            print('PASS disposable synthetic Git writer and CMake guard also ignored hostile Git environment')
        expected_commit = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,
                                                   env=stager.git_environment(),text=True).strip()
        assert manifest['source_commit'] == expected_commit
        assert not manifest['build_performed'] and not manifest['flashed'] and not manifest['device_tested']
        assert not manifest['ready_for_firmware_build'] and not manifest['device_test_ready'] and manifest['known_ccc_race']
        assert manifest['firmware_configure_forbidden'] and manifest['host_cmake_script_test_only']
        assert manifest['fresh_configure_required'] and manifest['incremental_guard_mtime_sensitive']
        readme = (target/'README_CANDIDATE.md').read_text()
        assert 'KNOWN SAFETY GAP' in readme and 'preserved mtimes or changed Git metadata' in readme
        assert manifest['hid_database_changed'] and not manifest['pnp_vid_pid_changed']
        assert not manifest['pnp_source_changed'] and not manifest['bluetooth_address_configuration_changed']
        assert manifest['keyboard_mouse_source_retained'] and not manifest['usb_config_changed']
        assert not manifest['keymap_changed'] and not manifest['settings_changed']
        assert manifest['expected_map_bytes'] == 380 and manifest['gamepad_input_id'] == 4
        assert manifest['gamepad_input_body_bytes'] == 10 and manifest['gamepad_output_id'] == 5
        assert manifest['gamepad_output_body_bytes'] == 4
        assert {m['path'] for m in manifest['modified_config']} == {stager.GAMEPAD, stager.BOARD_CMAKE}
        assert json.loads((target/'CANDIDATE.json').read_text()) == manifest
        tracked = subprocess.check_output(['git','ls-files','-z','--','config',
                                           'experiments/gamepad_protocol'], cwd=ROOT,
                                          env=stager.git_environment()).decode().split('\0')
        tracked_names = {name for name in tracked if name}
        assert tracked_names <= manifest['files'].keys()
        assert manifest['untracked_source_files'] == sorted(manifest['files'].keys() - tracked_names)
        assert not manifest['untracked_source_files'] or manifest['source_dirty']
        for name, details in manifest['files'].items():
            actual = (target/name).read_bytes()
            assert hashlib.sha256(actual).hexdigest() == details['sha256']
            if name.startswith('config/') and name not in (stager.GAMEPAD,stager.BOARD_CMAKE):
                assert actual == (ROOT/name).read_bytes(), name
        assert (target/stager.BOARD_CMAKE).read_bytes() == (
            stager.HOST_ONLY_BOARD_GUARD + (ROOT/stager.BOARD_CMAKE).read_text()).encode()
        original = (ROOT/stager.GAMEPAD).read_text()
        source_lock = stager.locked_source_guard({name:(target/name).read_bytes() for name in stager.LOCKED_SOURCE})
        assert (target/stager.GAMEPAD).read_bytes() == (
            stager.PRECONDITION + source_lock + original.replace(stager.OLD_PREFIX,stager.NEW_PREFIX,1)).encode()
        assert (target/stager.TAP).read_bytes() == (ROOT/stager.TAP).read_bytes()
        assert (target/'config/bbp9981.conf').read_bytes() == (ROOT/'config/bbp9981.conf').read_bytes()
        assert (target/'config/bbp9981.keymap').read_bytes() == (ROOT/'config/bbp9981.keymap').read_bytes()
        stock = bytes.fromhex((ROOT/stager.STOCK).read_text())
        pure = stager.descriptor_bytes((ROOT/stager.PURE).read_bytes())
        assert manifest['expected_map_sha256'] == hashlib.sha256(stock + pure[:7] + b'\x04\x15\x00' + pure[8:]).hexdigest()
        saved = (target/'CANDIDATE.json').read_bytes()
        reject(lambda: stage_candidate(target), FileExistsError, 'existing stage')
        assert (target/'CANDIDATE.json').read_bytes() == saved
        inside = ROOT/('forbidden-composite-' + temp.name)
        assert not inside.exists()
        reject(lambda: stage_candidate(inside), ValueError, 'inside source tree')
        assert not inside.exists()
        link = temp/'target-link'; link.symlink_to(target, target_is_directory=True)
        reject(lambda: stage_candidate(link), ValueError, 'symlink target')
        assert (target/'CANDIDATE.json').read_bytes() == saved
        badprefix = temp/'badprefix'
        oldprefix = stager.OLD_PREFIX
        try:
            stager.OLD_PREFIX = 'not-the-pinned-prefix'
            reject(lambda: stage_candidate(badprefix), ValueError, 'unexpected CMake prefix')
            assert not badprefix.exists()
        finally:
            stager.OLD_PREFIX = oldprefix
        descriptor = (ROOT/stager.PURE).read_bytes()
        reject(lambda: stager.descriptor_bytes(descriptor.replace(b'0x05, 0x01',b'0x05, 0x02',1)),
               ValueError, 'modified pure descriptor')
        for key in ('PURE_HEADER_SHA','CODEC_SHA'):
            previous = getattr(stager,key)
            invalid_stage = temp/('bad-'+key)
            try:
                setattr(stager,key,'0'*64)
                reject(lambda: stage_candidate(invalid_stage), ValueError, 'changed '+key)
                assert not invalid_stage.exists()
            finally:
                setattr(stager,key,previous)
        prior_check = stager.subprocess.check_output
        def wrong_git_root(command, **kwargs):
            if command and command[0] == 'git' and '--show-toplevel' in command:
                return str(temp) + '\n'
            return prior_check(command, **kwargs)
        try:
            stager.subprocess.check_output = wrong_git_root
            wrong_root = temp/'wrong-git-root'
            reject(lambda: stage_candidate(wrong_root), ValueError, 'wrong Git root')
            assert not wrong_root.exists()
        finally:
            stager.subprocess.check_output = prior_check
        def add_missing_tracked(command, **kwargs):
            result = prior_check(command, **kwargs)
            if command and command[0] == 'git' and 'ls-files' in command:
                return result + b'config/__deliberately_missing__\0'
            return result
        try:
            stager.subprocess.check_output = add_missing_tracked
            incomplete = temp/'missing-tracked'
            reject(lambda: stage_candidate(incomplete), ValueError, 'missing tracked source')
            assert not incomplete.exists()
        finally:
            stager.subprocess.check_output = prior_check
        def simulate_ignored_input(command, **kwargs):
            result = prior_check(command, **kwargs)
            if command and command[0] == 'git' and 'ls-files' in command:
                return b'\0'.join(x for x in result.split(b'\0') if x != b'config/bbp9981.keymap')
            if command and command[0] == 'git' and 'status' in command:
                return b''  # Git status alone may suppress ignored input.
            return result
        try:
            stager.subprocess.check_output = simulate_ignored_input
            simulated = stage_candidate(temp/'simulated-ignored')
            assert simulated['source_dirty'] and 'config/bbp9981.keymap' in simulated['untracked_source_files']
        finally:
            stager.subprocess.check_output = prior_check

        # Check root/ancestor symlinks too, before any Git call or stage output.
        for scenario in ('config','experiments','tests'):
            fake_root = temp/('fake-root-'+scenario); fake_root.mkdir()
            outside = temp/('outside-'+scenario); outside.mkdir()
            for name in ('config','experiments','tests'):
                if name == scenario:
                    (fake_root/name).symlink_to(outside,target_is_directory=True)
                else:
                    (fake_root/name).mkdir()
            if scenario != 'experiments': (fake_root/'experiments/gamepad_protocol').mkdir()
            if scenario != 'tests': (fake_root/'tests/hid_map_fixtures').mkdir()
            if scenario != 'tests': (fake_root/stager.STOCK).write_bytes((ROOT/stager.STOCK).read_bytes())
            prior_root = stager.ROOT
            try:
                stager.ROOT = fake_root.resolve()
                invalid = outside/('candidate-'+scenario)
                reject(lambda: stage_candidate(invalid), ValueError, 'source '+scenario+' symlink')
                assert not invalid.exists()
            finally:
                stager.ROOT = prior_root
        fake_index_root = temp/'fake-index-source'; fake_index_root.mkdir()
        (fake_index_root/'config').mkdir()
        (fake_index_root/'experiments/gamepad_protocol').mkdir(parents=True)
        (fake_index_root/'tests/hid_map_fixtures').mkdir(parents=True)
        (fake_index_root/stager.STOCK).write_bytes((ROOT/stager.STOCK).read_bytes())
        subprocess.run(['git','-c','core.hooksPath='+os.devnull,'init','-q',str(fake_index_root)],
                       check=True,env=stager.git_environment())
        index_link = fake_index_root/'.git/index'
        assert not index_link.exists() and not index_link.is_symlink()
        foreign_index = temp/'external-index-sentinel'; foreign_bytes = b'keep external index intact\n'
        foreign_index.write_bytes(foreign_bytes)
        index_link.symlink_to(foreign_index)
        prior_root = stager.ROOT
        try:
            stager.ROOT = fake_index_root.resolve()
            index_stage = temp/'fake-index-stage'
            reject(lambda: stage_candidate(index_stage), ValueError, 'symlinked Git index')
            assert not index_stage.exists() and foreign_index.read_bytes() == foreign_bytes
        finally:
            stager.ROOT = prior_root
        parent_link = temp/'source-parent-link'
        parent_link.symlink_to(ROOT/'config',target_is_directory=True)
        forbidden = ROOT/'config'/('forbidden-'+temp.name)
        assert not forbidden.exists()
        reject(lambda: stage_candidate(parent_link/forbidden.name), ValueError, 'output through source symlink')
        assert not forbidden.exists()

        # A symlink in a disposable source copy must be rejected before writing a stage.
        fake = temp/'fake-source'
        # Recreate files with disposable-directory defaults: copytree preserves
        # the source's mode-000 directory metadata, which would make this test's
        # own symlink fixture unwritable. Never change source permissions.
        for directory_name in ('config','experiments/gamepad_protocol'):
            for source in sorted((ROOT/directory_name).rglob('*')):
                assert not source.is_symlink()
                if source.is_file():
                    dest = fake/source.relative_to(ROOT)
                    dest.parent.mkdir(parents=True,exist_ok=True)
                    dest.write_bytes(source.read_bytes())
        (fake/'tests/hid_map_fixtures').mkdir(parents=True)
        (fake/stager.STOCK).write_bytes((ROOT/stager.STOCK).read_bytes())
        (fake/'experiments/gamepad_protocol/forbidden-link').symlink_to(fake/'config', target_is_directory=True)
        prior_root = stager.ROOT
        try:
            stager.ROOT = fake
            badlink = temp/'badlink-stage'
            reject(lambda: stage_candidate(badlink), ValueError, 'source symlink')
            assert not badlink.exists()
        finally:
            stager.ROOT = prior_root
        cmake_contract(target, temp)
        skip_messages_contract(temp)
        with hostile_build_environment(temp):
            configure_ran = firmware_configure_is_forbidden(target, temp)
            incremental_ran = incremental_lock_test(target, temp)
        if configure_ran and incremental_ran:
            print('PASS hostile compiler/toolchain/output flags ignored; foreign build markers untouched')
        else:
            print('SKIP hostile build isolation claim: one or more host build checks did not run')
        print('PASS isolated composite staging: explicit HID database acknowledgement, exact single-source CMake substitution, all keymaps/config and identity settings preserved; no overwrite, symlink or unsupported layout')
        print('LIMIT source stage and host CMake only: no firmware build, radio, device, Android, iPadOS or Moonlight proof')


if __name__ == '__main__':
    main()
