#!/usr/bin/env python3
"""Linux/no-device regression for the passive Mac MDIAG collector.

Run: python3 -B -m unittest discover -s tests -p 'test_menu_capture.py' -v
No port is opened. All serial, monotonic/UTC and signal IO is injected. A few
exclusive-file tests leave small self-test artifacts under the task root (not
in the source tree); no dependencies, runner edits, serial writes or devices.
"""
import ast
import contextlib
import copy
import datetime
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("menu_capture_under_test", ROOT / "tools" / "capture_menu_diag.py")
cap = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = cap
SPEC.loader.exec_module(cap)


def reference_crc(words):
    result = 2166136261
    for word in words:
        for byte in (word & 0xFFFFFFFF).to_bytes(4, "little", signed=False):
            result ^= byte
            result = (result * 16777619) % (1 << 32)
    return result


def wire(t, seq, kind, a=0, b=0, c=0, d=0, *, build=cap.BUILD, corrupt=False):
    crc = reference_crc((t, seq, kind, a, b, c, d))
    if corrupt:
        crc ^= 1
    return (f"MDIAG v=2 build={build} t={t} seq={seq} k={kind} "
            f"a={a} b={b} c={c} d={d} crc={crc:08x}\n").encode("ascii")


def pair(t, seq, route=0, *, profile=0, mask=None, drops=0, queued=0, active=0):
    mask = (1 << route) if mask is None else mask
    return (wire(t, seq, 11, route, route, mask, profile)
            + wire(t, (seq + 1) & cap.MASK32, 15, drops, queued, active, 2))


def stable(route=0, start=1000, seq=0, drops=0):
    return [(float(n * 2), pair((start + n * 2000) & cap.MASK32,
                               (seq + n * 2) & cap.MASK32, route, drops=drops)) for n in range(3)]


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def utc(self):
        base = datetime.datetime(2026, 10, 16, tzinfo=datetime.timezone.utc)
        return (base + datetime.timedelta(seconds=self.now)).isoformat()


class FakeSignals:
    SIGINT, SIGTERM = signal.SIGINT, signal.SIGTERM

    def __init__(self):
        self.original = {self.SIGINT: "prior-int", self.SIGTERM: "prior-term"}
        self.handlers = dict(self.original)

    def getsignal(self, number):
        return self.handlers[number]

    def signal(self, number, handler):
        old = self.handlers[number]
        self.handlers[number] = handler
        return old

    def fire(self, number):
        self.handlers[number](number, None)


class FakeIO:
    """No write method and no fallback to real syscalls."""
    def __init__(self, clock, events=(), *, fail=(), busy=False, fire=None):
        self.clock = clock
        self.events = list(events)
        self.calls = []
        self.fail = set(fail)
        self.busy, self.fire = busy, fire
        self.old = ["original", [1, 2, 3]]
        self.low_calls = 0

    def _check(self, point):
        if point in self.fail:
            raise OSError("NEVER_LOG_EXCEPTION_SECRET")

    def preflight(self, port):
        self.calls.append(("preflight", port))
        if self.busy:
            raise cap.CaptureError("existing_serial_opener_stop_without_opening")
        self._check("preflight")

    def open(self, port):
        self.calls.append(("open", port))
        self._check("open")
        return 41

    def is_character(self, fd):
        self.calls.append(("is_character", fd))
        return "not_character" not in self.fail

    def get_attrs(self, fd):
        self.calls.append(("get_attrs", fd))
        self._check("get_attrs")
        return copy.deepcopy(self.old)

    def configured_attrs(self, old):
        return ["configured", list(old[1])]

    def set_attrs(self, fd, attrs):
        operation = "restore" if attrs == self.old else "configure"
        self.calls.append((operation, fd, copy.deepcopy(attrs)))
        self._check(operation)

    def dtr(self, fd, high):
        self.calls.append(("dtr", fd, high))
        if not high:
            self.low_calls += 1
        self._check("dtr_high" if high else "dtr_low_cleanup" if self.low_calls > 1 else "dtr_low_initial")

    def exclusive(self, fd, enabled):
        self.calls.append(("exclusive", fd, enabled))
        self._check("exclusive_on" if enabled else "exclusive_off")

    def wait_readable(self, fd, timeout):
        self.calls.append(("wait", fd, timeout))
        self._check("wait")
        target = self.clock.now + timeout
        if self.fire and self.fire[0] <= target + 1e-10:
            when, api, number = self.fire
            self.clock.now = max(self.clock.now, when)
            self.fire = None
            api.fire(number)
            return False
        if self.events and self.events[0][0] <= target + 1e-10:
            self.clock.now = max(self.clock.now, self.events[0][0])
            return True
        self.clock.now = target
        return False

    def read(self, fd, size):
        self.calls.append(("read", fd, size))
        self._check("read")
        _, data = self.events.pop(0)
        if isinstance(data, Exception):
            raise data
        return data

    def close(self, fd):
        self.calls.append(("close", fd))
        self._check("close")


class MemoryReady(io.StringIO):
    def close(self):
        self.was_closed = True  # Keep readable for assertions after capture.


class CaptureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Explicitly scoped task-root artifacts, never arbitrary /tmp or devices.
        cls.artifacts = Path(tempfile.mkdtemp(prefix="采集器自测_", dir=ROOT.parent))

    def run_session(self, events=(), *, seconds=5, fail=(), busy=False, fire_number=None,
                    exclusive=False, ready_path=None, output=None):
        clock, signals = FakeClock(), FakeSignals()
        fire = (0.6, signals, fire_number) if fire_number is not None else None
        backend = FakeIO(clock, events, fail=fail, busy=busy, fire=fire)
        stdout = io.StringIO()
        output = output if output is not None else io.StringIO()
        memory = MemoryReady()
        creates = []

        def create(path):
            creates.append(path)
            return memory

        ready = cap.ReadyChannel(stdout, ready_path or "memory.ready.jsonl", create=create)
        config = cap.Config("/dev/cu.usbmodemPRIVATE_SERIAL", seconds, True, True, exclusive)
        code = cap.run_capture(config, io=backend, clock=clock, output=output,
                               stdout=stdout, ready=ready, signals=signals)
        text = output.getvalue()
        rows = [json.loads(line) for line in text.splitlines()]
        state = [json.loads(line) for line in memory.getvalue().splitlines()]
        self.assertEqual(signals.handlers, signals.original)
        self.assertLessEqual(clock.now, seconds + 1e-8)
        self.assertNotIn("PRIVATE_SERIAL", text + stdout.getvalue() + memory.getvalue())
        self.assertNotIn("NEVER_LOG_EXCEPTION_SECRET", text + stdout.getvalue())
        return code, rows, state, backend, stdout.getvalue(), creates

    def test_crc_little_endian_signed_twos_complement(self):
        # Shared published vectors also exercised by the firmware C regression.
        self.assertEqual(cap.fnv1a32((0, 0, 0, 0, 0, 0, 0)), 0xF5CBF9F5)
        self.assertEqual(cap.fnv1a32((0x12345678, 0x9ABCDEF0, 13, 7, 2, 1, -1)), 0xA4C3A518)
        self.assertEqual(cap.fnv1a32((0xFFFFFFFF, 0xFFFFFFFF, 15, -1, -(1 << 31), (1 << 31) - 1, 2)), 0xB5FC3D08)
        words = (0x12345678, 0xFFFFFFFF, 13, -1, -(1 << 31), (1 << 31) - 1, 0)
        self.assertEqual(cap.fnv1a32(words), reference_crc(words))
        self.assertNotEqual(cap.fnv1a32(words), cap.fnv1a32(tuple(reversed(words))))
        with self.assertRaises(ValueError):
            cap.fnv1a32((1, 2))

    def test_valid_input_and_raw_schema_exclude_log_prefix(self):
        line = b"\x1b[32mPREFIX_SERIAL_SECRET \x1b[0m" + wire(7, 8, 3, 4, 1, -2, 0).rstrip(b"\n") + b"\x1b[0m\r"
        row = cap.parse_line(line)
        self.assertEqual((row["protocol"], row["kind"], row["a"], row["c"]), (2, 3, 4, -2))
        self.assertTrue(row["raw_diag"].startswith("MDIAG v=2"))
        self.assertNotIn("PREFIX_SERIAL_SECRET", json.dumps(row))

    def test_fragmentation_every_boundary_and_multiple_lines(self):
        data = wire(0, 0, 3, 1) + b"ordinary INPUT_SECRET coordinates 124 789\n" + pair(1000, 1)
        expected = [3, 11, 15]
        for split in range(len(data) + 1):
            framer = cap.LineFramer()
            rows = framer.feed(data[:split]) + framer.feed(data[split:])
            self.assertEqual([row["kind"] for row in rows if row], expected)
            self.assertIsNone(framer.finish())
        framer = cap.LineFramer()
        rows = []
        for byte in data:
            rows.extend(framer.feed(bytes([byte])))
        self.assertEqual([row["kind"] for row in rows if row], expected)
        self.assertNotIn("INPUT_SECRET", json.dumps(rows))

    def test_crc_failure_safe_evidence_only(self):
        raw = b"SERIAL_PREFIX_SECRET " + wire(7, 8, 3, 4, corrupt=True).rstrip(b"\n")
        row = cap.parse_line(raw)
        self.assertEqual(row["reason"], "crc_failure")
        self.assertEqual(row["byte_length"], len(raw))
        self.assertEqual(row["sha256"], cap.hashlib.sha256(raw).hexdigest())
        self.assertNotIn("raw_diag", row)
        self.assertNotIn("a", row)
        self.assertNotIn("SERIAL_PREFIX_SECRET", json.dumps(row))

    def test_missing_build_character_is_quality_not_version_switch(self):
        bad = wire(5200, 6, 3, build="tap200_rote_20261016")
        events = stable() + [(4.2, bad), (6.0, pair(7000, 6)),
                             (8.0, pair(9000, 8)), (10.0, pair(11000, 10))]
        code, rows, state, _, stdout, _ = self.run_session(events, seconds=11)
        self.assertEqual(code, 0)
        bad_rows = [r for r in rows if r["event"] == "diagnostic_invalid"]
        self.assertEqual(bad_rows[0]["reason"], "build_token_anomaly")
        self.assertEqual(bad_rows[0]["diagnostic_build_token"], "tap200_rote_20261016")
        self.assertEqual([r["current_valid"] for r in state], [True, False, True, False])
        self.assertNotIn("reflash", stdout.lower())
        self.assertFalse(any(r.get("event") == "firmware_version_changed" for r in rows))

    def test_malformed_noise_does_not_leak_arbitrary_text(self):
        secrets = [b"ORDINARY_PASSWORD_SECRET", b"OTHER_KEYCODE_999", b"COORDINATES_400_600",
                   b"SERIAL_SECRET", b"BUILD_PRIVATE_SECRET"]
        data = (b" ".join(secrets[:4]) + b"\n"
                + b"prefix SERIAL_SECRET MDIAG v=2 build=BUILD_PRIVATE_SECRET text=ORDINARY_PASSWORD_SECRET\n"
                + b"MDIAG v=2 build=tap200_route_20261016 EXTRA_KEYCODE_999\n")
        code, rows, _, _, stdout, _ = self.run_session([(0, data)])
        saved = json.dumps(rows) + stdout
        for secret in secrets + [b"EXTRA_KEYCODE_999"]:
            self.assertNotIn(secret.decode(), saved)
        self.assertEqual(code, 2)
        self.assertEqual(sum(r["event"] == "diagnostic_invalid" for r in rows), 2)
        self.assertEqual(rows[-1]["ignored_non_diagnostic_lines"], 1)

    def test_build_family_is_not_permission_to_log_arbitrary_secrets(self):
        for raw in (b"MDIAG v=2 build=tp2_password_1234 SECRET_TEXT",
                    b"MDIAG status build=tp2_password_1234 t=7 transport=USB route=0 drop=0 queued=0"):
            row = cap.parse_line(raw)
            self.assertEqual(row["event"], "diagnostic_invalid")
            self.assertNotIn("diagnostic_build_token", row)
            self.assertNotIn("password", json.dumps(row))
            self.assertNotIn("SECRET_TEXT", json.dumps(row))
        damaged = cap.parse_line(b"MDIAG status build=tp2_m61016 t=7 BROKEN")
        self.assertEqual(damaged["diagnostic_build_token"], "tp2_m61016")

    def test_oversized_and_incomplete_diagnostic_are_hashed_only(self):
        data = b"noise" * 100 + b"MDIAG SECRET_NEVER_SAVE" + b"z" * 300
        framer = cap.LineFramer(limit=64)
        rows = framer.feed(data[:505]) + framer.feed(data[505:] + b"\n")
        self.assertEqual(rows[0]["reason"], "oversized_diagnostic")
        self.assertEqual(rows[0]["byte_length"], len(data))
        self.assertEqual(rows[0]["sha256"], cap.hashlib.sha256(data).hexdigest())
        self.assertNotIn("SECRET_NEVER_SAVE", json.dumps(rows))
        framer.feed(b"MDIAG truncated SECRET_NEVER_SAVE")
        self.assertEqual(framer.finish()["reason"], "incomplete_diagnostic")

    def test_numeric_bounds_and_unsupported_kind(self):
        cases = [wire(1 << 32, 0, 1), wire(0, 1 << 32, 1), wire(0, 0, 16),
                 wire(0, 0, 1, a=1 << 31), wire(0, 0, 1, b=-(1 << 31) - 1)]
        for line in cases:
            self.assertEqual(cap.parse_line(line)["reason"], "numeric_range_or_kind")

    def test_legacy_v1_kept_but_never_ready_v2(self):
        data = (b"MDIAG v=1 t=7 k=3 a=4 b=1 c=2 d=0 seq=1\n"
                b"MDIAG status build=tp2_mem_20261016 t=7 transport=BLE route=1 drop=0 queued=0\n")
        code, rows, state, _, _, creates = self.run_session([(0, data)])
        self.assertEqual(code, 2)
        self.assertEqual(state, [])
        self.assertEqual(creates, [])
        legacy = [r for r in rows if r.get("legacy")]
        self.assertEqual(len(legacy), 2)
        self.assertTrue(all(r["protocol"] == 1 and not r["current_valid"] for r in legacy))

    def test_bootstrap_burst_five_host_seconds_eighteen_fw_seconds_not_ready(self):
        events = [(n * 0.5, pair(1000 + n * 2000, n * 2)) for n in range(10)]
        code, rows, state, _, _, creates = self.run_session(events)
        self.assertEqual(code, 2)
        self.assertEqual(state, [])
        self.assertEqual(creates, [])
        self.assertEqual(sum(r["event"] == "firmware_event" for r in rows), 20)
        self.assertTrue(any(r.get("reason") == "clock_progress_mismatch_or_cached_burst" for r in rows))

    def test_immediate_cache_and_only_old_stream_stop_bounded(self):
        data = b"".join(pair(1000 + n * 2000, n * 2) for n in range(10))
        code, rows, state, backend, _, _ = self.run_session([(0, data)])
        self.assertEqual((code, state), (2, []))
        self.assertEqual(sum(r["event"] == "firmware_event" for r in rows), 20)
        self.assertEqual(backend.clock.now, 5)

    def test_stable_usb_ready_without_ble_gate_or_q_prompt(self):
        code, rows, state, backend, stdout, creates = self.run_session(stable())
        self.assertEqual(code, 0)
        self.assertEqual([r["current_valid"] for r in state], [True, False])
        self.assertEqual(state[0]["route"], "USB")
        self.assertGreaterEqual(state[0]["host_monotonic_s"], 4)
        self.assertEqual(len(creates), 1)
        self.assertIn("USB test is NOT BLE reproduction", stdout)
        self.assertNotIn("press", stdout.lower())
        self.assertNotIn("ROUTE_USB", stdout)
        self.assertTrue(rows[-1]["valid_before_close"])
        self.assertFalse(rows[-1]["current_valid"])
        self.assertEqual(backend.calls[-1], ("close", 41))

    def test_stable_ble_also_ready(self):
        code, rows, state, _, stdout, _ = self.run_session(stable(1))
        self.assertEqual(code, 0)
        self.assertEqual(state[0]["route"], "BLE")
        self.assertIn("READY_CAPTURE route=BLE", stdout)
        self.assertTrue(rows[-1]["ready_seen"])

    def test_small_heartbeat_jitter_does_not_create_permanent_ready_gate(self):
        events = [(0.0, pair(1000, 0)), (1.999, pair(3000, 2)), (3.998, pair(5000, 4))]
        code, _, state, _, _, _ = self.run_session(events)
        self.assertEqual(code, 0)
        self.assertAlmostEqual(state[0]["host_monotonic_s"], 3.998)

    def test_input_saved_before_ready_with_host_times(self):
        events = [(0, wire(1000, 0, 3, 1, -2, 7, 0)), (1, wire(2000, 1, 13, 7, 2, 1, 1))]
        code, rows, state, _, _, _ = self.run_session(events)
        self.assertEqual(code, 2)
        self.assertEqual(state, [])
        inputs = [r for r in rows if r["event"] == "firmware_event"]
        self.assertEqual([r["kind"] for r in inputs], [3, 13])
        self.assertEqual(inputs[0]["a"], 1)
        self.assertTrue(all("host_utc" in r and "host_monotonic_s" in r for r in inputs))
        self.assertTrue(all(not r["current_valid"] for r in inputs))

    def test_later_usb_heartbeat_revokes_ble_and_is_not_ignored(self):
        events = stable(1) + [(6, pair(7000, 6, 0))]
        code, rows, state, _, _, _ = self.run_session(events, seconds=7)
        self.assertEqual(code, 2)
        self.assertEqual([r["current_valid"] for r in state], [True, False])
        self.assertEqual(state[1]["reason"], "route_changed")
        self.assertTrue(any(r.get("kind") == 11 and r.get("a") == 0 for r in rows))

    def test_endpoint_change_immediately_revokes_ready(self):
        events = stable(1) + [(4.4, wire(5400, 6, 14, 0, -1, 1, 0))]
        code, _, state, _, _, _ = self.run_session(events, seconds=6)
        self.assertEqual(code, 2)
        self.assertEqual(state[1]["reason"], "endpoint_change")
        self.assertAlmostEqual(state[1]["host_monotonic_s"], 4.4)

    def test_ble_profile_change_is_a_route_interval_change(self):
        events = stable(1) + [(6, pair(7000, 6, 1, profile=2))]
        code, rows, state, _, _, _ = self.run_session(events, seconds=7)
        self.assertEqual(code, 2)
        self.assertEqual(state[1]["reason"], "route_changed")
        self.assertTrue(any(r["event"] == "route_change" and r["profile"] == 2 for r in rows))

    def test_crc_after_ready_revokes_and_continues_observation(self):
        events = stable() + [(4.3, wire(5300, 6, 3, corrupt=True)), (4.7, wire(5700, 7, 3))]
        code, rows, state, _, _, _ = self.run_session(events)
        self.assertEqual(code, 2)
        self.assertEqual(state[1]["reason"], "crc_failure")
        self.assertTrue(any(r.get("seq") == 7 and r.get("event") == "firmware_event" for r in rows))
        self.assertFalse(rows[-1]["valid_before_close"])

    def test_recent_crc_fault_requires_new_observation_window(self):
        events = [(0, wire(500, 0, 3, corrupt=True))] + [
            (0.5, pair(1000, 0)), (2.5, pair(3000, 2)), (4.5, pair(5000, 4))]
        code, _, state, _, _, _ = self.run_session(events)
        self.assertEqual(code, 0)
        self.assertGreaterEqual(state[0]["host_monotonic_s"], 4.5)

    def test_uptime_and_sequence_wrap_are_not_reboot(self):
        events = stable(start=cap.MASK32 - 2999, seq=cap.MASK32 - 2)
        code, rows, state, _, _, _ = self.run_session(events)
        self.assertEqual(code, 0)
        wraps = {r["counter"] for r in rows if r["event"] == "counter_wrap"}
        self.assertEqual(wraps, {"uptime", "sequence"})
        self.assertEqual(state[0]["current_valid"], True)
        self.assertFalse(any("restart" in r.get("reason", "") for r in rows))

    def test_restart_regression_revokes_ready(self):
        events = stable() + [(6, pair(100, 0))]
        code, rows, state, _, _, _ = self.run_session(events, seconds=7)
        self.assertEqual(code, 2)
        self.assertEqual(state[1]["reason"], "uptime_regression_restart_or_reorder")
        self.assertTrue(any(r.get("reason") == "sequence_reset_or_reorder" for r in rows))

    def test_duplicate_and_gap_each_revoke(self):
        for data, reason in [(wire(5000, 5, 15, 0, 0, 0, 2), "sequence_duplicate"),
                             (wire(5200, 8, 3), "sequence_gap")]:
            with self.subTest(reason=reason):
                code, rows, state, _, _, _ = self.run_session(stable() + [(4.2, data)])
                self.assertEqual(code, 2)
                self.assertEqual(state[1]["reason"], reason)
                self.assertTrue(any(r.get("reason") == reason for r in rows))

    def test_same_seq_pair_is_invalid_not_v2_protocol(self):
        events = [(n * 2, wire(1000 + n * 2000, n, 11, 0, 0, 1, 0)
                   + wire(1000 + n * 2000, n, 15, 0, 0, 0, 2)) for n in range(3)]
        code, rows, state, _, _, _ = self.run_session(events)
        self.assertEqual((code, state), (2, []))
        self.assertTrue(any(r.get("reason") == "sequence_duplicate" for r in rows))

    def test_missing_heartbeat_quality_and_mismatched_t_not_ready(self):
        events = [(0, wire(1000, 0, 11, 0, 0, 1, 0)),
                  (2, wire(3000, 1, 11, 0, 0, 1, 0) + wire(3001, 2, 15, 0, 0, 0, 2))]
        code, rows, state, _, _, _ = self.run_session(events)
        self.assertEqual((code, state), (2, []))
        reasons = {r.get("reason") for r in rows}
        self.assertIn("heartbeat_pair_missing", reasons)
        self.assertIn("heartbeat_pair_mismatch", reasons)

    def test_historical_drops_and_inactive_window_do_not_permanently_block(self):
        code, rows, state, _, _, _ = self.run_session(stable(drops=7))
        self.assertEqual(code, 0)
        self.assertTrue(state[0]["current_valid"])
        self.assertTrue(any(r.get("reason") == "historical_ring_drops" for r in rows))

    def test_new_ring_drop_queue_and_route_readiness_are_quality_faults(self):
        cases = [(pair(7000, 6, drops=1), "ring_drop_increment"),
                 (pair(7000, 6, queued=4), "firmware_queue_pending"),
                 (pair(7000, 6, mask=2), "selected_route_not_ready")]
        for data, reason in cases:
            with self.subTest(reason=reason):
                code, _, state, _, _, _ = self.run_session(stable() + [(6, data)], seconds=7)
                self.assertEqual(code, 2)
                self.assertEqual(state[1]["reason"], reason)

    def test_silence_revokes_ready_not_forever_valid(self):
        code, rows, state, _, _, _ = self.run_session(stable(), seconds=9)
        self.assertEqual(code, 2)
        self.assertEqual(state[1]["reason"], "heartbeat_stale")
        self.assertFalse(rows[-1]["valid_before_close"])

    def test_no_observed_io_gap_cannot_ready(self):
        sink, stdout, clock = io.StringIO(), io.StringIO(), FakeClock()
        monitor = cap.FreshnessMonitor(cap.Emitter(sink, clock), cap.ReadyChannel(stdout))
        for n in range(3):
            clock.now = n * 2
            for line in pair(1000 + n * 2000, n * 2).splitlines():
                monitor.consume(cap.parse_line(line), clock.now)
        self.assertFalse(monitor.current_valid)
        self.assertIn("no_observed_stream_gap", sink.getvalue())

    def test_lsof_exact_path_only_busy_missing_and_inconclusive(self):
        calls = []

        def run(argv, **kwargs):
            calls.append((argv, kwargs))
            return subprocess.CompletedProcess(argv, 1, b"", b"")

        cap.preflight_readers("/dev/cu.usbmodemEXACT", run=run, isfile=lambda p: True,
                              access=lambda p, mode: True)
        self.assertEqual(calls[0][0], ["/usr/sbin/lsof", "-F", "p", "--", "/dev/cu.usbmodemEXACT"])
        for result, expected in [(subprocess.CompletedProcess([], 0, b"p123\n", b""), "existing_serial_opener"),
                                 (subprocess.CompletedProcess([], 1, b"", b"PRIVATE_ERROR"), "lsof_inconclusive")]:
            with self.assertRaises(cap.CaptureError) as caught:
                cap.preflight_readers("/dev/cu.usbmodemEXACT", run=lambda *a, **k: result,
                                      isfile=lambda p: True, access=lambda p, m: True)
            self.assertTrue(str(caught.exception).startswith(expected))
            self.assertNotIn("PRIVATE_ERROR", str(caught.exception))
        with self.assertRaisesRegex(cap.CaptureError, "lsof_unavailable"):
            cap.preflight_readers("/dev/cu.usbmodemEXACT", run=run, isfile=lambda p: False)
        self.assertEqual(len(calls), 1)

    def test_existing_reader_stops_before_open(self):
        code, rows, _, backend, _, _ = self.run_session(busy=True)
        self.assertEqual(code, 3)
        self.assertEqual([c[0] for c in backend.calls], ["preflight"])
        self.assertFalse(next(r for r in rows if r["event"] == "cleanup")["session_opened"])

    def test_normal_cleanup_restores_dtr_exclusive_close(self):
        code, rows, _, backend, _, _ = self.run_session(stable(), exclusive=True)
        self.assertEqual(code, 0)
        cleanup = next(r for r in rows if r["event"] == "cleanup")
        for name in ("termios_restored", "dtr_low_requested", "exclusive_released", "serial_closed"):
            self.assertTrue(cleanup[name])
        self.assertEqual(cleanup["cleanup_errors"], [])
        self.assertEqual([c[0] for c in backend.calls[-4:]], ["restore", "dtr", "exclusive", "close"])
        self.assertEqual(backend.calls[-3], ("dtr", 41, False))
        self.assertEqual([c for c in backend.calls if c[0] == "dtr"],
                         [("dtr", 41, False), ("dtr", 41, True), ("dtr", 41, False)])

    def test_exception_and_partial_open_cleanup(self):
        for point in ("get_attrs", "configure", "dtr_high", "wait", "read"):
            with self.subTest(point=point):
                code, rows, _, backend, _, _ = self.run_session(stable(), fail=(point,))
                self.assertEqual(code, 3)
                cleanup = next(r for r in rows if r["event"] == "cleanup")
                self.assertTrue(cleanup["dtr_low_requested"])
                self.assertTrue(cleanup["serial_closed"])
                self.assertEqual(cleanup["termios_restored"], point != "get_attrs")
                self.assertEqual(backend.calls[-1], ("close", 41))

    def test_cleanup_failures_do_not_skip_remaining_steps(self):
        code, rows, _, backend, _, _ = self.run_session(
            stable(), exclusive=True, fail=("restore", "dtr_low_cleanup", "exclusive_off", "close"))
        self.assertEqual(code, 3)
        cleanup = next(r for r in rows if r["event"] == "cleanup")
        self.assertEqual(cleanup["cleanup_errors"],
                         ["termios_restore_failed", "dtr_clear_failed", "exclusive_release_failed", "serial_close_failed"])
        self.assertEqual([c[0] for c in backend.calls[-4:]], ["restore", "dtr", "exclusive", "close"])

    def test_sigterm_and_sigint_cleanup_with_handler_restoration(self):
        for number in (signal.SIGTERM, signal.SIGINT):
            with self.subTest(number=number):
                code, rows, _, backend, _, _ = self.run_session(stable(), fire_number=number, exclusive=True)
                self.assertEqual(code, 128 + number)
                cleanup = next(r for r in rows if r["event"] == "cleanup")
                self.assertTrue(cleanup["termios_restored"] and cleanup["dtr_low_requested"]
                                and cleanup["exclusive_released"] and cleanup["serial_closed"])
                self.assertEqual(backend.calls[-1], ("close", 41))
                self.assertEqual(rows[-1]["reason"], "sigterm" if number == signal.SIGTERM else "sigint")

    def test_payload_writes_impossible_and_only_session_ioctls(self):
        self.assertFalse(hasattr(cap.MacSyscalls, "write"))
        self.assertFalse(hasattr(FakeIO, "write"))
        # Read source through Python only for this invariant test, never executing it.
        source = (ROOT / "tools" / "capture_menu_diag.py").read_text(encoding="utf-8")
        tree = ast.parse(source)
        self.assertFalse(any(isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)
                             and isinstance(n.func.value, ast.Name) and n.func.value.id == "os"
                             and n.func.attr == "write" for n in ast.walk(tree)))
        with mock.patch.object(cap.os, "write", side_effect=AssertionError("payload write prohibited")):
            code, _, _, _, _, _ = self.run_session(stable())
            self.assertEqual(code, 0)
        real_boundary = cap.MacSyscalls()  # Imports stdlib only, never opens a device.
        with mock.patch.object(real_boundary.fcntl, "ioctl") as ioctl:
            real_boundary.dtr(41, False)
            real_boundary.dtr(41, True)
            real_boundary.exclusive(41, True)
            real_boundary.exclusive(41, False)
        t = real_boundary.termios
        self.assertEqual([call.args[1] for call in ioctl.call_args_list],
                         [t.TIOCMBIC, t.TIOCMBIS, t.TIOCEXCL, t.TIOCNXCL])

    def test_termios_no_flush_and_no_original_mutation(self):
        backend = cap.MacSyscalls()
        t = backend.termios
        original = [0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, t.B9600, t.B9600, [b"\0"] * 32]
        saved = copy.deepcopy(original)
        configured = backend.configured_attrs(original)
        self.assertEqual(original, saved)
        self.assertEqual(configured[4:6], [t.B115200, t.B115200])
        with mock.patch.object(t, "tcsetattr") as setattrs:
            backend.set_attrs(41, configured)
        self.assertEqual(setattrs.call_args.args, (41, t.TCSANOW, configured))

    def test_output_exclusive_create_never_overwrites(self):
        path = self.artifacts / "exclusive-original.jsonl"
        with cap.private_text_file(path) as file:
            file.write("ORIGINAL\n")
        with self.assertRaises(FileExistsError):
            cap.private_text_file(path)
        self.assertEqual(path.read_text(encoding="utf-8"), "ORIGINAL\n")
        # Some NAS mounts force the owner execute bit; privacy means no group/other access.
        self.assertEqual(path.stat().st_mode & 0o077, 0)
        self.assertEqual(path.stat().st_mode & 0o600, 0o600)

    def test_ready_file_append_revocation_and_exclusive_collision(self):
        path = self.artifacts / "ready-states.jsonl"
        stdout = io.StringIO()
        channel = cap.ReadyChannel(stdout, path)
        base = dict(event="capture_validity", interval_id=1, route="USB", reason="window")
        self.assertFalse(path.exists())
        channel.state(dict(base, current_valid=True))
        channel.state(dict(base, current_valid=False, reason="route_changed"))
        channel.close()
        rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        self.assertEqual([r["current_valid"] for r in rows], [True, False])
        self.assertIn("LATEST", stdout.getvalue())
        with self.assertRaisesRegex(cap.CaptureError, "ready_file_create_failed"):
            cap.ReadyChannel(stdout, path).state(dict(base, current_valid=True))
        self.assertEqual(len(path.read_text(encoding="utf-8").splitlines()), 2)

    def test_ready_file_write_failure_still_announces_revocation_on_stdout(self):
        class FailingReady(MemoryReady):
            fail = False

            def write(self, text):
                if self.fail:
                    raise OSError("READY_WRITE_PRIVATE")
                return super().write(text)

        stdout, memory = io.StringIO(), FailingReady()
        channel = cap.ReadyChannel(stdout, "memory.ready", create=lambda _path: memory)
        row = dict(event="capture_validity", interval_id=1, route="USB", reason="window")
        channel.state(dict(row, current_valid=True))
        memory.fail = True
        with self.assertRaises(OSError):
            channel.state(dict(row, current_valid=False, reason="route_changed"))
        self.assertIn("WAIT_CAPTURE current_valid=false reason=route_changed", stdout.getvalue())
        self.assertNotIn("READY_WRITE_PRIVATE", stdout.getvalue())

    def test_logging_failure_still_cleans_serial(self):
        class BrokenLog(io.StringIO):
            count = 0

            def write(self, text):
                self.count += 1
                if self.count > 4:
                    raise OSError("LOG_FAILURE_PRIVATE")
                return super().write(text)

        clock, signals = FakeClock(), FakeSignals()
        backend = FakeIO(clock, stable())
        with self.assertRaises(OSError):
            cap.run_capture(cap.Config("/dev/cu.usbmodemTEST", 5, True, True),
                            io=backend, clock=clock, output=BrokenLog(), stdout=io.StringIO(), signals=signals)
        self.assertEqual(backend.calls[-1], ("close", 41))
        self.assertIn(("dtr", 41, False), backend.calls[-2:])
        self.assertEqual(signals.handlers, signals.original)

    def test_truncated_final_diagnostic_revokes_and_returns_not_ready(self):
        events = stable() + [(4.5, b"MDIAG v=2 build=tap200_route_20261016 SECRET_TRUNCATED")]
        code, rows, state, _, stdout, _ = self.run_session(events)
        self.assertEqual(code, 2)
        self.assertEqual(state[1]["reason"], "incomplete_diagnostic")
        self.assertFalse(rows[-1]["valid_before_close"])
        self.assertNotIn("SECRET_TRUNCATED", json.dumps(rows) + stdout)

    def test_ready_file_failure_keeps_triggering_input_and_cleanup(self):
        clock, signals, output, stdout = FakeClock(), FakeSignals(), io.StringIO(), io.StringIO()
        backend = FakeIO(clock, stable())

        def denied(_path):
            raise FileExistsError("READY_PRIVATE_FAILURE")

        ready = cap.ReadyChannel(stdout, "existing.ready.jsonl", create=denied)
        code = cap.run_capture(cap.Config("/dev/cu.usbmodemTEST", 5, True, True),
                               io=backend, clock=clock, output=output, stdout=stdout,
                               ready=ready, signals=signals)
        rows = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(code, 3)
        self.assertEqual(sum(r["event"] == "firmware_event" for r in rows), 6)
        self.assertTrue(next(r for r in rows if r["event"] == "cleanup")["serial_closed"])
        self.assertNotIn("READY_PRIVATE_FAILURE", output.getvalue() + stdout.getvalue())
        self.assertEqual(backend.calls[-1], ("close", 41))

    def test_sigterm_or_deadline_during_preflight_never_opens(self):
        for stopping_signal in (True, False):
            with self.subTest(stopping_signal=stopping_signal):
                clock, signals, output = FakeClock(), FakeSignals(), io.StringIO()
                backend = FakeIO(clock)

                def preflight(port):
                    backend.calls.append(("preflight", port))
                    if stopping_signal:
                        signals.fire(signal.SIGTERM)
                    else:
                        clock.now = 5.0

                backend.preflight = preflight
                code = cap.run_capture(cap.Config("/dev/cu.usbmodemTEST", 5, True, True),
                                       io=backend, clock=clock, output=output,
                                       stdout=io.StringIO(), signals=signals)
                self.assertEqual(code, 143 if stopping_signal else 2)
                self.assertEqual([c[0] for c in backend.calls], ["preflight"])
                self.assertEqual(signals.handlers, signals.original)

    def test_log_failure_after_ready_still_revokes_independent_handshake(self):
        class SwitchableLog(io.StringIO):
            broken = False

            def write(self, text):
                if self.broken:
                    raise OSError("LOG_PRIVATE_FAILURE")
                return super().write(text)

        clock, signals, output, stdout = FakeClock(), FakeSignals(), SwitchableLog(), io.StringIO()
        backend = FakeIO(clock, stable() + [(4.5, wire(5500, 6, 3))])
        original_read = backend.read

        def read(fd, size):
            data = original_read(fd, size)
            if clock.now >= 4.5:
                output.broken = True
            return data

        backend.read = read
        memory = MemoryReady()
        ready = cap.ReadyChannel(stdout, "memory.ready.jsonl", create=lambda _path: memory)
        with self.assertRaises(OSError):
            cap.run_capture(cap.Config("/dev/cu.usbmodemTEST", 5, True, True),
                            io=backend, clock=clock, output=output, stdout=stdout,
                            ready=ready, signals=signals)
        states = [json.loads(line) for line in memory.getvalue().splitlines()]
        self.assertEqual([r["current_valid"] for r in states], [True, False])
        self.assertEqual(backend.calls[-1], ("close", 41))
        self.assertEqual(signals.handlers, signals.original)

    def test_authorization_path_duration_and_platform_gates(self):
        for config in (cap.Config("/dev/cu.usbmodemTEST"),
                       cap.Config("/dev/cu.usbmodemTEST", 5, True, False),
                       cap.Config("/dev/ttyUSB0", 5, True, True),
                       cap.Config("/dev/cu.usbmodem../other", 5, True, True),
                       cap.Config("/dev/cu.usbmodemTEST", 4, True, True),
                       cap.Config("/dev/cu.usbmodemTEST", 121, True, True)):
            with self.assertRaises(cap.CaptureError):
                config.validate()
        self.assertEqual(cap.Config("/dev/cu.usbmodemTEST").seconds, 90)
        path = self.artifacts / "linux-must-not-create.jsonl"
        with mock.patch.object(cap.sys, "platform", "linux"), contextlib.redirect_stderr(io.StringIO()), \
                mock.patch.object(cap.os, "open", side_effect=AssertionError("must not open")):
            code = cap.main(["--port", "/dev/cu.usbmodemTEST", "--confirm-debug-cdc",
                             "--allow-session-config", "--output", str(path)])
        self.assertEqual(code, 3)
        self.assertFalse(path.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
