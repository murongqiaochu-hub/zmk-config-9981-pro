#!/usr/bin/env python3
"""Stage a NEW isolated BLE composite source candidate; never build or flash.

The normal config stays untouched. This changes the HID database and may affect
host caches; it does not change VID/PID, PnP source, Bluetooth identity, keys,
USB configuration or settings. Static staging is not evidence of compatibility.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BOARD = 'config/boards/arm/bbp9981'
GAMEPAD = BOARD + '/cmake/gamepad.cmake'
BOARD_CMAKE = BOARD + '/CMakeLists.txt'
TAP = BOARD + '/cmake/tap_extensions.cmake'
CANDIDATE = 'experiments/gamepad_protocol/composite_ble_candidate.h'
PURE = 'experiments/gamepad_protocol/stadia_descriptor.h'
STOCK = 'tests/hid_map_fixtures/stock_196.hex'
OLD_PREFIX = '"-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/gamepad_ble_extension.h")'
NEW_PREFIX = '"-DBBP9981_COMPOSITE_EXPERIMENT=1" "-include" "${CMAKE_CURRENT_SOURCE_DIR}/../../../../experiments/gamepad_protocol/composite_ble_candidate.h")'
SNAPSHOT_PREFIX = '"-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/trackpad_hog_snapshot.h")'
GIT_PRELUDE = '''# Sanitize Git redirections for THIS configure process and its children only.
# Do not modify the caller's environment, Git files or global configuration.
foreach(_composite_env IN ITEMS GIT_DIR GIT_WORK_TREE GIT_COMMON_DIR GIT_INDEX_FILE
        GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE
        GIT_CONFIG_COUNT GIT_CONFIG_PARAMETERS GIT_CONFIG_SYSTEM GIT_CONFIG_GLOBAL
        GIT_REPLACE_REF_BASE GIT_GRAFT_FILE GIT_SHALLOW_FILE GIT_QUARANTINE_PATH
        GIT_CEILING_DIRECTORIES GIT_DISCOVERY_ACROSS_FILESYSTEM GIT_EXEC_PATH
        GIT_TEMPLATE_DIR GIT_EXTERNAL_DIFF GIT_DIFF_OPTS GIT_ATTR_SOURCE)
    unset(ENV{${_composite_env}})
endforeach()
set(ENV{GIT_CONFIG_NOSYSTEM} "1")
set(ENV{GIT_CONFIG_GLOBAL} "/dev/null")
set(ENV{GIT_TERMINAL_PROMPT} "0")
set(ENV{GIT_OPTIONAL_LOCKS} "0")
set(ENV{GIT_NO_REPLACE_OBJECTS} "1")
# Environment config entries override any repository-local fsmonitor/hook.
set(ENV{GIT_CONFIG_COUNT} "2")
set(ENV{GIT_CONFIG_KEY_0} "core.fsmonitor")
set(ENV{GIT_CONFIG_VALUE_0} "false")
set(ENV{GIT_CONFIG_KEY_1} "core.hooksPath")
set(ENV{GIT_CONFIG_VALUE_1} "/dev/null")
'''
HOST_ONLY_BOARD_GUARD = '''# Safety gate: this source stage is NOT ready for firmware configuration.
# The flag below is for CMake -P recording tests only, not an ARM build.
if(CMAKE_GENERATOR OR NOT CMAKE_SCRIPT_MODE_FILE OR NOT BBP9981_COMPOSITE_HOST_CMAKE_TEST STREQUAL "1")
    message(FATAL_ERROR "Composite candidate is not ready for firmware configuration or build")
endif()
''' + GIT_PRELUDE
PRECONDITION = GIT_PRELUDE + '''# Explicit, isolated composite BLE source candidate; never a default setting.
if(NOT CONFIG_BBP9981_GAMEPAD OR NOT CONFIG_ZMK_BLE OR NOT CONFIG_A320_TAP_OUTPUT OR
   NOT CONFIG_ZMK_POINTING OR NOT CONFIG_ZMK_HID_INDICATORS OR
   NOT CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION OR CONFIG_ZMK_POINTING_SMOOTH_SCROLLING OR CONFIG_BT_EATT OR
   NOT CONFIG_ZMK_HID_REPORT_TYPE_HKRO OR NOT CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL)
    message(FATAL_ERROR "Composite BLE candidate requires pinned gamepad/BLE/tap/pointing/LED/HKRO/full-consumer, enforced subscription, EATT=OFF and smooth=OFF")
endif()
# Notify, CCC and payload-lifetime analysis depends on three pinned Zephyr
# files. Existing guards check the revision and GATT diff but not ATT. Hash raw
# worktree bytes as well as checking the diff, so index flags cannot hide a
# modified file; do not change the SDK or its Git configuration.
foreach(_composite_path IN ITEMS "subsys/bluetooth/host/att.c"
        "subsys/bluetooth/host/gatt.c" "include/zephyr/bluetooth/gatt.h")
    if(_composite_path STREQUAL "subsys/bluetooth/host/att.c")
        set(_composite_expected "5af1301f054a0d3a04fd0f61f66bd1b94316983c")
    elseif(_composite_path STREQUAL "subsys/bluetooth/host/gatt.c")
        set(_composite_expected "069082ec67b0959ef4371f595ba307878ecc0ef6")
    else()
        set(_composite_expected "7d4ac72495b08242a3e80b713b7581ec9b6f921b")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${ZEPHYR_BASE}/${_composite_path}")
    execute_process(COMMAND git -C "${ZEPHYR_BASE}" hash-object --no-filters
        "${_composite_path}" RESULT_VARIABLE _composite_hash_status
        OUTPUT_VARIABLE _composite_blob OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _composite_hash_status EQUAL 0 OR NOT _composite_blob STREQUAL _composite_expected)
        message(FATAL_ERROR "Composite candidate requires exact pinned Zephyr BLE stack file: ${_composite_path}")
    endif()
    execute_process(COMMAND git -C "${ZEPHYR_BASE}" -c core.fsmonitor=false diff --no-ext-diff --quiet HEAD --
        "${_composite_path}" RESULT_VARIABLE _composite_dirty)
    if(NOT _composite_dirty EQUAL 0)
        message(FATAL_ERROR "Composite candidate requires clean pinned Zephyr BLE stack file: ${_composite_path}")
    endif()
endforeach()
# Track all other source files covered by the existing ZMK/USB/HAL guards.
if(DEFINED APPLICATION_SOURCE_DIR)
    foreach(_composite_app IN ITEMS "src/hog.c" "src/usb.c" "src/usb_hid.c" "include/zmk/hid.h")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${APPLICATION_SOURCE_DIR}/${_composite_app}")
    endforeach()
endif()
foreach(_composite_usb IN ITEMS "drivers/usb/device/usb_dc_nrfx.c"
        "subsys/usb/device/class/hid/core.c" "subsys/usb/device/usb_device.c"
        "subsys/usb/device/usb_descriptor.c" "include/zephyr/usb/usb_device.h"
        "include/zephyr/usb/class/usb_hid.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ZEPHYR_BASE}/${_composite_usb}")
endforeach()
if(CONFIG_ZMK_USB)
    if(DEFINED ZEPHYR_HAL_NORDIC_MODULE_DIR)
        set(_composite_hal "${ZEPHYR_HAL_NORDIC_MODULE_DIR}")
    else()
        set(_composite_hal "${ZEPHYR_BASE}/../modules/hal/nordic")
    endif()
    foreach(_composite_hal_file IN ITEMS "nrfx/drivers/src/nrfx_usbd.c"
            "nrfx/drivers/include/nrfx_usbd.h" "nrfx/hal/nrf_usbd.h")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_composite_hal}/${_composite_hal_file}")
    endforeach()
endif()
'''
PURE_SHA = '4317e34b66d9c247b0fb12327516c38c74deea04619db54a05cf190444482a3f'
PURE_HEADER_SHA = 'ced8c185ad628390e54d71e17ffd6ef0ba1996f1b3fac17772c514a03b0b5ea5'
CODEC_SHA = '6e50b49ca0f00bb20466e4d784c8f1fc7ac4e719759df4f01e41dd2f8afdd9ea'
STOCK_SHA = '5c7e4106019e5baf9027c8a24e334c58f5e09c65cd3ff46b338c551c61da4ee3'
LOCKED_SOURCE = (CANDIDATE, PURE, 'experiments/gamepad_protocol/stadia_codec.h',
                 BOARD + '/custom_driver/gamepad_report.h',
                 BOARD + '/custom_driver/gamepad_runtime.h',
                 BOARD + '/custom_driver/gamepad_runtime.c',
                 BOARD + '/custom_driver/trackpad_hog_snapshot.h',
                 BOARD + '/cmake/tap_extensions.cmake',
                 BOARD + '/cmake/gamepad_sdk_guard.cmake',
                 BOARD + '/CMakeLists.txt', BOARD + '/bbp9981.keymap',
                 'config/bbp9981.keymap', 'config/bbp9981.conf')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git_environment():
    """Read only the selected checkout, never inherited Git redirections."""
    env = {key:value for key,value in os.environ.items() if not key.startswith('GIT_')}
    env.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull,
               GIT_TERMINAL_PROMPT='0', GIT_OPTIONAL_LOCKS='0', GIT_NO_REPLACE_OBJECTS='1')
    return env


def source_path(relative, *, directory):
    """Reject symlinks in relative source components; ROOT is normalized separately."""
    candidate = ROOT
    for part in Path(relative).parts:
        if part in ('', '.', '..'):
            raise ValueError('invalid source path component: ' + relative)
        candidate = candidate / part
        if candidate.is_symlink():
            raise ValueError('source symlink is not allowed: ' + str(candidate))
    resolved = candidate.resolve()
    if ROOT not in resolved.parents:
        raise ValueError('source path escapes checkout: ' + relative)
    if directory and not candidate.is_dir():
        raise ValueError('source directory missing or unreadable: ' + relative)
    if not directory and not candidate.is_file():
        raise ValueError('source file missing or unreadable: ' + relative)
    return candidate


def descriptor_bytes(source):
    match = re.search(rb'bbp_exp_report_map\[\]\s*=\s*\{([^}]+)\}', source)
    if not match or not re.fullmatch(rb'\s*(?:0x[0-9a-fA-F]{2}\s*,\s*)+', match[1]):
        raise ValueError('pure descriptor must remain the reviewed literal byte array')
    raw = bytes(int(x, 16) for x in re.findall(rb'0x([0-9a-fA-F]{2})', match[1]))
    if len(raw) != 182 or digest(raw) != PURE_SHA or raw[:8] != bytes.fromhex('05010905a1018503'):
        raise ValueError('pure descriptor changed: re-review before staging')
    return raw


def locked_source_guard(sources):
    """Bind stage-time bytes into its CMake; this is not independent review."""
    lines = ['# Fail closed if staged candidate, runtime, snapshot or keymaps change.']
    for name in LOCKED_SOURCE:
        if name not in sources or not re.fullmatch(r'[A-Za-z0-9_./-]+', name) or '..' in Path(name).parts:
            raise ValueError('candidate source lock is incomplete: ' + name)
        path = '${CMAKE_CURRENT_SOURCE_DIR}/../../../../' + name
        lines.extend([f'if(NOT EXISTS "{path}")',
                      f'    message(FATAL_ERROR "Composite staged source missing: {name}")',
                      'endif()', f'set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "{path}")',
                      f'file(SHA256 "{path}" _composite_source_sha)',
                      f'if(NOT _composite_source_sha STREQUAL "{digest(sources[name])}")',
                      f'    message(FATAL_ERROR "Composite staged source changed: {name}")', 'endif()'])
    return '\n'.join(lines) + '\n'


def stage(out, *, acknowledge_hid_database_change=False):
    if acknowledge_hid_database_change is not True:
        raise ValueError('explicit HID database change acknowledgement is required')
    requested = Path(out).expanduser().absolute()
    if requested.is_symlink():
        raise ValueError('stage target itself must not be a symlink')
    target = requested.resolve()
    if target == ROOT or ROOT in target.parents or target in ROOT.parents:
        raise ValueError('stage must be outside the source tree and not its ancestor')
    if target.exists():
        raise FileExistsError('refusing to overwrite an existing stage: ' + str(target))

    source_roots = [source_path(name, directory=True) for name in ('config','experiments/gamepad_protocol')]
    stock_path = source_path(STOCK, directory=False)
    if any(target == root.resolve() or root.resolve() in target.parents for root in source_roots):
        raise ValueError('stage must be outside every resolved source directory')
    sources = {}
    for root in source_roots:
        for path in sorted(root.rglob('*')):
            if path.is_symlink():
                raise ValueError('source symlink is not allowed: ' + str(path))
            if path.is_file():
                sources[path.relative_to(ROOT).as_posix()] = path.read_bytes()
    git = ['git','-c','core.hooksPath='+os.devnull,'-c','core.fsmonitor=false']
    env = git_environment()
    top = subprocess.check_output([*git,'rev-parse','--show-toplevel'], cwd=ROOT, env=env, text=True).strip()
    if Path(top).resolve() != ROOT:
        raise ValueError('Git checkout root does not match candidate source root')
    dotgit = ROOT/'.git'
    if dotgit.is_symlink():
        raise ValueError('checkout .git must not be a symlink')
    gitdir = Path(subprocess.check_output([*git,'rev-parse','--absolute-git-dir'],cwd=ROOT,env=env,text=True).strip())
    common = Path(subprocess.check_output([*git,'rev-parse','--path-format=absolute','--git-common-dir'],cwd=ROOT,env=env,text=True).strip()).resolve()
    index = Path(subprocess.check_output([*git,'rev-parse','--path-format=absolute','--git-path','index'],cwd=ROOT,env=env,text=True).strip())
    if gitdir.is_symlink() or index.is_symlink() or not index.is_file():
        raise ValueError('Git directory/index must be an existing non-symlink')
    if index.resolve() != (gitdir.resolve()/'index'):
        raise ValueError('Git index does not belong to this checkout')
    if gitdir.resolve() != common and not gitdir.resolve().is_relative_to(common/'worktrees'):
        raise ValueError('linked worktree gitdir does not belong to its common repository')
    tracked = subprocess.check_output([*git,'ls-files','-z','--','config',
                                       'experiments/gamepad_protocol'], cwd=ROOT, env=env).decode('utf-8').split('\0')
    tracked_names = {name for name in tracked if name}
    missing = sorted(tracked_names - sources.keys())
    if missing:
        raise ValueError('tracked candidate sources missing or unreadable: ' + ', '.join(missing))
    # Git status suppresses ignored files, but we copy them if present. Record
    # every copied non-tracked input and never label that stage Git-clean.
    untracked_sources = sorted(sources.keys() - tracked_names)
    required = (GAMEPAD, BOARD_CMAKE, TAP, CANDIDATE, PURE,
                'experiments/gamepad_protocol/stadia_codec.h')
    if any(name not in sources for name in required):
        raise ValueError('candidate source is incomplete; no stage was created')
    if digest(sources[PURE]) != PURE_HEADER_SHA:
        raise ValueError('pure descriptor header changed: re-review before staging')
    pure = descriptor_bytes(sources[PURE])
    if digest(sources['experiments/gamepad_protocol/stadia_codec.h']) != CODEC_SHA:
        raise ValueError('pure gamepad codec changed: re-review before staging')
    stock = bytes.fromhex(stock_path.read_text())
    if len(stock) != 196 or digest(stock) != STOCK_SHA:
        raise ValueError('expected stock map fixture changed: re-review before staging')
    composite = stock + pure[:7] + b'\x04\x15\x00' + pure[8:]
    if len(composite) != 380:
        raise ValueError('unexpected composite descriptor length')
    gamepad = sources[GAMEPAD].decode('utf-8')
    tap = sources[TAP].decode('utf-8')
    if gamepad.count(OLD_PREFIX) != 1 or tap.count(SNAPSHOT_PREFIX) != 1:
        raise ValueError('unrecognized board/snapshot integration: refuse heuristic patching')
    if b'BBP9981_COMPOSITE_EXPERIMENT' not in sources[CANDIDATE]:
        raise ValueError('candidate is missing its explicit build guard')
    guarded_board = (HOST_ONLY_BOARD_GUARD + sources[BOARD_CMAKE].decode('utf-8')).encode('utf-8')
    locked_sources = dict(sources)
    locked_sources[BOARD_CMAKE] = guarded_board
    source_lock = locked_source_guard(locked_sources)
    modified = {
        GAMEPAD:(PRECONDITION + source_lock + gamepad.replace(OLD_PREFIX, NEW_PREFIX, 1)).encode('utf-8'),
        BOARD_CMAKE:guarded_board,
    }
    commit = subprocess.check_output([*git,'rev-parse','HEAD'], cwd=ROOT, env=env, text=True).strip()
    dirty = bool(subprocess.check_output([*git,'status','--porcelain'], cwd=ROOT, env=env)) or bool(untracked_sources)

    # All rejectable source/identity checks are performed before creating output.
    print('Resolved NEW isolated composite source stage:', target, flush=True)
    target.mkdir(parents=True, exist_ok=False)
    manifest = dict(schema=1, profile='ble-composite-experiment', source_commit=commit,
                    source_dirty=dirty, untracked_source_files=untracked_sources,
                    build_performed=False, flashed=False, device_tested=False,
                    ready_for_firmware_build=False, device_test_ready=False, known_ccc_race=True,
                    firmware_configure_forbidden=True, host_cmake_script_test_only=True,
                    fresh_configure_required=True, incremental_guard_mtime_sensitive=True,
                    pnp_vid_pid_changed=False, pnp_source_changed=False,
                    bluetooth_address_configuration_changed=False, hid_database_changed=True,
                    keyboard_mouse_source_retained=True, usb_config_changed=False,
                    keymap_changed=False, settings_changed=False,
                    stock_expected_bytes=len(stock), stock_expected_sha256=digest(stock),
                    pure_descriptor_header_sha256=PURE_HEADER_SHA, pure_codec_sha256=CODEC_SHA,
                    pinned_zephyr_att_blob='5af1301f054a0d3a04fd0f61f66bd1b94316983c',
                    pinned_zephyr_att_sha256='7ebfd7ac8026a0110cc0a750cba0b98a93c5ce61f215d6bae7aa572a626936b7',
                    pinned_zephyr_gatt_c_blob='069082ec67b0959ef4371f595ba307878ecc0ef6',
                    pinned_zephyr_gatt_h_blob='7d4ac72495b08242a3e80b713b7581ec9b6f921b',
                    gamepad_input_id=4, gamepad_input_body_bytes=10,
                    gamepad_output_id=5, gamepad_output_body_bytes=4,
                    expected_map_bytes=len(composite), expected_map_sha256=digest(composite),
                    modified_config=[], files={})
    for name, original in sources.items():
        data = modified.get(name, original)
        dest = target / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        with dest.open('xb') as file:
            file.write(data)
        if dest.read_bytes() != data:
            raise OSError('candidate stage write verification failed: ' + name)
        manifest['files'][name] = dict(bytes=len(data), sha256=digest(data))
        if name in modified:
            manifest['modified_config'].append(dict(path=name, original_sha256=digest(original),
                                                     candidate_sha256=digest(data)))
        elif name.startswith('config/') and data != original:
            raise OSError('unexpected staged config difference: ' + name)
    with (target / 'CANDIDATE.json').open('x') as file:
        json.dump(manifest, file, indent=2, ensure_ascii=False); file.write('\n')
    with (target / 'README_CANDIDATE.md').open('x') as file:
        file.write('''# ISOLATED COMPOSITE SOURCE — NOT A FLASHABLE DELIVERY

The stock BLE keyboard, consumer, mouse, LED and snapshot source paths are
retained; one HID service is expected to add experimental gamepad input/output.
This is a source candidate, not a claim that any host or device accepts it.
USB configuration and all keymaps stay unchanged. No vendor identity is copied.

KNOWN SAFETY GAP: a CCC unsubscribe can be observed before its new value is
committed, so a newer epoch may still send while the old subscription is ON.
Queued notifications cannot be retracted and old-peer neutral/host ACK is not
guaranteed. The staged board rejects ordinary firmware CMake configuration;
its explicit host-script test mode is not firmware. This candidate is NOT ready
for firmware build or device testing; resolve the gap and complete all reviews.

The HID database changes even though PnP fields/address configuration do not.
Host caches or pairing may be affected; there is NO automatic unpair/reset.
A successful host test or source stage is not an ARM build or device acceptance.
CMake configure dependencies trigger checks on ordinary source mtime changes,
but do not guarantee detection of preserved mtimes or changed Git metadata.
Any later authorized build must use a fresh configure and verify its exact
SDK/source bytes again; the current candidate is not ready for such a build.
After a future build, inspect DTS/Kconfig/ELF/UF2, matching manual and rollback
before considering a separate device decision; do not flash, delete pairings,
reset or alter hosts.

Generating this directory performs NO build, upload, Actions, pairing or flash.
CANDIDATE.json records expected (not hardware-read) map values and all source
hashes. The 4-byte gamepad output is accepted but has no physical vibration.
''')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    parser.add_argument('--acknowledge-hid-database-change', action='store_true',
                        help='acknowledge this is an unbuilt source experiment with changed host-visible HID services')
    args = parser.parse_args()
    if not args.acknowledge_hid_database_change:
        parser.error('explicit --acknowledge-hid-database-change is required; this is NOT a flashable delivery')
    stage(args.out, acknowledge_hid_database_change=args.acknowledge_hid_database_change)


if __name__ == '__main__':
    main()
