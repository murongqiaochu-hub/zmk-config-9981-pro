#!/usr/bin/env python3
"""Fail-closed inverse projection: exactly one CMake replacement + two pinned TUs."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '1014c94'
PREFIX = 'config/boards/arm/bbp9981/custom_driver/'
CM = 'config/boards/arm/bbp9981/CMakeLists.txt'
OLD = b'        zephyr_library_sources(custom_driver/gamepad_usb.c custom_driver/gamepad_usb_neutral.c)\n'
NEW = b'        zephyr_library_sources(custom_driver/gamepad_usb.c custom_driver/behavior_gamepad.c\n            custom_driver/gamepad_ble_disabled.c)\n'
ADDED = {PREFIX + 'behavior_gamepad.c': '1c61b81',
         PREFIX + 'gamepad_ble_disabled.c': 'd05c9e1'}


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def project_config(root=None, git_read=None):
    root = ROOT if root is None else root
    git_read = git if git_read is None else git_read
    base = set(git_read('ls-tree', '-r', '--name-only', BASE, 'config').decode().splitlines())
    files = list((root / 'config').rglob('*'))
    assert not any(p.is_symlink() for p in files), 'config symlinks forbidden'
    actual = {p.relative_to(root).as_posix() for p in files if p.is_file()}
    assert actual == base | set(ADDED), ('runtime config path drift', sorted(actual ^ (base | set(ADDED))))
    for path, rev in ADDED.items():
        assert (root / path).read_bytes() == git_read('show', rev + ':' + path), 'runtime original byte drift: ' + path
    projected = {}
    for path in sorted(base):
        data = (root / path).read_bytes()
        if path == CM:
            assert data.count(NEW) == 1, 'runtime CMake replacement must occur exactly once'
            assert OLD not in data, 'neutral shim must not compile'
            data = data.replace(NEW, OLD, 1)
        assert data == git_read('show', BASE + ':' + path), 'USB-only inverse projection mismatch: ' + path
        projected[path] = data
    return projected


if __name__ == '__main__':
    result = project_config()
    print(f'PASS runtime scope: {len(result)} config files byte-identical to 1014c94 after exactly one CMake replacement + two pinned additions; no Kconfig/keymap/DT/tap drift')
