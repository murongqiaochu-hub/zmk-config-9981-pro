#!/usr/bin/env python3
"""Dedicated, no-install HID replay; objects/executables are anonymous memfds.

Run from any directory: PYTHONDONTWRITEBYTECODE=1 python3 <this-file>
No parent runner/build files are changed; -pipe avoids compiler stage temp files.
All stock fixtures are byte-for-byte downloads from ZMK's v0.3.0 tag.
"""
from pathlib import Path
import hashlib
import os
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MOCKS = ROOT / "tests/hid_snapshot_mocks"
PREFIX = ROOT / "config/boards/arm/bbp9981/custom_driver/trackpad_hid_owners.h"
SOURCE = ROOT / "tests/test_sdk_hid.c"
STOCK_BASE = "https://raw.githubusercontent.com/zmkfirmware/zmk/v0.3.0/app/"
STOCK_HASHES = {
    "tests/sdk_v0_3_0_hid.c": "347d4dd0ce09364354dd41b600c2b9582c294f9af52c95ee260bd79e928d2719",
    "tests/hid_snapshot_mocks/zmk/hid.h": "dd161a6631a9dad59f320389a0c146712b2c953215f4a97a8d0d64b0b3ffdee7",
    "tests/hid_snapshot_mocks/zmk/pointing.h": "a3f2a82239ad803776273a8e49abd604394a65256640b91334dfc3cfdb214bbc",
    "tests/hid_snapshot_mocks/zmk/keys.h": "dac8729882c0c674f4eb6e2508da0784abdd071e9c4c87f3baa528b83e7b06e3",
    "tests/hid_snapshot_mocks/dt-bindings/zmk/modifiers.h": "c2c741d1306546ac10bbb5925299f2f1accd7ea97788adf8a9cb99a8281d773f",
    "tests/hid_snapshot_mocks/dt-bindings/zmk/pointing.h": "69862dd52baf3159e6352f9357c9d5d10eaf29a171ef10ae7dd04aec72481b6d",
    "tests/hid_snapshot_mocks/dt-bindings/zmk/hid_usage.h": "bdc33ad32cae4132d518930d1d546a31694a4c9be23ba3e91ddac072ddd4364d",
    "tests/hid_snapshot_mocks/dt-bindings/zmk/hid_usage_pages.h": "f73be7e38cc51706fc804867639e03f9d114e2467c4c6f1255618f31a2da9cb8",
    "tests/hid_snapshot_mocks/dt-bindings/zmk/keys.h": "ba77257a1ee189c1b360c8860bc51a0226afb1b5c6ba53899dc309e1e73fdd9e",
}
for name, expected in STOCK_HASHES.items():
    actual = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    assert actual == expected, (name, actual, expected)
print("PASS unchanged v0.3.0 SDK source + 8 official headers SHA256", flush=True)

CC = shlex.split(os.environ.get("CC", "cc"))
print(subprocess.check_output([*CC, "--version"], text=True).splitlines()[0], flush=True)
FLAGS = ["-pipe", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=undefined", "-fno-sanitize-recover=all"]
PUBLIC = {"zmk_hid_mouse_button_press", "zmk_hid_mouse_button_release",
          "zmk_hid_mouse_buttons_press", "zmk_hid_mouse_buttons_release"}
STOCK = {"bbp9981_stock_" + name for name in PUBLIC}
GETTER = "bbp9981_hid_mouse_button_references"
CONFIGS = [("HKRO6/basic", 1, 0, 0), ("HKRO6/full+smooth", 1, 1, 1),
           ("HKRO6/pointing-off", 0, 0, 0)]
for label, pointing, full, smooth in CONFIGS:
    config = {
        "CONFIG_ZMK_HID_REPORT_TYPE_HKRO": 1,
        "CONFIG_ZMK_HID_REPORT_TYPE_NKRO": 0,
        "CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE": 6,
        "CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE": 6,
        "CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_BASIC": 1 - full,
        "CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL": full,
        "CONFIG_ZMK_HID_KEYBOARD_NKRO_EXTENDED_REPORT": 0,
        "CONFIG_ZMK_POINTING": pointing,
        "CONFIG_ZMK_POINTING_SMOOTH_SCROLLING": smooth,
        "CONFIG_ZMK_USB_BOOT": 1,
        "CONFIG_ZMK_HID_INDICATORS": 1,
        "CONFIG_ZMK_LOG_LEVEL": 4,
    }
    obj = os.memfd_create("sdk-hid-object", 0)
    exe = os.memfd_create("sdk-hid-replay", 0)
    obj_path, exe_path = f"/proc/self/fd/{obj}", f"/proc/self/fd/{exe}"
    try:
        compile_cmd = [*CC, *FLAGS, *[f"-D{k}={v}" for k, v in config.items()],
                       "-I", str(MOCKS), "-include", str(PREFIX),
                       "-c", str(SOURCE), "-o", obj_path]
        link_cmd = [*CC, *FLAGS, obj_path, "-o", exe_path]
        print("CONFIG " + label, flush=True)
        print("COMPILE " + shlex.join(compile_cmd), flush=True)
        subprocess.run(compile_cmd, cwd=ROOT, pass_fds=(obj,), check=True)
        print("LINK " + shlex.join(link_cmd), flush=True)
        subprocess.run(link_cmd, cwd=ROOT, pass_fds=(obj, exe), check=True)
        nm = subprocess.check_output(["nm", "-S", "--defined-only", exe_path],
                                     pass_fds=(exe,), text=True)
        symbols = {}
        for line in nm.splitlines():
            fields = line.split()
            if len(fields) == 4:
                symbols.setdefault(fields[3], []).append(fields)
        wanted = PUBLIC | STOCK | {GETTER}
        if pointing:
            for name in wanted:
                assert len(symbols.get(name, [])) == 1, (name, symbols.get(name))
                assert symbols[name][0][2] == "T", (name, symbols[name])
            for name in PUBLIC:
                assert symbols[name][0][0] != symbols["bbp9981_stock_" + name][0][0]
            storage = symbols["explicit_button_counts"]
            assert len(storage) == 1 and int(storage[0][1], 16) == 20, storage
            assert storage[0][2] in {"b", "d"}, storage
            print("PASS nm: 4 public + 4 stock + Getter distinct; ONE local int[5] storage", flush=True)
        else:
            assert not (wanted & symbols.keys()), wanted & symbols.keys()
            assert "explicit_button_counts" not in symbols
            print("PASS nm: pointing-off has no button wrapper/stock/Getter storage", flush=True)
        subprocess.run([exe_path], cwd=ROOT, pass_fds=(exe,), check=True,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
    finally:
        os.close(obj)
        os.close(exe)
print("PASS SDK HID replay: 3 configurations; -Wall -Wextra -Werror + UBSan; memfd only", flush=True)
