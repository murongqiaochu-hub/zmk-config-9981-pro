#!/usr/bin/env python3
"""Final BLE candidate: unchanged baseline oracle plus actual independent service.

No old oracle/SDK fixture is relaxed. The full original 11ee89e host suite runs
in a private byte-exact checkout, then the actual new HIDS is compiled separately.
Unaffected config (including runtime/USB/HOG prefixes/keymaps/settings) must be
byte-identical before either result can count. Host tests are not radio/OS tests.
"""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = '11ee89eee92969a15f75a0d5848c38c6a2c2ce15'
DRIVER = 'config/boards/arm/bbp9981/custom_driver/'
SERVICE = DRIVER + 'gamepad_ble_service.c'
ADAPTER = DRIVER + 'gamepad_android_report.h'
# Frozen reviewed production inputs, updated only after actual implementation review.
EXPECTED = {
    SERVICE: 'd9004f4a65db6514bf09ded0ce3a0ff954e870298fab1bce163dbbb7f615c991',
    ADAPTER: '65ee8de2f3655d05ad2c87943fa4bddb03b6d2b860be8799f6808935c7d5b230',
}


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def paths(prefix):
    return [p for p in git('ls-tree', '-r', '--name-only', '-z', BASE, prefix).decode().split('\0') if p]


def scope(root=ROOT):
    original = set(paths('config'))
    current = {p.relative_to(root).as_posix() for p in (root/'config').rglob('*') if p.is_file()}
    assert current == original | {ADAPTER}, ('unexpected config paths', current ^ (original | {ADAPTER}))
    assert not any(p.is_symlink() for p in (root/'config').rglob('*')), 'config symlink'
    for p in original - {SERVICE}:
        assert (root/p).read_bytes() == git('show', BASE+':'+p), 'baseline config drift: '+p
    for p in paths('tests'):
        assert (root/p).read_bytes() == git('show', BASE+':'+p), 'frozen test/oracle drift: '+p
    assert set(EXPECTED) == {SERVICE, ADAPTER}, 'missing exact reviewed production hashes'
    for p, digest in EXPECTED.items():
        assert hashlib.sha256((root/p).read_bytes()).hexdigest() == digest, 'reviewed production drift: '+p
    print('PASS exact final scope: 85 unchanged config files, one replaced service, one added BLE adapter; ALL old tests frozen', flush=True)


def negatives():
    with tempfile.TemporaryDirectory(prefix='final-android-scope-') as name:
        root = Path(name)
        for folder in ('config', 'tests'):
            shutil.copytree(ROOT/folder, root/folder, ignore=shutil.ignore_patterns('__pycache__'))
        changed = [SERVICE, ADAPTER, DRIVER+'gamepad_runtime.c', DRIVER+'gamepad_report.h',
                   DRIVER+'gamepad_usb.c', DRIVER+'trackpad_hog_snapshot.h',
                   'config/boards/arm/bbp9981/cmake/gamepad.cmake', 'config/bbp9981.conf',
                   'config/bbp9981.keymap', 'config/boards/arm/bbp9981/bbp9981.keymap',
                   'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c', 'tests/test_gamepad_ble_service.c']
        for item in changed:
            file = root/item
            before = file.read_bytes()
            file.write_bytes(before+b'\n/* must be rejected */\n')
            try:
                scope(root)
            except AssertionError:
                pass
            else:
                raise AssertionError('accepted scope mutation: '+item)
            finally:
                file.write_bytes(before)
        print('PASS 12 final-scope mutation negatives', flush=True)


def frozen_host():
    with tempfile.TemporaryDirectory(prefix='final-android-frozen-') as name:
        root = Path(name)
        for item in paths('.'):
            # Historical firmware archives are not inputs to any host test.
            if item.startswith('firmware-delivery/'):
                continue
            dest = root/item
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(git('show', BASE+':'+item))
        env = {k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
        env['PYTHONDONTWRITEBYTECODE'] = '1'
        common = Path(git('rev-parse', '--path-format=absolute', '--git-common-dir').decode().strip())
        assert common.is_absolute() and (common/'objects').is_dir()
        subprocess.run(['git', 'init', '-q', str(root)], check=True, env=env)
        info = root/'.git/objects/info'; info.mkdir(parents=True, exist_ok=True)
        (info/'alternates').write_text(str(common/'objects')+'\n')
        for args in [('symbolic-ref','HEAD','refs/heads/frozen-keymouse'),
                     ('update-ref','refs/heads/frozen-keymouse',BASE), ('read-tree',BASE)]:
            subprocess.run(['git', *args], cwd=root, env=env, check=True)
        print('RUN original unchanged 11ee89e complete host suite; CMake is mandatory in CI', flush=True)
        subprocess.run([sys.executable, '-B', 'tests/run_full_repair_tests.py'], cwd=root, env=env, check=True)


def main():
    scope()
    subprocess.run([sys.executable, '-B', 'tests/check_manual_sync.py'], cwd=ROOT, check=True)
    negatives()
    frozen_host()
    subprocess.run([sys.executable, '-B', 'tests/run_android_ble_service_tests.py'], cwd=ROOT, check=True)
    print('PASS final BLE candidate: frozen complete host + actual independent Android service; HOST ONLY', flush=True)


if __name__ == '__main__':
    main()
