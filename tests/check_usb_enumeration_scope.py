#!/usr/bin/env python3
"""Exact inverse projection to v2.0: two insertion hunks + six originals + shim.

No regex stripping or exception-only allowlist: every config path and byte is
checked before any legacy oracle receives the projected original bytes.
"""
from pathlib import Path
import hashlib
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '72c0cee'
ORIGINAL = '1c61b81'
PREFIX = 'config/boards/arm/bbp9981/custom_driver/'
CM = 'config/boards/arm/bbp9981/CMakeLists.txt'
CONF = 'config/bbp9981.conf'
ORIGINAL_NAMES = ('gamepad_usb.c', 'gamepad_usb_extension.h', 'gamepad_usb_lifecycle.h',
                  'gamepad_report.h', 'gamepad_runtime.h', 'gamepad_stick.h')
SHIM = PREFIX + 'gamepad_usb_neutral.c'
CONF_ADD = b'\r\n# USB enumeration isolation only: HID_1, no gamepad runtime.\r\nCONFIG_USB_HID_DEVICE_COUNT=2\r\nCONFIG_USB_HID_REPORTS=4\r\n'
CM_START = b'    # USB enumeration isolation: original HID_1 adapter, neutral read-only shim.\n'
CM_END = b'    if(CONFIG_BBP9981_MENU_DIAG)\n'
CM_ADD_SHA256 = '4ed7bfc8ab2ae76f292e912669eb0b8e19639123106eb1878cbf838f9342699b'
SHIM_SHA256 = 'c6c1040e74399ae4731002705a0e4b7ab03fd2e25ee3f33edc1b6ee1be3603cb'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def blob(rev, path):
    return git('show', rev + ':' + path)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def project_config():
    """Validate the WHOLE tree, then return its byte-exact v2.0 projection."""
    base = git('ls-tree', '-r', '--name-only', BASE, 'config').decode().splitlines()
    new = {PREFIX + name for name in ORIGINAL_NAMES} | {SHIM}
    files = list((ROOT / 'config').rglob('*'))
    assert not any(p.is_symlink() for p in files), 'config symlinks forbidden'
    actual = {p.relative_to(ROOT).as_posix() for p in files if p.is_file()}
    assert actual == set(base) | new, ('config path drift', sorted(actual ^ (set(base) | new)))
    for name in ORIGINAL_NAMES:
        path = PREFIX + name
        assert (ROOT / path).read_bytes() == blob(ORIGINAL, path), 'original USB byte drift: ' + path
    assert digest((ROOT / SHIM).read_bytes()) == SHIM_SHA256, 'neutral shim drift'
    projected = {}
    for path in base:
        data = (ROOT / path).read_bytes()
        if path == CONF:
            assert data.count(CONF_ADD) == 1, 'conf insertion must occur exactly once'
            data = data.replace(CONF_ADD, b'', 1)
        elif path == CM:
            assert data.count(CM_START) == 1 and data.count(CM_END) == 1, 'CMake insertion anchors'
            start = data.index(CM_START)
            end = data.index(CM_END)
            assert start < end, 'CMake insertion order'
            assert digest(data[start:end]) == CM_ADD_SHA256, 'exact CMake insertion drift'
            data = data[:start] + data[end:]
        assert data == blob(BASE, path), 'v2.0 inverse projection mismatch: ' + path
        projected[path] = data
    assert len(new) == 7
    return projected


if __name__ == '__main__':
    result = project_config()
    print(f'PASS USB enumeration scope: {len(result)} v2.0 config files inverse-byte-identical; exactly 2 insertions + 6 original files + neutral shim')
