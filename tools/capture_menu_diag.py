#!/usr/bin/env python3
"""Passive, bounded Mac debug-CDC MDIAG capture; Python standard library only.

Example (use an already independently confirmed 9981 *debug CDC* path):
  python3 tools/capture_menu_diag.py --port /dev/cu.usbmodemCONFIRMED \
    --confirm-debug-cdc --allow-session-config --output NEW.jsonl \
    --ready-file NEW.ready.jsonl --seconds 90 > NEW.stdout.txt 2>&1 &
Start the process BEFORE acting. Wait for READY_CAPTURE on stdout or the LATEST
ready-file JSONL state with current_valid=true. File existence alone is NOT a
live handshake: route changes, quality faults, silence and shutdown revoke it.
USB and BLE may both become READY_CAPTURE; a USB test is not BLE reproduction.
READY/current_valid describe the diagnostic capture-channel window, NOT successful
HID delivery. HID ready bits and legal queued counts are observations, not gates;
raw/input diagnostics remain useful even when neither HID route is ready.
No instruction to press Q, preference changes, serial commands, HID access,
device enumeration, process termination, privilege escalation or dependencies.

Real capture is Mac-only and requires explicit temporary DTR/termios permission;
--exclusive additionally authorizes TIOCEXCL on this one CDC session, NOT HID.
Preflight uses ONLY existing /usr/sbin/lsof on the exact selected path. If it is
missing, inconclusive, or finds any opener, stop without opening the port.
Valid diagnostic schema records (including input events) are saved from open,
with safe schema-only raw_diag, host monotonic time and UTC. Ordinary serial
text is never persisted. Malformed diagnostic lines retain only a fixed marker,
length, SHA-256 and, when narrowly recognizable, a bounded diagnostic build token.
A missing build character / bad CRC is a quality fault, NOT proof of a different
installed firmware and NOT a reason to request reflashing. v1 is legacy only.

Freshness is a heuristic, NOT an absolute proof for a one-way read-only stream.
Three paired route/quality heartbeats must span about 4 host seconds (>=3.8), show matching
firmware progression, correct per-record sequence numbers and observed read
idle gaps. Initial cached input is kept but never immediately grants readiness.
Heartbeats 11 then 15 share t, with UNIQUE adjacent seq N, N+1 modulo 2**32.
SIGINT/SIGTERM request bounded shutdown; cleanup attempts restore termios, lower
DTR, release optional CDC exclusivity and close, independently reporting results.
SIGKILL, power loss or an unresponsive OS cannot guarantee cleanup.

Portable no-device tests: python3 -B -m unittest discover -s tests \
  -p 'test_menu_capture.py' -v
"""

import argparse
import copy
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import stat
import struct
import subprocess
import sys
import time
from dataclasses import dataclass


BUILD = "tap200_route_20261016"
MASK32 = 0xFFFFFFFF
HALF32 = 0x80000000
# Firmware integration verified real ZMK v0.3.0 endpoints_types.h (not mocks):
# SHA-256 736852811dbc99b9d4b1523bf574374513c55e8c97c65c15ff9b1e36c16e088c.
ROUTES = {0: "USB", 1: "BLE"}
LSOF = "/usr/sbin/lsof"
MIN_HOST_SPAN = 3.8
HEARTBEAT_STALE = 3.5
PAIR_TIMEOUT = 1.0
MAX_LINE_BYTES = 4096
MAX_HEARTBEAT_QUEUED = 126  # 128-slot ring, snapshot before reserving two heartbeat records.
ANSI = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")
U32 = rb"([0-9]{1,10})"
I32 = rb"(-?[0-9]{1,10})"
V2 = re.compile(
    rb"MDIAG v=2 build=([a-zA-Z0-9_]{1,64}) t=" + U32
    + rb" seq=" + U32 + rb" k=([0-9]{1,3}) a=" + I32
    + rb" b=" + I32 + rb" c=" + I32 + rb" d=" + I32
    + rb" crc=([a-fA-F0-9]{8})"
)
V1 = re.compile(
    rb"MDIAG v=1 t=" + U32 + rb" k=([0-9]{1,3}) a=" + I32
    + rb" b=" + I32 + rb" c=" + I32 + rb" d=" + I32 + rb" seq=" + U32
)
# Restricted diagnostic family, not arbitrary build= text, paths, serials or prose.
BUILD_TOKEN = rb"(?:tap[0-9]{1,3}|tp[0-9])_[a-z0-9_]{0,24}[0-9]{4,8}"
SAFE_BUILD = re.compile(rb"(?:^| )build=(" + BUILD_TOKEN + rb")(?: |$)")
V1_STATUS = re.compile(
    rb"MDIAG status build=(" + BUILD_TOKEN + rb") t=" + U32
    + rb" transport=(USB|BLE) route=" + I32 + rb" drop=" + U32 + rb" queued=" + U32
)
PORT = re.compile(r"/dev/cu\.usbmodem[A-Za-z0-9_-]+\Z")


class CaptureError(Exception):
    """A fixed, non-sensitive failure code (never a system exception message)."""


class CaptureStopped(Exception):
    """Stop requested or observation deadline reached before serial open."""


class SystemClock:
    def monotonic(self):
        return time.monotonic()

    def utc(self):
        return datetime.datetime.now(datetime.timezone.utc).isoformat()


def fnv1a32(words):
    if len(words) != 7:
        raise ValueError("seven words required")
    result = 2166136261
    for byte in struct.pack("<7I", *(value & MASK32 for value in words)):
        result = ((result ^ byte) * 16777619) & MASK32
    return result


def _restricted_build_token(token):
    """Only known identifiers/deletion-damaged forms, not arbitrary build= text."""
    for known in (BUILD.encode("ascii"), b"tp2_mem_20261016"):
        remaining = iter(known)
        if all(byte in remaining for byte in token):
            return True
    return False


def _invalid(raw, reason, *, length=None, digest=None):
    result = {
        "event": "diagnostic_invalid", "marker": "MDIAG_INVALID", "reason": reason,
        "byte_length": len(raw) if length is None else length,
        "sha256": hashlib.sha256(raw).hexdigest() if digest is None else digest,
    }
    # Never retain a malformed line or any unvalidated numeric/input fields.
    at = raw.find(b"MDIAG")
    match = SAFE_BUILD.search(raw[at:]) if at >= 0 else None
    if match and _restricted_build_token(match.group(1)):
        result["diagnostic_build_token"] = match.group(1).decode("ascii")
    return result


def _numeric_ok(t_ms, seq, kind, args, max_kind):
    return (0 <= t_ms <= MASK32 and 0 <= seq <= MASK32 and 1 <= kind <= max_kind
            and all(-(1 << 31) <= value < (1 << 31) for value in args))


def parse_line(raw):
    """Return a strictly schema-filtered diagnostic row, safe evidence, or None."""
    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    if b"MDIAG" not in raw:
        return None
    cleaned = ANSI.sub(b"", raw).rstrip(b"\r\n")
    diag = cleaned[cleaned.find(b"MDIAG"):]
    match = V2.fullmatch(diag)
    if match:
        token, t_ms, seq, kind, a, b, c, d, crc = match.groups()
        t_ms, seq, kind, a, b, c, d = map(int, (t_ms, seq, kind, a, b, c, d))
        if not _numeric_ok(t_ms, seq, kind, (a, b, c, d), 15):
            return _invalid(raw, "numeric_range_or_kind")
        if fnv1a32((t_ms, seq, kind, a, b, c, d)) != int(crc, 16):
            return _invalid(raw, "crc_failure")
        if token.decode("ascii") != BUILD:
            return _invalid(raw, "build_token_anomaly")
        return dict(event="firmware_event", protocol=2, build=BUILD, t_ms=t_ms,
                    seq=seq, kind=kind, a=a, b=b, c=c, d=d,
                    crc=crc.decode("ascii").lower(), raw_diag=diag.decode("ascii"))
    match = V1.fullmatch(diag)
    if match:
        t_ms, kind, a, b, c, d, seq = map(int, match.groups())
        if not _numeric_ok(t_ms, seq, kind, (a, b, c, d), 10):
            return _invalid(raw, "legacy_numeric_range_or_kind")
        return dict(event="legacy_firmware_event", protocol=1, legacy=True,
                    t_ms=t_ms, seq=seq, kind=kind, a=a, b=b, c=c, d=d,
                    raw_diag=diag.decode("ascii"))
    match = V1_STATUS.fullmatch(diag)
    if match:
        token, t_ms, transport, route, drops, queued = match.groups()
        if not _restricted_build_token(token):
            return _invalid(raw, "legacy_build_token_anomaly")
        t_ms, route, drops, queued = map(int, (t_ms, route, drops, queued))
        if (not all(0 <= x <= MASK32 for x in (t_ms, drops, queued))
                or route not in ROUTES):
            return _invalid(raw, "legacy_numeric_range_or_kind")
        return dict(event="legacy_firmware_status", protocol=1, legacy=True,
                    diagnostic_build_token=token.decode("ascii"), t_ms=t_ms,
                    route=route, transport=transport.decode("ascii"),
                    drop=drops, queued=queued, raw_diag=diag.decode("ascii"))
    return _invalid(raw, "malformed_diagnostic")


class LineFramer:
    """Fragmentation-safe and bounded; even oversized lines never leak text."""
    def __init__(self, limit=MAX_LINE_BYTES):
        self.limit = limit
        self._reset()

    def _reset(self):
        self.prefix = bytearray()
        self.tail = b""
        self.size = 0
        self.digest = hashlib.sha256()
        self.diagnostic = False

    def _append(self, part):
        self.diagnostic |= b"MDIAG" in self.tail + part
        self.tail = (self.tail + part)[-4:]
        self.size += len(part)
        self.digest.update(part)
        room = self.limit - len(self.prefix)
        if room > 0:
            self.prefix.extend(part[:room])

    def _finish(self, incomplete=False):
        if not self.diagnostic:
            result = None
        elif self.size > self.limit or incomplete:
            result = _invalid(bytes(self.prefix),
                              "incomplete_diagnostic" if incomplete else "oversized_diagnostic",
                              length=self.size, digest=self.digest.hexdigest())
        else:
            result = parse_line(bytes(self.prefix))
        self._reset()
        return result

    def feed(self, chunk):
        parts = chunk.split(b"\n")
        rows = []
        for part in parts[:-1]:
            self._append(part)
            rows.append(self._finish())
        self._append(parts[-1])
        return rows

    def finish(self):
        return self._finish(incomplete=True) if self.size else None


def private_text_file(path):
    """O_EXCL also refuses existing symlinks; no overwrite, chmod, or mkdir."""
    fd = os.open(os.fspath(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        return os.fdopen(fd, "w", encoding="utf-8")
    except BaseException:
        os.close(fd)
        raise


class Emitter:
    def __init__(self, output, clock):
        self.output = output
        self.clock = clock

    def __call__(self, row):
        row = dict(row, host_monotonic_s=self.clock.monotonic(), host_utc=self.clock.utc())
        self.output.write(json.dumps(row, ensure_ascii=True, separators=(",", ":")) + "\n")
        self.output.flush()
        return row


class ReadyChannel:
    """Create only on first ready; subsequently append revocations, never replace."""
    def __init__(self, stdout, path=None, create=private_text_file):
        self.stdout, self.path, self.create = stdout, path, create
        self.file = None

    def state(self, row):
        if row["current_valid"] and self.path is not None and self.file is None:
            try:
                self.file = self.create(self.path)
            except OSError:
                raise CaptureError("ready_file_create_failed") from None
        if not row["current_valid"]:
            # Announce revocation even when the ready file itself becomes unwritable.
            print("WAIT_CAPTURE current_valid=false reason=" + row["reason"]
                  + "; use LATEST ready-file state", file=self.stdout, flush=True)
        if self.file is not None:
            self.file.write(json.dumps(row, ensure_ascii=True, separators=(",", ":")) + "\n")
            self.file.flush()
        if row["current_valid"]:
            note = "USB test is NOT BLE reproduction" if row["route"] == "USB" else "BLE route observed"
            print("READY_CAPTURE route=" + row["route"] + " current_valid=true interval="
                  + str(row["interval_id"]) + "; " + note
                  + "; scope=diagnostic_capture_channel; hid_output_ready="
                  + json.dumps(row.get("hid_output_ready"))
                  + " ready_bits=" + json.dumps(row.get("ready_bits"))
                  + " queued=" + json.dumps(row.get("queued"))
                  + "; NOT HID delivery confirmation"
                  + "; use LATEST ready-file state, not file existence", file=self.stdout, flush=True)

    def close(self):
        if self.file is not None:
            self.file.close()


class FreshnessMonitor:
    """No IO or clock calls: deterministic, bounded state for a passive stream."""
    def __init__(self, emit, ready):
        self.emit, self.ready = emit, ready
        self.current_valid = False
        self.ready_seen = False
        self.interval_id = 0
        self.route = None
        self.profile = None
        self.last_seq = self.last_t = None
        self.pending = None
        self.candidates = []
        self.last_fault = None
        self.last_heartbeat_host = None
        self.last_drop = None
        self.idle_count = 0
        self.stale_reported = False
        self.hid_status = dict(hid_output_ready=None, ready_bits=None, queued=None)

    def mark(self):
        return {"current_valid": self.current_valid,
                "interval_id": self.interval_id if self.current_valid else None,
                "validity_scope": "diagnostic_capture_channel", **self.hid_status}

    def _state(self, valid, reason):
        if self.current_valid == valid:
            return
        self.current_valid = valid
        if valid:
            self.interval_id += 1
            self.ready_seen = True
        row = dict(event="capture_validity", current_valid=valid,
                   interval_id=self.interval_id, reason=reason,
                   route=ROUTES.get(self.route, "UNKNOWN"), profile=self.profile,
                   freshness="passive_heuristic_not_absolute_proof",
                   validity_scope="diagnostic_capture_channel", **self.hid_status)
        try:
            row = self.emit(row)
        finally:
            # A log failure must not prevent an independent handshake revocation.
            self.ready.state(row)

    def fault(self, reason, now, **evidence):
        self.candidates = []
        self.last_fault = now
        self._state(False, reason)
        self.emit(dict(event="data_quality", reason=reason, **evidence, **self.mark()))

    def observe_idle(self):
        self.idle_count += 1

    def tick(self, now):
        if self.pending is not None and now - self.pending["host"] > PAIR_TIMEOUT:
            self.pending = None
            self.fault("heartbeat_pair_missing", now)
        if (self.last_heartbeat_host is not None
                and now - self.last_heartbeat_host > HEARTBEAT_STALE
                and not self.stale_reported):
            self.stale_reported = True
            self.fault("heartbeat_stale", now)

    def _observe_route(self, route, profile, now):
        if route not in ROUTES or not -1 <= profile <= 255:
            self.fault("route_fields_invalid", now)
            return False
        profile = profile if route == 1 else -1
        if self.route is not None and (route, profile) != (self.route, self.profile):
            self.emit(dict(event="route_change", previous_route=self.route,
                           previous_profile=self.profile, route=route, profile=profile))
            self.fault("route_changed", now)
        self.route, self.profile = route, profile
        return True

    def consume(self, row, now):
        annotations = []
        if row["event"] == "diagnostic_invalid":
            self.pending = None
            self.fault(row["reason"], now)
            return annotations
        if row.get("protocol") != 2:
            self.pending = None
            self.fault("legacy_not_v2", now)
            return annotations
        t_ms, seq, kind = row["t_ms"], row["seq"], row["kind"]
        order_ok = True
        if self.last_t is not None:
            advance = (t_ms - self.last_t) & MASK32
            if advance >= HALF32:
                annotations.append("uptime_regression_restart_or_reorder")
                self.fault("uptime_regression_restart_or_reorder", now)
                order_ok = False
            elif t_ms < self.last_t:
                annotations.append("uptime_wrap")
                self.emit(dict(event="counter_wrap", counter="uptime", previous=self.last_t, value=t_ms))
        if self.last_seq is not None:
            advance = (seq - self.last_seq) & MASK32
            if advance != 1:
                reason = ("sequence_duplicate" if advance == 0 else
                          "sequence_gap" if advance < HALF32 else "sequence_reset_or_reorder")
                annotations.append(reason)
                evidence = {"missing_records": advance - 1} if 1 < advance < HALF32 else {}
                self.fault(reason, now, **evidence)
                order_ok = False
            elif seq < self.last_seq:
                annotations.append("sequence_wrap")
                self.emit(dict(event="counter_wrap", counter="sequence", previous=self.last_seq, value=seq))
        self.last_t, self.last_seq = t_ms, seq
        if not order_ok:
            self.pending = None
        if self.pending is not None and kind != 15:
            self.pending = None
            self.fault("heartbeat_pair_missing", now)
        if kind == 14:
            self.pending = None
            self.fault("endpoint_change", now)
            self._observe_route(row["a"], row["b"], now)
        elif kind == 12 and row["c"] == 0:
            self.fault("route_selection_in_progress", now)
        elif kind == 11:
            if not self._observe_route(row["a"], row["d"], now):
                return annotations
            if row["b"] not in ROUTES or not 0 <= row["c"] <= 3:
                self.fault("route_fields_invalid", now)
                return annotations
            # First route after an ordering fault is retained as a new baseline,
            # never as evidence of continuity across the fault.
            self.pending = {"row": row, "host": now, "idle": self.idle_count}
        elif kind == 15:
            pending, self.pending = self.pending, None
            if (pending is None or not order_ok or pending["row"]["t_ms"] != t_ms
                    or ((pending["row"]["seq"] + 1) & MASK32) != seq):
                self.fault("heartbeat_pair_mismatch", now)
            else:
                self._heartbeat(pending, row, now)
        return annotations

    def _heartbeat(self, pending, quality, now):
        route = pending["row"]
        drops, queued, active, version = (quality[name] for name in ("a", "b", "c", "d"))
        if drops < 0 or not 0 <= queued <= MAX_HEARTBEAT_QUEUED or active not in (0, 1) or version != 2:
            self.fault("quality_fields_invalid", now)
            return
        self.hid_status.update(hid_output_ready=bool(route["c"] & (1 << route["a"])),
                               ready_bits=route["c"], queued=queued)
        if self.last_drop is not None and drops != self.last_drop:
            self.fault("ring_drop_increment" if drops > self.last_drop else "ring_drop_counter_reset",
                       now, previous=self.last_drop, value=drops)
        elif self.last_drop is None and drops:
            self.emit(dict(event="data_quality", reason="historical_ring_drops", value=drops,
                           **self.mark()))
        self.last_drop = drops
        self.last_heartbeat_host = now
        self.stale_reported = False
        # HID availability and a nonempty legal queue do not diagnose CDC freshness.
        warnings = []
        if not self.hid_status["hid_output_ready"]:
            warnings.append("selected_hid_route_not_ready")
        if queued:
            warnings.append("firmware_queue_nonempty")
        self.emit(dict(event="capture_channel_status", warnings=warnings, **self.mark()))
        sample = {"host": pending["host"], "t_ms": route["t_ms"], "idle": pending["idle"]}
        if self.candidates:
            previous = self.candidates[-1]
            host_delta = sample["host"] - previous["host"]
            fw_delta = ((sample["t_ms"] - previous["t_ms"]) & MASK32) / 1000.0
            # A 5-host-second / 18-firmware-second bootstrap burst cannot pass.
            if (not 1.5 <= host_delta <= 3.0 or not 1.5 <= fw_delta <= 3.0
                    or abs(host_delta - fw_delta) > 0.55):
                self.fault("clock_progress_mismatch_or_cached_burst", now)
            elif sample["idle"] <= previous["idle"]:
                self.fault("no_observed_stream_gap", now)
        self.candidates = (self.candidates + [sample])[-3:]
        if len(self.candidates) == 3:
            host_span = sample["host"] - self.candidates[0]["host"]
            fw_span = ((sample["t_ms"] - self.candidates[0]["t_ms"]) & MASK32) / 1000.0
            recent_fault = self.last_fault is not None and now - self.last_fault < MIN_HOST_SPAN
            if (host_span + 1e-9 >= MIN_HOST_SPAN and abs(host_span - fw_span) <= 0.65
                    and not recent_fault):
                self._state(True, "fresh_route_heartbeat_window")

    def finish(self, reason):
        self._state(False, reason)


@dataclass
class Config:
    port: str
    seconds: int = 90
    allow_session_config: bool = False
    confirm_debug_cdc: bool = False
    exclusive: bool = False

    def validate(self):
        if not PORT.fullmatch(self.port or ""):
            raise CaptureError("exact_debug_cdc_path_required")
        if not self.confirm_debug_cdc:
            raise CaptureError("debug_cdc_confirmation_required")
        if not self.allow_session_config:
            raise CaptureError("session_config_authorization_required")
        if not 5 <= self.seconds <= 120:
            raise CaptureError("duration_must_be_5_to_120_seconds")


def preflight_readers(port, *, run=subprocess.run, isfile=os.path.isfile, access=os.access):
    if not isfile(LSOF) or not access(LSOF, os.X_OK):
        raise CaptureError("lsof_unavailable_stop_without_opening")
    try:
        result = run([LSOF, "-F", "p", "--", port], stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, timeout=5, check=False)
    except (OSError, subprocess.SubprocessError):
        raise CaptureError("lsof_inconclusive_stop_without_opening") from None
    # No process arguments, command names, paths or serial numbers are printed.
    if result.returncode == 0:
        raise CaptureError("existing_serial_opener_stop_without_opening")
    if result.returncode != 1 or result.stdout or result.stderr:
        raise CaptureError("lsof_inconclusive_stop_without_opening")


class MacSyscalls:
    """Small injectable syscall boundary. Deliberately contains no write API."""
    def __init__(self):
        import fcntl
        import select
        import termios
        self.fcntl, self.select, self.termios = fcntl, select, termios

    def preflight(self, port):
        preflight_readers(port)

    def open(self, port):
        return os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0))

    def is_character(self, fd):
        return stat.S_ISCHR(os.fstat(fd).st_mode)

    def get_attrs(self, fd):
        return self.termios.tcgetattr(fd)

    def set_attrs(self, fd, attrs):
        self.termios.tcsetattr(fd, self.termios.TCSANOW, attrs)  # Never TCSAFLUSH.

    def configured_attrs(self, original):
        t = self.termios
        attrs = copy.deepcopy(original)
        for name in ("IGNBRK", "BRKINT", "PARMRK", "ISTRIP", "INLCR", "IGNCR", "ICRNL", "IXON", "IXOFF", "IXANY"):
            attrs[0] &= ~getattr(t, name, 0)
        attrs[1] &= ~t.OPOST
        for name in ("CSIZE", "PARENB", "CSTOPB", "CRTSCTS"):
            attrs[2] &= ~getattr(t, name, 0)
        attrs[2] |= t.CS8 | t.CREAD | t.CLOCAL
        for name in ("ECHO", "ECHONL", "ICANON", "ISIG", "IEXTEN"):
            attrs[3] &= ~getattr(t, name, 0)
        attrs[4] = attrs[5] = t.B115200
        attrs[6][t.VMIN] = 0
        attrs[6][t.VTIME] = 0
        return attrs

    def dtr(self, fd, high):
        t = self.termios
        self.fcntl.ioctl(fd, t.TIOCMBIS if high else t.TIOCMBIC, struct.pack("I", t.TIOCM_DTR))

    def exclusive(self, fd, enabled):
        t = self.termios
        request = getattr(t, "TIOCEXCL" if enabled else "TIOCNXCL", None)
        if request is None:
            raise CaptureError("cdc_exclusive_ioctl_unavailable")
        self.fcntl.ioctl(fd, request)

    def wait_readable(self, fd, timeout):
        return bool(self.select.select([fd], [], [], timeout)[0])

    def read(self, fd, size):
        return os.read(fd, size)

    def close(self, fd):
        os.close(fd)


class SerialSession:
    def __init__(self, io, config):
        self.io, self.config = io, config
        self.fd = self.original = None
        self.exclusive_active = False

    def open(self, should_stop=None):
        self.io.preflight(self.config.port)
        if should_stop is not None and should_stop():
            raise CaptureStopped()
        self.fd = self.io.open(self.config.port)
        if not self.io.is_character(self.fd):
            raise CaptureError("selected_path_is_not_character_device")
        if self.config.exclusive:
            self.io.exclusive(self.fd, True)
            self.exclusive_active = True
        self.original = self.io.get_attrs(self.fd)
        self.io.dtr(self.fd, False)
        self.io.set_attrs(self.fd, self.io.configured_attrs(self.original))
        self.io.dtr(self.fd, True)

    def cleanup(self):
        row = dict(event="cleanup", session_opened=self.fd is not None,
                   termios_restore_attempted=False, termios_restored=False,
                   dtr_clear_attempted=False, dtr_low_requested=False,
                   exclusive_release_attempted=False, exclusive_released=False,
                   serial_close_attempted=False, serial_closed=False, cleanup_errors=[])
        if self.fd is None:
            return row
        if self.original is not None:
            row["termios_restore_attempted"] = True
            try:
                self.io.set_attrs(self.fd, self.original)
                row["termios_restored"] = True
            except Exception:
                row["cleanup_errors"].append("termios_restore_failed")
        row["dtr_clear_attempted"] = True
        try:
            self.io.dtr(self.fd, False)
            row["dtr_low_requested"] = True
        except Exception:
            row["cleanup_errors"].append("dtr_clear_failed")
        if self.exclusive_active:
            row["exclusive_release_attempted"] = True
            try:
                self.io.exclusive(self.fd, False)
                row["exclusive_released"] = True
            except Exception:
                row["cleanup_errors"].append("exclusive_release_failed")
        row["serial_close_attempted"] = True
        try:
            self.io.close(self.fd)
            row["serial_closed"] = True
        except Exception:
            row["cleanup_errors"].append("serial_close_failed")
        self.fd = None
        return row


class SignalGuard:
    def __init__(self, api=signal):
        self.api, self.previous = api, {}
        self.received = None

    def handler(self, signum, _frame):
        self.received = signum

    def install(self):
        for signum in (self.api.SIGINT, self.api.SIGTERM):
            self.previous[signum] = self.api.getsignal(signum)
            self.api.signal(signum, self.handler)

    def restore(self):
        for signum, handler in self.previous.items():
            self.api.signal(signum, handler)


def run_capture(config, *, io, clock, output, stdout, ready=None, signals=signal):
    """Inject clock, serial IO, outputs and signal API; no real device in tests."""
    config.validate()
    emit = Emitter(output, clock)
    ready = ready if ready is not None else ReadyChannel(stdout)
    monitor = FreshnessMonitor(emit, ready)
    session = SerialSession(io, config)
    guard = SignalGuard(signals)
    framer = LineFramer()
    count = ignored = 0
    reason, code = "deadline", 2
    end_valid = False
    started = clock.monotonic()
    deadline = started + config.seconds

    def accept(row):
        nonlocal count, ignored
        if row is None:
            ignored += 1
            return
        annotations = []
        try:
            annotations = monitor.consume(row, clock.monotonic())
        finally:
            # Readiness-side IO failure is not permission to discard the input frame.
            emit(dict(row, **monitor.mark(), observations=annotations))
            count += 1

    emit(dict(event="capture_start", seconds=config.seconds, protocol_expected=2,
              expected_build=BUILD, serial_payload_writes=False, filter="MDIAG_schema_only",
              session_config_authorized=True, cdc_exclusive_authorized=config.exclusive,
              freshness="passive_heuristic_not_absolute_proof",
              cleanup_limit="SIGKILL_power_loss_or_unresponsive_OS_not_guaranteed"))
    try:
        guard.install()
        session.open(lambda: guard.received is not None or clock.monotonic() >= deadline)
        emit(dict(event="serial_open", dtr_low_then_high_requested=True,
                  termios_temporary=True, cdc_exclusive=config.exclusive))
        print("OPEN_CAPTURE: save diagnostics immediately; wait for READY_CAPTURE before action. "
              "Passive freshness is heuristic, not proof; no route/preference changes.",
              file=stdout, flush=True)
        while clock.monotonic() < deadline and guard.received is None:
            now = clock.monotonic()
            monitor.tick(now)
            timeout = min(0.2, max(0.0, deadline - now))
            before = clock.monotonic()
            readable = io.wait_readable(session.fd, timeout)
            if guard.received is not None:
                break
            if not readable:
                if clock.monotonic() - before >= 0.09:
                    monitor.observe_idle()
                continue
            if clock.monotonic() >= deadline:
                break
            try:
                chunk = io.read(session.fd, 4096)
            except (BlockingIOError, InterruptedError):
                continue
            if not chunk:
                reason = "serial_eof"
                monitor.fault(reason, clock.monotonic())
                break
            for row in framer.feed(chunk):
                accept(row)
        if guard.received is not None:
            reason = "sigterm" if guard.received == signals.SIGTERM else "sigint"
            code = 128 + int(guard.received)
        else:
            monitor.tick(clock.monotonic())
            code = 0 if monitor.current_valid else 2
    except CaptureStopped:
        reason, code = "deadline", 2
    except CaptureError as exc:
        # CaptureError codes are created only by this module, never from OS text.
        reason = str(exc)
        emit(dict(event="capture_error", reason=reason))
        code = 3
    except KeyboardInterrupt:
        reason, code = "sigint", 130
    except Exception:
        reason, code = "capture_io_error", 3
        emit(dict(event="capture_error", reason=reason))
    finally:
        # Even log/ready-file failures must not prevent serial cleanup.
        cleanup = session.cleanup()
        if guard.received is not None:
            reason = "sigterm" if guard.received == signals.SIGTERM else "sigint"
            code = 128 + int(guard.received)
        try:
            try:
                incomplete = framer.finish()
                if incomplete is not None:
                    accept(incomplete)
                end_valid = monitor.current_valid
                if code == 0 and not end_valid:
                    code = 2
            finally:
                try:
                    monitor.finish(reason)
                finally:
                    emit(cleanup)
            if cleanup["cleanup_errors"] and code < 128:
                code = 3
            emit(dict(event="capture_end", reason=reason, ready_seen=monitor.ready_seen,
                      valid_before_close=end_valid, current_valid=False, records=count,
                      ignored_non_diagnostic_lines=ignored,
                      elapsed_s=clock.monotonic() - started, exit_code=code))
            print("FINISHED_CAPTURE reason=" + reason + " ready_seen="
                  + str(monitor.ready_seen).lower() + " current_valid=false; diagnostics retained",
                  file=stdout, flush=True)
        finally:
            try:
                ready.close()
            finally:
                guard.restore()
    return code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True, help="Exact independently confirmed /dev/cu.usbmodem... debug CDC; never enumerated")
    parser.add_argument("--confirm-debug-cdc", action="store_true", help="Confirm this exact path is 9981 debug CDC, not HID/Studio")
    parser.add_argument("--allow-session-config", action="store_true", help="Authorize temporary termios and DTR low/high; cleanup explicitly lowers DTR")
    parser.add_argument("--exclusive", action="store_true", help="Additionally authorize temporary TIOCEXCL on ONLY this CDC session, never HID")
    parser.add_argument("--output", type=Path, required=True, help="New private JSONL file, exclusive create, no overwrite")
    parser.add_argument("--ready-file", type=Path, help="New append-only JSONL handshake; read LATEST state, not just existence")
    parser.add_argument("--seconds", type=int, default=90, help="Bounded observation, 5..120 seconds (default 90)")
    args = parser.parse_args(argv)
    config = Config(args.port, args.seconds, args.allow_session_config, args.confirm_debug_cdc, args.exclusive)
    try:
        config.validate()
        if sys.platform != "darwin":
            raise CaptureError("real_capture_is_Mac_only_use_portable_unit_tests")
        if args.ready_file and os.path.abspath(args.ready_file) == os.path.abspath(args.output):
            raise CaptureError("output_and_ready_file_must_differ")
        if os.path.lexists(args.output) or (args.ready_file and os.path.lexists(args.ready_file)):
            raise CaptureError("output_or_ready_file_exists_no_overwrite")
        with private_text_file(args.output) as output:
            return run_capture(config, io=MacSyscalls(), clock=SystemClock(), output=output,
                               stdout=sys.stdout, ready=ReadyChannel(sys.stdout, args.ready_file))
    except CaptureError as exc:
        print("STOP_CAPTURE: " + str(exc), file=sys.stderr)
    except OSError:
        print("STOP_CAPTURE: output_or_session_unavailable_no_overwrite", file=sys.stderr)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
