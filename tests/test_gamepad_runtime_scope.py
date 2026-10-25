#!/usr/bin/env python3
"""Reject config drift in temporary copies; preserve the real frozen tree."""
from pathlib import Path
import shutil
import tempfile
import check_gamepad_runtime_scope as scope


def append(root, path):
    p = root / path
    p.write_bytes(p.read_bytes() + b'\n')


def replace(root, before, after):
    p = root / scope.CM
    data = p.read_bytes()
    assert data.count(before) == 1
    p.write_bytes(data.replace(before, after, 1))


cases = {
    'runtime-byte': lambda r: append(r, scope.PREFIX + 'behavior_gamepad.c'),
    'disabled-byte': lambda r: append(r, scope.PREFIX + 'gamepad_ble_disabled.c'),
    'neutral-byte': lambda r: append(r, scope.PREFIX + 'gamepad_usb_neutral.c'),
    'cmake-outside': lambda r: append(r, scope.CM),
    'cmake-shim-restored': lambda r: replace(r, scope.NEW, scope.OLD),
    'cmake-extra-shim': lambda r: replace(r, scope.NEW, scope.NEW + scope.OLD),
    'cmake-duplicate': lambda r: replace(r, scope.NEW, scope.NEW + scope.NEW),
    'conf': lambda r: append(r, 'config/bbp9981.conf'),
    'keymap': lambda r: append(r, 'config/bbp9981.keymap'),
    'extra-hidden': lambda r: (r / 'config/.unexpected').write_bytes(b'x'),
    'missing-runtime': lambda r: (r / (scope.PREFIX + 'behavior_gamepad.c')).unlink(),
    'missing-neutral': lambda r: (r / (scope.PREFIX + 'gamepad_usb_neutral.c')).unlink(),
    'symlink': lambda r: (r / 'config/unexpected-link').symlink_to(scope.ROOT / 'config/bbp9981.conf'),
}
assert scope.project_config()
for name, mutate in cases.items():
    with tempfile.TemporaryDirectory(prefix='runtime-scope-') as temp:
        root = Path(temp)
        shutil.copytree(scope.ROOT / 'config', root / 'config')
        mutate(root)
        try:
            scope.project_config(root)
        except AssertionError:
            pass
        else:
            raise AssertionError('runtime scope accepted mutation: ' + name)
print(f'PASS runtime scope rejection: {len(cases)} independent mutations; worktree untouched')
