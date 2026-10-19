#!/usr/bin/env python3
"""Standalone true-source old/current contrast; no runner/production writes.

Run: python3 -B tests/test_micro_source_contrast.py
BASE is the user's flashed-345 source baseline. git show extracts ONLY the
listed public firmware sources, byte-for-byte, into TemporaryDirectory. The
same C input driver compiles twice; HAS_NATIVE_GUARD only connects the new
position listener (which the old runtime genuinely does not have).

Real code: variant runtime + matching runtime header/gesture core, unchanged
shoulder behavior + shoulder core. Mock code: event fan-out, stock native input
and tap-output bridge acceptance/12ms hold. These are NOT hardware, CGEvent,
real HOG/USB notifications, transport delivery, or host ACK measurements.
"""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "config/boards/arm/bbp9981/custom_driver"
BASE = "7e485368865409f05c42c7ae5bd955f1f9ccb91c"
SOURCE = ROOT / "tests/test_micro_source_contrast.c"
PREVIOUS_STUBS = Path(
    "/vol1/1000/agent/output/1017/013704_菜单同排按键列隔离实验/"
    "01_固件源码/.host-test-build/include"
)
OLD_FILES = (
    "trackpad_microtap_runtime.c",
    "trackpad_microtap_runtime.h",
    "trackpad_microtap.h",
    "behavior_microtap_shoulder.c",
    "trackpad_shoulder.h",
)
FROZEN_FILES = (
    "behavior_microtap_shoulder.c", "trackpad_shoulder.h",
    "trackpad_tap_output.h", "menu_click_diag.h",
)
STUB_HEADERS = (
    "zephyr/device.h", "zephyr/kernel.h", "zephyr/irq.h",
    "zephyr/sys/atomic.h", "zephyr/logging/log.h", "drivers/behavior.h",
    "dt-bindings/zmk/mouse.h", "zmk/behavior.h", "zmk/event_manager.h",
    "zmk/endpoints.h", "zmk/hid.h", "zmk/events/endpoint_changed.h",
    "zmk/events/position_state_changed.h", "zmk/events/layer_state_changed.h",
)


def old_bytes(name):
    relative = (DRIVER / name).relative_to(ROOT).as_posix()
    return subprocess.check_output(
        ["git", "show", f"{BASE}:{relative}"], cwd=ROOT, timeout=15
    )


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def check_result(variant, case, row):
    assert row["case"] == case, row
    assert row["nativeDownAccepted"] == row["nativeUpAccepted"] == 1, row
    assert row["rawRefs"] == 0 and row["rawKnown"] == 1, row
    assert row["rawPhysicalDown"] == 0, row
    expected_auto = int(variant == "old" or case == "C")
    for key in ("autoSubmitAccepted", "autoDownAccepted", "autoUpAccepted"):
        assert row[key] == expected_auto, (variant, case, key, row)
    assert row["quiet_ms"] == (60 if variant == "old" else 120), row
    assert row["gap_ms"] == (200 if variant == "old" else 500), row
    expected_down = {"A": 100, "B": 110, "C": 70}[case]
    if variant == "current":
        expected_down = 130 if case == "C" else -1
    assert row["autoDownAt"] == expected_down, (variant, case, row)
    assert row["autoUpAt"] == (expected_down + 12 if expected_auto else -1), row
    assert row["motionFrames"] == 3 * (1 - expected_auto), row
    assert row["motionAt"] == ({"A": 40, "B": 50}[case] if not expected_auto else -1), row
    assert row["rawEnabled"] == expected_auto, row
    native_walls = {"A": (10, 30), "B": (50, 50), "C": (200, 220)}[case]
    assert (row["nativeDownAt"], row["nativeUpAt"]) == native_walls, row
    native_events = (0, 30) if case == "B" else native_walls
    assert (row["nativeEventDownAt"], row["nativeEventUpAt"]) == native_events, row
    if case == "C":
        # Passing MUST retain this limitation: both completed autos precede the
        # later, preserved native click. Do not move native into auto's hold.
        assert row["autoUpAt"] < row["nativeDownAt"] == 200, row
        assert row["autoDownAccepted"] + row["nativeDownAccepted"] == 2, row


def main():
    current = {name: (DRIVER / name).read_bytes()
               for name in set(OLD_FILES + FROZEN_FILES)}
    baseline = {name: old_bytes(name) for name in set(OLD_FILES + FROZEN_FILES)}
    for name in FROZEN_FILES:
        assert current[name] == baseline[name], f"Frozen source changed: {name}"
    assert b"native_position" not in baseline["trackpad_microtap_runtime.c"]
    assert b"MICRO_NATIVE_GUARD_MS 600U" in current["trackpad_microtap_runtime.c"]
    print(f"BASE {BASE} (flashed-345 source baseline)", flush=True)
    for variant, contents in (("old", baseline), ("current", current)):
        for name in OLD_FILES[:3]:
            print(f"SHA256 {variant} {name} {sha256(contents[name])}", flush=True)
    print(f"SHA256 shared-input-driver {sha256(SOURCE.read_bytes())}", flush=True)

    cc = shlex.split(os.environ.get("CC", "cc"))
    assert cc, "CC must name a host C compiler"
    results = {}
    with tempfile.TemporaryDirectory(prefix="micro-source-contrast-") as temporary:
        build = Path(temporary)
        old = build / "old"
        old.mkdir()
        for name in OLD_FILES:
            target = old / name
            target.write_bytes(baseline[name])
            assert target.read_bytes() == baseline[name], name
        # Never run/import run_host_tests.py (it writes the shared build tree).
        # Reuse generated empty SDK includes, falling back per missing header.
        # Any unavailable empty include is created ONLY inside this tempdir.
        overlay = build / "include"
        stub_roots = (ROOT / ".host-test-build/include", PREVIOUS_STUBS)
        reused = set()
        for relative in STUB_HEADERS:
            target = overlay / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            for candidate_root in stub_roots:
                candidate = candidate_root / relative
                if candidate.is_file():
                    target.write_bytes(candidate.read_bytes())
                    reused.add(str(candidate_root))
                    break
            else:
                target.write_text("/* Host declarations come from test_env.h. */\n")
        print("Host SDK include stubs reused read-only: " + ", ".join(sorted(reused)), flush=True)
        for variant, source_dir, has_guard in (("old", old, 0), ("current", DRIVER, 1)):
            exe = build / f"contrast_{variant}"
            command = [
                *cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                # Empty listener/device macros and the shared SDK fixture leave
                # known unused callback/mock declarations, as in host runner.
                "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable",
                "-fsanitize=undefined", "-fno-sanitize-recover=all", "-g",
                "-I", str(overlay), "-I", str(DRIVER),
                f'-DMICRO_RUNTIME_C="{source_dir / "trackpad_microtap_runtime.c"}"',
                f'-DMICRO_RUNTIME_H="{source_dir / "trackpad_microtap_runtime.h"}"',
                f'-DMICRO_SHOULDER_C="{source_dir / "behavior_microtap_shoulder.c"}"',
                f"-DHAS_NATIVE_GUARD={has_guard}", str(SOURCE), "-o", str(exe),
            ]
            subprocess.run(command, cwd=ROOT, check=True, timeout=60)
            for case in "ABC":
                # New process = true zero-init; never reset variant private state.
                run = subprocess.run([str(exe), case], cwd=ROOT,
                                     capture_output=True, text=True, timeout=10)
                assert run.returncode == 0, (
                    f"{variant} {case} failed ({run.returncode}):\n"
                    f"{run.stdout}\n{run.stderr}"
                )
                assert not run.stderr, run.stderr
                row = json.loads(run.stdout)
                check_result(variant, case, row)
                results[variant, case] = row
                print(
                    f"PASS {variant:7s} {case}: "
                    f"nativeDown/UpAccepted={row['nativeDownAccepted']}/{row['nativeUpAccepted']} "
                    f"native@{row['nativeDownAt']}/{row['nativeUpAt']} "
                    f"autoDown/UpAccepted={row['autoDownAccepted']}/{row['autoUpAccepted']} "
                    f"auto@{row['autoDownAt']}/{row['autoUpAt']} "
                    f"motionFrames={row['motionFrames']} "
                    f"rawEnabled={row['rawEnabled']} refs={row['rawRefs']} "
                    f"quiet={row['quiet_ms']}ms", flush=True,
                )
        for name, before in current.items():
            assert (DRIVER / name).read_bytes() == before, f"Source changed during run: {name}"

    assert len(results) == 6
    print("PASS A: actual old runtime reproduces native -> auto at100; current preserves native only.")
    print("PASS B: actual shoulder adapter flushes D/U at50 with original timestamps0/30; "
          "refs=0 and busy=false BEFORE same-poll 3-frame raw candidate. Old auto@110; current auto=0 "
          "through400 (>raw50+quiet120), from the native600 guard, not quiet120 alone.")
    print("PASS C LIMITATION RETAINED: old auto70/82 and current auto130/142 finish BEFORE "
          "native200/220. BOTH retain native and count TWO mock clicks; an already-sent "
          "auto cannot be retracted by later native input.")
    print("PASS true-source contrast under -Wall/-Wextra/-Werror + UBSan. "
          "Acceptance is simulated enqueue/bridge acceptance, NOT hardware/CGEvent, "
          "real HOG/USB delivery, or host ACK. No production/runner/CI writes.")


if __name__ == "__main__":
    main()
