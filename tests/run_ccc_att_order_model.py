#!/usr/bin/env python3
"""Strict host-only model for app-owned CCC/send admission ordering.

This does not compile pinned Zephyr or prove ATT, radio, host, or device behavior.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tests/test_ccc_att_order_model.c'
CC = shlex.split(os.environ.get('CC', 'cc'))
if not CC:
    raise RuntimeError('CC must name a compiler')
COMMON = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
          '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']


def main():
    if not __debug__:
        raise RuntimeError('the model oracle must not run under Python -O')
    with tempfile.TemporaryDirectory(prefix='9981-ccc-att-model-') as directory:
        temp = Path(directory).resolve()
        if not temp.is_dir() or temp.is_symlink():
            raise RuntimeError('unexpected model temporary directory')
        for sanitizer in ('undefined', 'address'):
            flags = ['-fsanitize=' + sanitizer]
            if sanitizer == 'undefined':
                flags += ['-fno-sanitize-recover=all']
            executable = temp / ('ccc-att-order-' + sanitizer)
            subprocess.run([*CC, *COMMON, *flags, str(SOURCE), '-o', str(executable)],
                           cwd=ROOT, check=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True)
            print('PASS ' + sanitizer + ' CCC/app-queue ordering model', flush=True)
    print('LIMIT abstract app queue only; public bt_gatt_notify_cb/ATT path remains unmodeled')


if __name__ == '__main__':
    main()
