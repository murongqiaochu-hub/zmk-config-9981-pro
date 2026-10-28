#!/usr/bin/env python3
"""Validate isolated composite source staging, never build/flash a device.

All CMake checks use the real interpreter with recording Zephyr/SDK stubs.
Source staging is not an ARM build, a live GATT service or OS acceptance.
"""
from pathlib import Path
from contextlib import contextmanager
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


def cmake_contract(stage, temp):
    if not shutil.which('cmake'):
        if os.environ.get('CI'):
            raise AssertionError('CI must execute composite CMake contracts')
        print('SKIP composite CMake interpreter: cmake unavailable; staging checks still run')
        return
    from test_refactor_cmake import HARNESS
    def run(harness, board, sdk, flags, failure=''):
        command = ['cmake', f'-DBOARD_ROOT={board}', f'-DSDK_ROOT={sdk}', f'-DFAILURE={failure}']
        command += [f'-D{k}={int(value)}' for k,value in flags.items()]
        return subprocess.run([*command,'-P',str(harness)], text=True, capture_output=True,
                              env=stager.git_environment())
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
        baseline = run(harness, ROOT/'config/boards/arm/bbp9981', sdk, current)
        assert baseline.returncode == 0, baseline.stderr
        def usb_events(output, root):
            return [line.replace(str(root),'BOARD') for line in output.splitlines()
                    if line.startswith('-- EVENT property') and '/src/usb' in line]
        assert usb_events(result.stdout, board) == usb_events(baseline.stdout, ROOT/'config/boards/arm/bbp9981')
    good = {**flags, **dict.fromkeys(required,1), 'CONFIG_ZMK_POINTING_SMOOTH_SCROLLING':0, 'CONFIG_BT_EATT':0}
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
        return
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
    harness.write_text('set(ZEPHYR_BASE "'+str(zephyr)+'")\n' + config + precondition)
    clean = subprocess.run(['cmake','-P',str(harness)],capture_output=True,text=True,env=cmake_env)
    assert clean.returncode == 0, clean.stderr
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
    print('PASS real CMake/git BLE stack gates on disposable synthetic repo: three clean files accepted, modified and assume-unchanged bytes rejected')


def incremental_lock_test(stage, temp):
    """Prove CMake reconfigures on changes to stage and SDK source bytes.

    Uses a tiny native C program and the disposable synthetic Git repo, not a
    Zephyr/ARM/firmware build. The source and mtime changes are temp-only.
    """
    if not shutil.which('cmake') or not shutil.which('make'):
        if os.environ.get('CI'):
            raise AssertionError('CI must execute incremental source-lock proof')
        print('SKIP real incremental lock: cmake/make unavailable')
        return
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
    (source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(host_lock C)\n'
        +'set(ZEPHYR_BASE "'+str(zephyr)+'")\n'+config+precondition+source_guard
        +'add_executable(host_lock main.c)\n')
    env = stager.git_environment()
    configure = ['cmake','-G','Unix Makefiles','-S',str(source),'-B',str(build)]
    make = ['cmake','--build',str(build)]
    subprocess.run(configure,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    subprocess.run(make,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    for relative,root,message in (
        (stager.CANDIDATE,stage,'Composite staged source changed'),
        ('subsys/bluetooth/host/att.c',zephyr,'exact pinned Zephyr BLE stack file'),
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
    print('PASS actual CMake/Make host incremental reconfigure: changed stage header and SDK ATT bytes force recheck and fail')


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
            real_ble_guard(temp)
        print('PASS hostile GIT_DIR/WORK_TREE/INDEX/CONFIG redirections ignored; foreign index and hook untouched')
        expected_commit = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,
                                                   env=stager.git_environment(),text=True).strip()
        assert manifest['source_commit'] == expected_commit
        assert not manifest['build_performed'] and not manifest['flashed'] and not manifest['device_tested']
        assert not manifest['ready_for_firmware_build'] and not manifest['device_test_ready'] and manifest['known_ccc_race']
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
        assert {m['path'] for m in manifest['modified_config']} == {stager.GAMEPAD}
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
            if name.startswith('config/') and name != stager.GAMEPAD:
                assert actual == (ROOT/name).read_bytes(), name
        original = (ROOT/stager.GAMEPAD).read_text()
        source_lock = stager.locked_source_guard({name:(ROOT/name).read_bytes() for name in stager.LOCKED_SOURCE})
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
        incremental_lock_test(target, temp)
        print('PASS isolated composite staging: explicit HID database acknowledgement, exact single-source CMake substitution, all keymaps/config and identity settings preserved; no overwrite, symlink or unsupported layout')
        print('LIMIT source stage and host CMake only: no firmware build, radio, device, Android, iPadOS or Moonlight proof')


if __name__ == '__main__':
    main()
