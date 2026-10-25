#!/usr/bin/env python3
"""First-cut scope oracle: byte-bound inverse projection, never broad exemptions."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FAILED = '1c61b817f7e2d52fd2bbea888d1867f354e4c2a8'
BASE = '28f6c86'
PREFIX = 'config/boards/arm/bbp9981/custom_driver/'
CMAKE = 'config/boards/arm/bbp9981/CMakeLists.txt'
STUB = PREFIX + 'gamepad_ble_disabled.c'
OLD = b'''            # AFTER the existing mouse snapshot prefix. Tail attrs keep5/9/13.
            set_property(SOURCE "${APPLICATION_SOURCE_DIR}/src/hog.c"
                DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS
                "-include" "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/gamepad_ble_extension.h")'''
NEW = b'''            # First iPad bisect: retain stock keyboard/mouse HOG, disable BLE gamepad.
            zephyr_library_sources(custom_driver/gamepad_ble_disabled.c)'''
STUB_BYTES = b'''/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include "gamepad_runtime.h"

/* First iPad bisect: no BLE gamepad GATT/report/security side effects. */
int bbp_gamepad_ble_send(struct bt_conn *captured_conn,
                         const struct bbp_gamepad_report_body *body,
                         uint32_t expected_context_epoch) {
    (void)captured_conn;
    (void)body;
    (void)expected_context_epoch;
    return -ENOTSUP;
}
'''

def blob(rev, path):
    return subprocess.check_output(['git', 'show', rev + ':' + path], cwd=ROOT)

def check():
    paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', FAILED, 'config'],
                                    cwd=ROOT, text=True).splitlines()
    current = {p.relative_to(ROOT).as_posix() for p in (ROOT/'config').rglob('*') if p.is_file()}
    assert current == set(paths) | {STUB}, 'unexpected config addition/deletion'
    assert (ROOT/STUB).read_bytes() == STUB_BYTES, 'stub must remain exact fail-closed implementation'
    for p in paths:
        actual = (ROOT/p).read_bytes()
        expected = blob(FAILED, p)
        if p == CMAKE:
            assert actual.count(NEW) == expected.count(OLD) == 1
            assert OLD not in actual and NEW not in expected
            actual = actual.replace(NEW, OLD)
        assert actual == expected, p + ': outside first-cut byte boundary'
    # This preinclude is the only retained HOG modifier; it must be identical
    # to the accepted non-gamepad keyboard/mouse baseline, not a reimplementation.
    snapshot = PREFIX + 'trackpad_hog_snapshot.h'
    assert (ROOT/snapshot).read_bytes() == blob(BASE, snapshot)
    cmake = (ROOT/CMAKE).read_text()
    assert cmake.count('custom_driver/trackpad_hog_snapshot.h') == 1
    assert 'custom_driver/gamepad_ble_extension.h' not in cmake
    assert cmake.count('custom_driver/gamepad_ble_disabled.c') == 1
    print('PASS first-cut scope: all failed-version config bytes unchanged except exact BLE CMake replacement + exact inert stub; baseline HOG snapshot byte-identical; USB/runtime/keymap untouched')

if __name__ == '__main__':
    check()
