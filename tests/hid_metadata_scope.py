#!/usr/bin/env python3
"""Exact four-chunk HID metadata delta over the first Xbox release; no exemptions."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = "1c61b817f7e2d52fd2bbea888d1867f354e4c2a8"
BOARD = "config/boards/arm/bbp9981/"
CONF = "config/bbp9981.conf"
CONF_INSERT = """
# BLE metadata compatibility candidate; retain every HID report and control.
CONFIG_BBP9981_BLE_HID_METADATA_COMPAT=y
# 1D50/615E is the assigned USB-IF ZMK identity, not a Bluetooth SIG company ID.
CONFIG_BT_DIS_PNP_VID_SRC=2
# A new standard DIS characteristic also changes the native GATT database hash.
CONFIG_BT_DIS_SW_REV=y
CONFIG_BT_DIS_SW_REV_STR="2.2.1-ipad-hid1"
"""
KCONFIG_INSERT = """config BBP9981_BLE_HID_METADATA_COMPAT
    bool "Standards-compliant BLE HID metadata candidate"
    depends on BBP9981_GAMEPAD && ZMK_BLE && BT_DIS && BT_DIS_PNP
    default n
    help
      Advertise implemented HID 1.11 in the HID Information read response while
      preserving its country and flags. This does not change any report map,
      report body, characteristic, CCC, route, security or pairing policy.
      The product config also corrects the USB-IF PnP namespace and adds the
      standard DIS software revision characteristic for native schema refresh.
      Software validation is not a claim of iPad device compatibility.

"""
CMAKE_INSERT = """        if(CONFIG_BBP9981_BLE_HID_METADATA_COMPAT)
            execute_process(COMMAND git -C "${ZEPHYR_BASE}" diff --quiet HEAD --
                subsys/bluetooth/services/dis.c subsys/bluetooth/services/Kconfig.dis
                RESULT_VARIABLE _hid_metadata_dis_dirty)
            if(NOT _hid_metadata_dis_dirty EQUAL 0)
                message(FATAL_ERROR "HID metadata candidate requires clean pinned DIS sources")
            endif()
            message(STATUS "HID_METADATA_COMPAT bcdHID=0111 PnPSource=${CONFIG_BT_DIS_PNP_VID_SRC} SWRevision=${CONFIG_BT_DIS_SW_REV_STR}")
        endif()
"""
INFO_INSERT = """#if IS_ENABLED(CONFIG_BBP9981_BLE_HID_METADATA_COMPAT)
#pragma message("BBP9981_BLE_HID_METADATA_COMPAT_ACTIVE")
    if (attr && attr->uuid && value && value_len == 4U &&
        bt_uuid_cmp(attr->uuid, BT_UUID_HIDS_INFO) == 0) {
        const uint8_t *original = value;
        /* The native helper copies synchronously; no pointer is retained.
         * HID 1.11 is little-endian 0x0111. Country and flags remain stock. */
        const uint8_t corrected[4] = {0x11U, 0x01U, original[2], original[3]};
        return bt_gatt_attr_read(conn, attr, buf, len, offset, corrected, sizeof(corrected));
    }
#endif
"""
CMAKE_ANCHOR = '        foreach(_fixture IN ITEMS "src/hog.c" "src/usb_hid.c" "src/usb.c" "include/zmk/hid.h")\n'
INFO_ANCHOR = "                                       const void *value, uint16_t value_len) {\n"
PATCHES = {
    CONF: [("CONFIG_USB_HID_REPORTS=4\n", "CONFIG_USB_HID_REPORTS=4\n" + CONF_INSERT)],
    BOARD + "Kconfig.board": [("\nendmenu\n", "\n" + KCONFIG_INSERT + "endmenu\n")],
    BOARD + "CMakeLists.txt": [(CMAKE_ANCHOR, CMAKE_INSERT + CMAKE_ANCHOR)],
    BOARD + "custom_driver/gamepad_ble_extension.h": [
        (" * All other HOG reads keep the SDK helper's offset/error semantics unchanged.\n",
         " * Other HOG reads keep the SDK helper's offset/error semantics; the optional\n"
         " * metadata correction below changes only the two bcdHID bytes of HID Information.\n"),
        (INFO_ANCHOR, INFO_ANCHOR + INFO_INSERT),
    ],
}


def _git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root)


def _original(root, path):
    return _git(root, "show", BASE + ":" + path)


def _expected(path, original):
    """Apply only reviewed literals, retaining the original file's exact EOLs."""
    if path not in PATCHES:
        return original
    crlf = path == CONF
    if crlf:
        assert b"\r\n" in original and original.count(b"\n") == original.count(b"\r\n"), path
    else:
        assert b"\r" not in original, path
    for before, after in PATCHES.get(path, ()):
        a, b = before.encode(), after.encode()
        if crlf:
            a, b = a.replace(b"\n", b"\r\n"), b.replace(b"\n", b"\r\n")
        assert original.count(a) == 1, (path, "baseline anchor is not unique", before)
        original = original.replace(a, b, 1)
    return original


def projected_legacy_conf(conf: str) -> str:
    """Reject any unrelated conf delta BEFORE projecting to legacy's LF input."""
    original = _original(ROOT, CONF)
    expected = _expected(CONF, original).decode().replace("\r\n", "\n")
    assert conf == expected, "conf differs from the exact reviewed metadata insertion"
    return original.decode().replace("\r\n", "\n")


def _baseline(root):
    names = _git(root, "ls-tree", "-r", "-z", "--name-only", BASE, "config")
    paths = [p.decode() for p in names.split(b"\0") if p]
    assert len(paths) == 63 and set(PATCHES) <= set(paths), "first Xbox config tree changed"
    return {p: _original(root, p) for p in paths}


def _verify(original, current):
    assert set(current) == set(original), (
        "config path set changed", sorted(set(original) - set(current)),
        sorted(set(current) - set(original)))
    for path, data in original.items():
        assert current[path] == _expected(path, data), (path, "outside exact metadata delta")
    changed = {p for p in original if original[p] != current[p]}
    assert changed == set(PATCHES) and len(original) - len(changed) == 59
    keymaps = {p for p in original if p.endswith(".keymap")}
    assert keymaps and not (keymaps & changed), "all keymaps and bindings must remain byte-exact"


def check_scope(root=ROOT):
    """Verify all 63 config files, including all maps, controls and SDK guards."""
    root = Path(root).resolve()
    original = _baseline(root)
    entries = list((root / "config").rglob("*"))
    assert not any(p.is_symlink() for p in entries), "config symlink is not in the frozen tree"
    current = {p.relative_to(root).as_posix(): p.read_bytes() for p in entries if p.is_file()}
    _verify(original, current)
    return original, current


def selftest():
    """Controlled negatives are memory-only; never mutate a frozen file."""
    original, current = check_scope()
    legacy = original[CONF].decode().replace("\r\n", "\n")
    candidate = current[CONF].decode().replace("\r\n", "\n")
    assert projected_legacy_conf(candidate) == legacy
    for bad in (legacy, candidate + "\n", candidate.replace("CONFIG_ZMK_BLE=y", "CONFIG_ZMK_BLE=n")):
        try:
            projected_legacy_conf(bad)
        except AssertionError:
            pass
        else:
            raise AssertionError("conf projection accepted an unreviewed candidate")
    keymap = next(p for p in original if p.endswith(".keymap"))
    for path in (*PATCHES, keymap):
        bad = dict(current)
        bad[path] += b"\n"
        try:
            _verify(original, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError((path, "scope accepted an extra byte"))
    for bad in (dict(current, **{"config/unreviewed.conf": b""}),
                {p: b for p, b in current.items() if p != keymap}):
        try:
            _verify(original, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError("scope accepted an added/deleted config path")


if __name__ == "__main__":
    assert sys.argv[1:] in ([], ["--selftest"]), "usage: hid_metadata_scope.py [--selftest]"
    selftest() if sys.argv[1:] else check_scope()
    print("PASS HID metadata scope: exact four reviewed deltas; 59/63 config files and all keymaps byte-identical")
