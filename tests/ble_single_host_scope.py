#!/usr/bin/env python3
"""Exact v2.3.4 authorization and historical v2.3.3 configuration projection.

Do not update the historical BASE/oracles to make a feature pass. New policy
files are explicitly authorized; all previous production bodies and keymaps
remain exact. This projection is not a claim of feature-OFF binary identity.
"""
from functools import lru_cache
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '432d6c857382769963afd65de0f803a303a67e59'
DRIVER = 'config/boards/arm/bbp9981/custom_driver/'
CMAKE = 'config/boards/arm/bbp9981/cmake/'
CHANGED = {'config/bbp9981.conf', 'config/boards/arm/bbp9981/Kconfig.board',
           'config/boards/arm/bbp9981/CMakeLists.txt'}
ADDED = {DRIVER + 'ble_single_host.c', DRIVER + 'ble_single_host.h',
         DRIVER + 'ble_single_host_prefix.h', CMAKE + 'ble_single_host.cmake',
         CMAKE + 'ble_single_host_sdk_guard.cmake'}
CONF_ADDITION = '''
# v2.3.4: retain bonds but keep only the selected BLE host connected.
CONFIG_BBP9981_BLE_SINGLE_HOST=y
'''
CMAKE_ADDITION = '    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ble_single_host.cmake")\n'
KCONFIG_ADDITION = '''config BBP9981_BLE_SINGLE_HOST
    bool "Keep only the selected Bluetooth host connected"
    depends on BOARD_BBP9981 && ZMK_BLE && (!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL)
    default n
    help
      Disconnect inactive incoming hosts on profile selection and reconnect.
      Retain bonds and allow new pairing on an empty selected profile.
      Central-role links and split-peripheral builds are not managed.
      A rejected host can appear connected briefly; no link-layer whitelist
      or directed advertising is enabled, preserving privacy compatibility.

'''


@lru_cache(maxsize=None)
def base(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT)


@lru_cache(maxsize=1)
def base_paths():
    return subprocess.check_output(
        ['git', 'ls-tree', '-r', '--name-only', BASE, 'config'], cwd=ROOT,
        text=True).splitlines()


def remove_exact(path, data, text):
    newline = '\r\n' if b'\r\n' in base(path) else '\n'
    block = text.replace('\n', newline).encode()
    assert data.count(block) == 1, ('Missing/excess single-host addition', path)
    return data.replace(block, b'')


def restore(path, data):
    if path == 'config/bbp9981.conf':
        return remove_exact(path, data, CONF_ADDITION)
    if path == 'config/boards/arm/bbp9981/Kconfig.board':
        return remove_exact(path, data, KCONFIG_ADDITION)
    if path == 'config/boards/arm/bbp9981/CMakeLists.txt':
        return remove_exact(path, data, CMAKE_ADDITION)
    return data


def verify_configuration(current):
    paths = base_paths()
    assert set(current) == set(paths) | ADDED, (
        'Unapproved v2.3.4 config paths', set(current) ^ (set(paths) | ADDED))
    changes = {p for p in paths if current[p] != base(p)}
    assert changes == CHANGED, ('Unapproved v2.3.4 production scope', changes ^ CHANGED)
    view = {p: restore(p, current[p]) for p in paths}
    for p in paths:
        assert view[p] == base(p), 'Changed outside exact single-host additions: ' + p
    return view


@lru_cache(maxsize=1)
def verified_v233_view():
    current = {str(p.relative_to(ROOT)): p.read_bytes()
               for p in (ROOT / 'config').rglob('*') if p.is_file()}
    view = verify_configuration(current)
    tests = subprocess.check_output(
        ['git', 'ls-tree', '-r', '--name-only', BASE, 'tests'], cwd=ROOT,
        text=True).splitlines()
    for p in tests:
        if Path(p).suffix in {'.c', '.h'}:
            assert (ROOT / p).read_bytes() == base(p), (
                'Frozen pre-existing C oracle/SDK fixture changed: ' + p)
    return view


def main():
    view = verified_v233_view()
    print(f'PASS v2.3.4 scope: {len(view)-len(CHANGED)}/{len(view)} old configs byte-exact; '
          f'{len(CHANGED)} exact additions + {len(ADDED)} new BLE policy files; '
          'all old C oracles/fixtures frozen; complete v2.3.3 projection restored')


if __name__ == '__main__':
    main()
