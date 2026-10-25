#!/usr/bin/env python3
"""Adversarial gate checks; mutations happen only in temporary config copies."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from unittest.mock import patch
import check_usb_enumeration_scope as scope

REAL_ROOT = scope.ROOT

def original_git(*args):
    return subprocess.check_output(['git', *args], cwd=REAL_ROOT)

def append(root, name, data):
    p = root / name
    p.write_bytes(p.read_bytes() + data)

def replace(root, name, before, after):
    p = root / name
    assert before in p.read_bytes()
    p.write_bytes(p.read_bytes().replace(before, after, 1))

cases = {
    'conf-value': lambda r: replace(r, scope.CONF, b'CONFIG_USB_HID_REPORTS=4', b'CONFIG_USB_HID_REPORTS=5'),
    'conf-outside-hunk': lambda r: append(r, scope.CONF, b'\n# drift\n'),
    'cmake-inside-hunk': lambda r: replace(r, scope.CM, b'USB enumeration isolation:', b'USB enumeration isolation changed:'),
    'cmake-outside-hunk': lambda r: append(r, scope.CM, b'\n# drift\n'),
    'original-header': lambda r: append(r, scope.PREFIX + 'gamepad_report.h', b'\n'),
    'shim': lambda r: append(r, scope.SHIM, b'\n'),
    'unrelated-keymap': lambda r: append(r, 'config/bbp9981.keymap', b'\n'),
    'extra-hidden': lambda r: (r / 'config/.unexpected').write_bytes(b'x'),
    'missing-original': lambda r: (r / (scope.PREFIX + 'gamepad_usb.c')).unlink(),
    'missing-baseline': lambda r: (r / 'config/bbp9981.keymap').unlink(),
    'symlink': lambda r: (r / 'config/unexpected-link').symlink_to(REAL_ROOT / 'config/bbp9981.conf'),
}
assert scope.project_config()
for name, mutate in cases.items():
    with tempfile.TemporaryDirectory(prefix='usb-scope-') as temp:
        root = Path(temp)
        shutil.copytree(REAL_ROOT / 'config', root / 'config')
        mutate(root)
        with patch.object(scope, 'ROOT', root), patch.object(scope, 'git', original_git):
            try:
                scope.project_config()
            except AssertionError:
                pass
            else:
                raise AssertionError('scope accepted mutation: ' + name)
print(f'PASS USB scope rejection: {len(cases)} independent mutations; real worktree untouched')
