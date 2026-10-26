#!/usr/bin/env python3
"""Require the additive Map oracle to pass locally and reject the discarded slot selector.
Historical production code is obtained from pinned local Git objects, never a remote.
No hardware or OS compatibility claim is made.
"""
from pathlib import Path
import os
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OLD = '3d5dd36'
DRIVER = 'config/boards/arm/bbp9981/custom_driver'

def main():
    flags = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-parameter', '-fsanitize=undefined', '-fno-sanitize-recover=all',
             '-I' + str(ROOT / 'tests/gamepad_ble_mocks'), '-I' + str(ROOT / DRIVER)]
    with tempfile.TemporaryDirectory(prefix='9981-map-oracle-') as directory:
        temp = Path(directory)
        fixed = temp / 'fixed'
        subprocess.run([*flags, str(ROOT / 'tests/test_gamepad_map_stability.c'),
                        '-o', str(fixed)], check=True)
        subprocess.run([str(fixed)], check=True)
        for name in ['gamepad_ble_extension.h', 'gamepad_ble_map.h']:
            data = subprocess.check_output(['git', 'show', f'{OLD}:{DRIVER}/{name}'], cwd=ROOT)
            (temp / name).write_bytes(data)
        legacy = (ROOT / 'tests/test_gamepad_ble.c').read_text()
        legacy = legacy.replace('../' + DRIVER + '/', str(ROOT / DRIVER) + '/')
        legacy = legacy.replace(str(ROOT / DRIVER / 'gamepad_ble_extension.h'),
                                str(temp / 'gamepad_ble_extension.h'))
        legacy = legacy.replace('"gamepad_ble_mocks/sdk_v0_3_0_hog.c"',
                                '"' + str(ROOT / 'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c') + '"')
        (temp / 'test_gamepad_ble.c').write_text(legacy)
        (temp / 'test_gamepad_map_stability.c').write_bytes(
            (ROOT / 'tests/test_gamepad_map_stability.c').read_bytes())
        mutant = temp / 'mutant'
        subprocess.run([*flags, str(temp / 'test_gamepad_map_stability.c'),
                        '-o', str(mutant)], check=True)
        result = subprocess.run([str(mutant)], capture_output=True, text=True,
                                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        assert result.returncode != 0, 'discarded global-slot selector unexpectedly passed'
        assert 'memcmp(out + offset, expected + offset, count)' in result.stderr, result.stderr
        print('PASS counterfactual: 3d5dd36 rejected by new peer/fragment Map oracle')
        print(result.stderr.strip())

if __name__ == '__main__':
    main()
