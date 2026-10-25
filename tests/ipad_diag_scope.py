#!/usr/bin/env python3
"""Exact diagnostic-only projection to immutable 17192e6; no broad exemptions.

Full-byte hashes bind the reviewed additive hooks and new implementation. Each
existing file must be insertion-only against the pinned candidate. The previous
metadata scope then independently checks its original 63-file tree/oracles.
"""
from pathlib import Path
import difflib
import hashlib
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '17192e63fdc94e8080d61f89cd97724339bd64c7'
BOARD = 'config/boards/arm/bbp9981/'
CONF = 'config/bbp9981.conf'
EXISTING = {
    CONF: '975daa27e9a86b5b8d7fa93f2ff4e4578e1e500fb6504543edffb172129a3525',
    BOARD + 'Kconfig.board': 'f26f6f2cb182687558bd1d85e312dbef284ff298e92447cabeffe17d6f84a090',
    BOARD + 'CMakeLists.txt': '7647b31526fc6eed2edf96af0f6e85f8e9b931e4b5231a440b99fc642d2a8df1',
    BOARD + 'custom_driver/gamepad_ble_extension.h': '55fd9db5d836bb2fea2311de9d4d20ed5ea1ebee3781db52e9099be2398361d8',
}
NEW = {
    BOARD + 'custom_driver/ipad_diag.h': 'c6b2e21612b3703e77b8c1b622a2cac980cb53e597e14b85966d62109b65c7d9',
    BOARD + 'custom_driver/ipad_diag_hog.h': '0ec1825f23ec7c11cfb1acabe33cebeacd65510adc6e873f6c6690ff6b9ea266',
    BOARD + 'custom_driver/ipad_diag_core.h': 'e1abc83f55f5f316e5f6d3f6bde2bb0c1082652b21c84c2841df4be4c43f7839',
    BOARD + 'custom_driver/ipad_diag.c': '6d75f0e8ceacaffc72407bcd3b7fc1c6e292679d43adc17cb5b937b742dded91',
}


def original(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT)


def project_config(current):
    result = dict(current)
    # Exact file content, not a name-prefix exemption. Any extra byte fails.
    for path, expected in {**EXISTING, **NEW}.items():
        assert path in current, ('missing diagnostic path', path)
        assert hashlib.sha256(current[path]).hexdigest() == expected, ('unreviewed diagnostic bytes', path)
    for path in EXISTING:
        before = original(path)
        delta = difflib.SequenceMatcher(None, before.splitlines(keepends=True),
                                       current[path].splitlines(keepends=True), autojunk=False)
        assert all(op in ('equal', 'insert') for op, *_ in delta.get_opcodes()), ('not insertion-only', path)
        result[path] = before
    for path in NEW:
        del result[path]
    return result


def project_conf(text):
    # Old tests also exercise the previous metadata-only candidate; it still
    # flows through that old strict checker without being exempted here.
    if 'CONFIG_BBP9981_IPAD_DIAG' not in text:
        return text
    raw = text.replace('\r\n', '\n').replace('\n', '\r\n').encode()
    assert hashlib.sha256(raw).hexdigest() == EXISTING[CONF], 'unexpected diagnostic conf'
    return original(CONF).decode().replace('\r\n', '\n')


def selftest():
    current = {p.relative_to(ROOT).as_posix(): p.read_bytes()
               for p in (ROOT / 'config').rglob('*') if p.is_file()}
    projected = project_config(current)
    assert len(current) == 67 and len(projected) == 63
    for path in (*EXISTING, *NEW):
        bad = dict(current)
        bad[path] += b'\n'
        try:
            project_config(bad)
        except AssertionError:
            pass
        else:
            raise AssertionError(('extra byte accepted', path))
        bad = dict(current)
        del bad[path]
        try:
            project_config(bad)
        except AssertionError:
            pass
        else:
            raise AssertionError(('missing path accepted', path))
    text = current[CONF].decode().replace('\r\n', '\n')
    assert project_conf(text) == original(CONF).decode().replace('\r\n', '\n')
    for bad in (text + '\n', text.replace('CONFIG_BBP9981_IPAD_DIAG=y', 'CONFIG_BBP9981_IPAD_DIAG=n')):
        try:
            project_conf(bad)
        except AssertionError:
            pass
        else:
            raise AssertionError('bad diagnostic conf accepted')
    print('PASS exact IPDIAG delta: 4 insertion-only old files, 4 hash-bound new files; mutation/deletion negatives')


if __name__ == '__main__':
    selftest()
