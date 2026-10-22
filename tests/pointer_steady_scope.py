#!/usr/bin/env python3
"""Explicit v2.3.2 authorization, separate from every immutable older BASE.

Removing only the new feature's preprocessor blocks must restore the complete
sealed production tree byte-for-byte. Older proofs use that verified OFF view;
the new runner executes the actual enabled driver/HOG instead. No oracle swap.
"""
from functools import lru_cache
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SEALED = 'cee926aa736e6f4f01278212c727e3e73f2403d1'
DRIVER = 'config/boards/arm/bbp9981/custom_driver/'
FILTER = DRIVER + 'trackpad_cursor_filter.h'
CHANGED = {DRIVER+'a320_0x57.c', DRIVER+'trackpad_hog_snapshot.h',
           'config/bbp9981.conf', 'config/boards/arm/bbp9981/Kconfig.board'}
CONF_ADDITION = '''
# v2.3.2: ordinary cursor only; preserve tap classifier, scroll and gamepad.
CONFIG_A320_CURSOR_STEADY=y
'''
KCONFIG_ADDITION = '''config A320_CURSOR_STEADY
    bool "Bounded mouse jitter/spike filter and BLE burst coalescing"
    depends on BOARD_BBP9981 && ZMK_POINTING && I2C && GPIO
    default n
    help
      Apply one-count per-axis hysteresis and the four-slow-sample spike
      limiter only to committed ordinary cursor motion, before fixed gain.
      Tap classification, scroll and gamepad keep their original raw input.
      BLE may merge adjacent same-direction XY in a 10 ms admission window;
      button/zero/wheel reports remain ordered barriers, without extra delay.

'''


@lru_cache(maxsize=None)
def sealed(path):
    return subprocess.check_output(['git', 'show', SEALED+':'+path], cwd=ROOT)


def strip_steady_blocks(data):
    lines = data.decode().splitlines(keepends=True)
    out, depth, blocks = [], 0, 0
    for line in lines:
        directive = line.strip()
        if not depth and directive == '#ifdef CONFIG_A320_CURSOR_STEADY':
            depth = 1
            blocks += 1
        elif depth:
            if directive.startswith(('#if ', '#ifdef ', '#ifndef ')):
                depth += 1
            elif directive.startswith('#endif'):
                depth -= 1
            elif depth == 1:
                assert not directive.startswith(('#else', '#elif')), 'No hidden OFF branch allowed'
        else:
            out.append(line)
    assert depth == 0 and blocks, 'Unbalanced/missing explicit feature blocks'
    return ''.join(out).encode()


def restore(path, data):
    if path in {DRIVER+'a320_0x57.c', DRIVER+'trackpad_hog_snapshot.h'}:
        return strip_steady_blocks(data)
    if path in {'config/bbp9981.conf', 'config/boards/arm/bbp9981/Kconfig.board'}:
        text = CONF_ADDITION if path.endswith('.conf') else KCONFIG_ADDITION
        newline = '\r\n' if b'\r\n' in sealed(path) else '\n'
        addition = text.replace('\n', newline).encode()
        assert data.count(addition) == 1, ('Missing exact authorized addition', path)
        return data.replace(addition, b'')
    return data


@lru_cache(maxsize=1)
def verified_legacy_view():
    paths = subprocess.check_output(['git','ls-tree','-r','--name-only',SEALED,'config'],
                                    cwd=ROOT,text=True).splitlines()
    actual = {str(p.relative_to(ROOT)) for p in (ROOT/'config').rglob('*') if p.is_file()}
    assert actual == set(paths) | {FILTER}, ('Unapproved files', actual ^ (set(paths)|{FILTER}))
    changed = {p for p in paths if (ROOT/p).read_bytes() != sealed(p)}
    assert changed == CHANGED, ('Unapproved production scope', changed ^ CHANGED)
    result = {}
    for path in paths:
        result[path] = restore(path, (ROOT/path).read_bytes())
        assert result[path] == sealed(path), 'Changed outside explicit v2.3.2 feature: '+path
    # ALL pre-existing C oracles/SDK fixtures/mocks remain byte-exact except the
    # official nonblocking peek declaration needed by the enabled HOG extension.
    tests = subprocess.check_output(['git','ls-tree','-r','--name-only',SEALED,'tests'],
                                    cwd=ROOT,text=True).splitlines()
    for path in tests:
        if Path(path).suffix not in {'.c', '.h'}:
            continue
        data = (ROOT/path).read_bytes()
        if path == 'tests/hog_snapshot_mocks/zephyr/kernel.h':
            addition = b'int k_msgq_peek(struct k_msgq *q, void *data);\n'
            assert data.count(addition) == 1
            data = data.replace(addition, b'')
        assert data == sealed(path), 'Frozen oracle/SDK fixture changed: '+path
    return result


def main():
    view = verified_legacy_view()
    print(f'PASS v2.3.2 scope: {len(view)-len(CHANGED)}/{len(view)} sealed configs byte-exact; '
          'only 4 explicit feature-block additions + cursor filter; complete OFF tree restored; '
          'ALL pre-existing C oracles/SDK fixtures frozen (only official peek mock declaration added)')


if __name__ == '__main__':
    main()
