#!/usr/bin/env python3
"""Exact inverse projection of enumeration seam; no broad config exemptions."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '72c0cee'
ORIGINAL = '1c61b81'
CM = 'config/boards/arm/bbp9981/CMakeLists.txt'
PREFIX = 'config/boards/arm/bbp9981/custom_driver/'
HEADERS = [PREFIX + 'gamepad_' + n + '.h' for n in ['ble_extension', 'runtime', 'report', 'stick']]
SHIM = PREFIX + 'gamepad_neutral_shim.c'
SHIM_BYTES = b'''/* SPDX-License-Identifier: MIT
 * Enumeration-only isolation seam, NOT a playable BLE gamepad runtime.
 * No input subscriptions, workers, notifications or security requests.
 * Original extension/report headers are retained byte-for-byte; their dynamic
 * runtime is deliberately absent. This experiment is not a mathematical complement.
 */
#include "gamepad_runtime.h"

uint32_t bbp_gamepad_context_epoch(void) { return 0U; }
bool bbp_gamepad_context_valid(uint32_t expected) { return expected == 0U; }
void bbp_gamepad_transport_invalidate(void) {}
void bbp_gamepad_get_report(uint8_t transport, struct bbp_gamepad_report_body *out) {
    (void)transport;
    if (out) { *out = bbp_gamepad_neutral(); }
}
'''

def git_bytes(rev, path):
    return subprocess.check_output(['git', 'show', rev + ':' + path], cwd=ROOT)

def project_config():
    paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', BASE, 'config'], cwd=ROOT, text=True).splitlines()
    actual = {p.relative_to(ROOT).as_posix() for p in (ROOT / 'config').rglob('*') if p.is_file() or p.is_symlink()}
    assert actual == set(paths) | set(HEADERS) | {SHIM}, ('config path set', actual ^ (set(paths) | set(HEADERS) | {SHIM}))
    assert not any((ROOT / p).is_symlink() for p in actual), 'No config symlink escape'
    for p in HEADERS:
        assert (ROOT / p).read_bytes() == git_bytes(ORIGINAL, p), ('original header changed', p)
    assert (ROOT / SHIM).read_bytes() == SHIM_BYTES, 'neutral seam changed'
    block = (ROOT / 'tests/ble_enum_cmake.txt').read_bytes()
    cm = (ROOT / CM).read_bytes()
    assert cm.count(block) == 1, 'CMake addition must match exact reviewed fixture'
    projected = {p: (ROOT / p).read_bytes() for p in paths}
    projected[CM] = cm.replace(block, b'', 1)
    for p in paths:
        assert projected[p] == git_bytes(BASE, p), ('non-seam config drift', p)
    return projected

if __name__ == '__main__':
    projected = project_config()
    print(f'PASS BLE enum scope: {len(projected)} baseline files exact after one CMake block removal; 4 original headers + exact neutral shim only')
