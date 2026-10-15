#!/usr/bin/env python3
"""P9981 TPRAW proto=1 local-only, continuous, read-only serial capture.

Python 3.9+; only the live CLI needs the already-installed ``pyserial`` package.
No per-tap prompts, device writes, port discovery, RPC, reset or firmware actions.
DTR is asserted on ordinary open as the logging-session handshake, then lowered
once at stop. A short read-only drain may receive END, but END is never invented.

Example (use a NEW output directory each time)::
    python3 tools/p9981_continuous_capture.py --port /dev/cu.usbmodem123 \
        --seconds 90 --label natural_taps --output ./capture-natural-001

Use a SEPARATE 5-10 second capture labelled preflight_motion for ordinary sliding.
A coarse label / approximate count is NOT per-tap ground truth or a detection rate.
"""

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
import time
from typing import Any, Dict, Optional


BAUDRATE = 115200
BEGIN_TIMEOUT_SECONDS = 5.0
MAX_CAPTURE_SECONDS = 190.0  # 190 permits observing the firmware's 180 s automatic END.
READ_TIMEOUT_SECONDS = 0.1
DRAIN_SECONDS = 0.35
UINT32_MAX = (1 << 32) - 1
SEQ_HALF_RANGE = 1 << 31
ANSI_RE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07\x1b]*(?:\x07|\x1b\\))")
TOKEN_RE = re.compile(r"(?<![A-Za-z0-9_])(TPRAW_[A-Z_]+)\b")
LEGACY_RE = re.compile(r"\b(?:TPL|TPB|TPBEGIN|TPEND|cached)\b", re.IGNORECASE)
BOOT_RE = re.compile(r"\bBooting\s+(?:Zephyr|ZMK)\b", re.IGNORECASE)
INTEGER_RE = re.compile(r"-?[0-9]+\Z")
HEX_BYTE_RE = re.compile(r"[0-9a-fA-F]{2}\Z")
KNOWN_TOKENS = {"TPRAW_BEGIN", "TPRAW_FRAME", "TPRAW_GPIO", "TPRAW_HEARTBEAT", "TPRAW_END"}


def _integer(fields: Dict[str, str], key: str, unsigned: bool = False,
             uint32: bool = False) -> int:
    value = fields[key]
    if not INTEGER_RE.fullmatch(value):
        raise ValueError("%s is not an integer" % key)
    number = int(value)
    if (unsigned or uint32) and number < 0:
        raise ValueError("%s must be nonnegative" % key)
    if uint32 and number > UINT32_MAX:
        raise ValueError("%s exceeds uint32" % key)
    return number


def _keys(fields: Dict[str, str], required: str) -> None:
    expected = set(required.split())
    missing, extra = expected - fields.keys(), fields.keys() - expected
    if missing or extra:
        raise ValueError("field mismatch: missing=%s extra=%s" % (sorted(missing), sorted(extra)))


def parse_line(line: bytes, complete: bool = True) -> Dict[str, Any]:
    """Parse one line without modifying it; original bytes remain in raw.bin.

    ANSI / Zephyr prefixes are ignored ONLY for parsing. Unknown, old cached
    diagnostics and malformed lines are events, never frame samples. An EOF
    fragment is not promoted to a complete protocol record.
    """
    original = line.decode("utf-8", errors="backslashreplace")
    result = {"source": "firmware", "raw_line": original, "valid": False}
    if not complete:
        result.update(type="TRUNCATED_LINE", parse_error="unterminated line at capture end")
        return result
    clean = ANSI_RE.sub("", original).strip()
    match = TOKEN_RE.search(clean)
    if not match:
        if BOOT_RE.search(clean):
            result.update(type="BOOT", valid=True)
        else:
            result["type"] = "LEGACY_REJECTED" if LEGACY_RE.search(clean) else "UNKNOWN_LINE"
        return result
    token = match.group(1)
    result["type"] = token
    if token not in KNOWN_TOKENS:
        result["parse_error"] = "unknown TPRAW token"
        return result
    fields = {}
    try:
        for part in clean[match.end():].split():
            if "=" not in part:
                raise ValueError("non key=value payload")
            key, value = part.split("=", 1)
            if not key or not value or key in fields:
                raise ValueError("empty or duplicate field")
            fields[key] = value
        if token == "TPRAW_BEGIN":
            _keys(fields, "proto session t_ms readonly synthetic_click gate addr reg len path poll_ms max_ms first_seq")
            fixed = {"proto": "1", "readonly": "1", "synthetic_click": "0", "gate": "motion_low",
                     "addr": "57", "reg": "0a", "len": "7", "path": "pointer_stop_then_burst",
                     "poll_ms": "10", "max_ms": "180000"}
            if any(fields[key].lower() != value for key, value in fixed.items()):
                raise ValueError("not the required readonly TPRAW proto=1 interface")
            parsed = dict(fields)
            for key in ("proto", "readonly", "synthetic_click", "len", "poll_ms", "max_ms"):
                parsed[key] = _integer(fields, key, unsigned=True)
            parsed["first_seq"] = _integer(fields, "first_seq", uint32=True)
        elif token == "TPRAW_FRAME":
            _keys(fields, "session seq t_ms read stage raw obtained dx dy scroll err")
            parsed = dict(fields)
            parsed["seq"] = _integer(fields, "seq", uint32=True)
            parsed["obtained"] = _integer(fields, "obtained", unsigned=True)
            parsed["err"] = _integer(fields, "err")
            parsed["scroll"] = _integer(fields, "scroll")
            if parsed["scroll"] not in (0, 1):
                raise ValueError("scroll must be the existing driver's 0/1 mode")
            if fields["read"] == "ok":
                raw = fields["raw"].split(",")
                if (fields["stage"] != "complete" or parsed["obtained"] != 7 or parsed["err"] != 0
                        or len(raw) != 7 or any(not HEX_BYTE_RE.fullmatch(byte) for byte in raw)):
                    raise ValueError("success requires fresh complete seven-byte read")
                parsed["raw"] = [int(byte, 16) for byte in raw]
                parsed["dx"], parsed["dy"] = _integer(fields, "dx"), _integer(fields, "dy")
                signed = lambda b: b - 256 if b >= 128 else b
                if (parsed["dx"], parsed["dy"]) != (signed(parsed["raw"][3]), signed(parsed["raw"][1])):
                    raise ValueError("dx/dy do not match the existing driver's byte3/byte1 decoding")
            elif fields["read"] == "error":
                if (fields["stage"] not in ("pointer", "burst") or fields["raw"] != "none"
                        or parsed["obtained"] != 0 or fields["dx"] != "na" or fields["dy"] != "na"
                        or parsed["err"] >= 0):
                    raise ValueError("error requires pointer/burst, raw=none, obtained=0 and negative err")
                parsed["raw"], parsed["dx"], parsed["dy"] = None, None, None
            else:
                raise ValueError("unknown read outcome (cached values are not frames)")
        elif token in ("TPRAW_GPIO", "TPRAW_HEARTBEAT"):
            _keys(fields, "session t_ms irq gpio_err last_seq")
            parsed = dict(fields)
            for key in ("irq", "gpio_err"):
                parsed[key] = _integer(fields, key)
            parsed["last_seq"] = _integer(fields, "last_seq", uint32=True)
        else:
            _keys(fields, "session t_ms reason first_seq last_seq attempts ok errors")
            if fields["reason"] not in ("dtr_closed", "usb_offline", "window_limit", "clock_fault"):
                raise ValueError("unknown END reason")
            parsed = dict(fields)
            for key in ("first_seq", "last_seq"):
                parsed[key] = _integer(fields, key, uint32=True)
            for key in ("attempts", "ok", "errors"):
                parsed[key] = _integer(fields, key, unsigned=True)
        parsed["session"] = _integer(fields, "session", uint32=True)
        parsed["t_ms"] = _integer(fields, "t_ms", unsigned=True)
        result.update(parsed)
        result["valid"] = True
    except (KeyError, ValueError) as exc:
        result["parse_error"] = str(exc)
        result["fields_text"] = fields
    return result


class SystemClock:
    def monotonic_ns(self) -> int:
        return time.monotonic_ns()

    def utc_now(self) -> str:
        return datetime.now(timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z")


def _uptime_backwards(now: int, before: int) -> bool:
    return ((now - before) & UINT32_MAX) >= SEQ_HALF_RANGE


def _stamp(clock: Any) -> Dict[str, Any]:
    return {"utc": clock.utc_now(), "monotonic_ns": clock.monotonic_ns()}


def _json_line(handle: Any, value: Dict[str, Any]) -> None:
    handle.write(json.dumps(value, ensure_ascii=False, sort_keys=True) + "\n")


def _write_json(path: Path, value: Dict[str, Any]) -> None:
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")


class CaptureLog:
    """Byte-preserving writer and non-concatenating firmware-session ledger."""

    def __init__(self, output: Path, label: str, clock: Any):
        self.output, self.label, self.clock = output, label, clock
        self.raw = (output / "session.raw.bin").open("xb")
        self.frames = (output / "frames.jsonl").open("x", encoding="utf-8", newline="\n")
        self.events = (output / "events.jsonl").open("x", encoding="utf-8", newline="\n")
        self.buffer = b""
        self.offset = 0
        self.raw_bytes = 0
        self.phase = "capture"
        self.counts = Counter()
        self.segments = []
        self.current = None
        self.partial_reasons = set()
        self.begin_seen = False
        self.sequence_gaps = []
        self.host_start = _stamp(clock)

    def host_event(self, event_type: str, **fields: Any) -> None:
        event = {"type": event_type, "source": "host", "label": self.label, "host": _stamp(self.clock)}
        event.update(fields)
        _json_line(self.events, event)
        self.events.flush()

    def _partial(self, reason: str, segment: Optional[Dict[str, Any]] = None) -> None:
        self.partial_reasons.add(reason)
        target = segment if segment is not None else self.current
        if target is not None and reason not in target["partial_reasons"]:
            target["partial_reasons"].append(reason)

    def _new_segment(self, event: Dict[str, Any], begin: bool) -> Dict[str, Any]:
        if self.segments:
            self._partial("session_or_boot_boundary", self.segments[-1])
        segment = {"segment_id": len(self.segments) + 1, "session": event["session"],
                   "begin": event if begin else None, "end": None, "partial_reasons": [],
                   "host_first": event["host"], "host_last": event["host"],
                   "first_uptime_ms": event["t_ms"], "last_uptime_ms": event["t_ms"],
                   "frame_count": 0, "ok_count": 0, "error_count": 0,
                   "unique_frame_count": 0, "unique_ok_count": 0, "unique_error_count": 0,
                   "duplicate_sequences": 0, "out_of_order_sequences": 0,
                   "first_observed_seq": None, "last_observed_seq": None,
                   "seq_gaps": [], "_seen": set(),
                   "_expected": event["first_seq"] if begin else None}
        self.segments.append(segment)
        self.current = segment
        if not begin:
            self._partial("missing_firmware_begin", segment)
        return segment

    def _frame(self, event: Dict[str, Any], segment: Dict[str, Any]) -> None:
        seq = event["seq"]
        segment["frame_count"] += 1
        outcome = "ok" if event["read"] == "ok" else "error"
        segment[outcome + "_count"] += 1
        if seq in segment["_seen"]:
            segment["duplicate_sequences"] += 1
            self._partial("duplicate_sequence", segment)
        else:
            segment["_seen"].add(seq)
            segment["unique_frame_count"] += 1
            segment["unique_" + outcome + "_count"] += 1
            expected = segment["_expected"]
            delta = 0 if expected is None else (seq - expected) & UINT32_MAX
            if delta and delta < SEQ_HALF_RANGE:
                gap = {"segment_id": segment["segment_id"], "expected_seq": expected,
                       "observed_seq": seq, "missing_count": delta,
                       "position": "leading" if segment["first_observed_seq"] is None else "between"}
                segment["seq_gaps"].append(gap)
                self.sequence_gaps.append(gap)
                self._partial("sequence_gap", segment)
            elif delta:
                segment["out_of_order_sequences"] += 1
                self._partial("out_of_order_sequence", segment)
            if not delta or delta < SEQ_HALF_RANGE:
                segment["_expected"] = (seq + 1) & UINT32_MAX
        if segment["first_observed_seq"] is None:
            segment["first_observed_seq"] = seq
        segment["last_observed_seq"] = seq
        event["segment_id"] = segment["segment_id"]
        event["session_begin_seen"] = segment["begin"] is not None
        _json_line(self.frames, event)

    def _line(self, line: bytes, complete: bool = True) -> None:
        event = parse_line(line, complete)
        event.update(host=_stamp(self.clock), raw_offset=self.offset, raw_length=len(line),
                     label=self.label, received_during=self.phase)
        self.offset += len(line)
        token = event["type"]
        self.counts["lines"] += 1
        if event["valid"]:
            self.counts[token] += 1
        elif token == "TRUNCATED_LINE":
            self.counts["truncated_lines"] += 1
            self._partial("truncated_line")
        elif token in KNOWN_TOKENS:
            self.counts["malformed_lines"] += 1
            self._partial("malformed_protocol_line")
        else:
            self.counts["unparsed_lines"] += 1
            if token == "LEGACY_REJECTED":
                self.counts["legacy_rejected_lines"] += 1
        if token == "BOOT":
            self.counts["boot_events"] += 1
            if self.current is not None:
                self._partial("boot_observed")
                self.current = None
        if event["valid"] and token in KNOWN_TOKENS:
            if token == "TPRAW_BEGIN":
                if self.current is not None:
                    reason = "uptime_regression" if _uptime_backwards(event["t_ms"], self.current["last_uptime_ms"]) else "session_switch_or_repeated_begin"
                    self._partial(reason)
                segment = self._new_segment(event, True)
                self.begin_seen = True
            else:
                segment = self.current
                if segment is not None and (event["session"] != segment["session"]
                                            or _uptime_backwards(event["t_ms"], segment["last_uptime_ms"])
                                            or segment["end"] is not None):
                    reason = "session_changed" if event["session"] != segment["session"] else (
                        "uptime_regression" if _uptime_backwards(event["t_ms"], segment["last_uptime_ms"]) else "event_after_firmware_end")
                    self._partial(reason, segment)
                    segment = None
                if segment is None:
                    segment = self._new_segment(event, False)
            event["segment_id"] = segment["segment_id"]
            segment["host_last"] = event["host"]
            segment["last_uptime_ms"] = event["t_ms"]
            if token == "TPRAW_FRAME":
                self._frame(event, segment)
            elif token == "TPRAW_END":
                segment["end"] = event
                if event["reason"] in ("clock_fault", "usb_offline"):
                    self._partial("firmware_" + event["reason"], segment)
        _json_line(self.events, event)

    def feed(self, data: bytes) -> None:
        if not data:
            return
        self.raw.write(data)
        self.raw_bytes += len(data)
        self.buffer += data
        while b"\n" in self.buffer:
            line, self.buffer = self.buffer.split(b"\n", 1)
            self._line(line + b"\n")
        for handle in (self.raw, self.frames, self.events):
            handle.flush()

    @property
    def active_session_ended(self) -> bool:
        return bool(self.current is not None and self.current["begin"] is not None and self.current["end"] is not None)

    def finish(self, metadata: Dict[str, Any], stop_reason: str,
               exit_code: int, error: Optional[str]) -> Dict[str, Any]:
        if self.buffer:
            self._line(self.buffer, complete=False)
            self.buffer = b""
        if stop_reason in ("sigint", "serial_error", "diagnostic_interface_missing"):
            self._partial(stop_reason)
        if not self.begin_seen:
            self._partial("missing_firmware_begin")
        for segment in self.segments:
            end = segment["end"]
            reconciliation = None
            if end is None:
                self._partial("missing_firmware_end", segment)
            else:
                declared = {"attempts": end["attempts"], "ok": end["ok"], "errors": end["errors"]}
                observed = {"attempts": segment["unique_frame_count"], "ok": segment["unique_ok_count"],
                            "errors": segment["unique_error_count"]}
                delta = {key: declared[key] - observed[key] for key in declared}
                last = segment["last_observed_seq"]
                trailing = end["attempts"] if last is None else (end["last_seq"] - last) & UINT32_MAX
                trailing = trailing if trailing < SEQ_HALF_RANGE else None
                span_ok = (end["attempts"] == 0 or
                           end["last_seq"] == (end["first_seq"] + end["attempts"] - 1) & UINT32_MAX)
                begin_ok = segment["begin"] is None or segment["begin"]["first_seq"] == end["first_seq"]
                internal_ok = end["attempts"] == end["ok"] + end["errors"]
                reconciliation = {"declared": declared, "observed_unique": observed,
                                  "declared_minus_observed": delta,
                                  "counts_match": all(value == 0 for value in delta.values()),
                                  "trailing_missing_count": trailing,
                                  "declared_sequence_span_consistent": span_ok,
                                  "begin_first_seq_matches": begin_ok,
                                  "declared_counts_internally_consistent": internal_ok}
                if not reconciliation["counts_match"]:
                    self._partial("firmware_count_mismatch", segment)
                if not (span_ok and begin_ok and internal_ok):
                    self._partial("firmware_end_inconsistent", segment)
                if trailing:
                    self._partial("trailing_sequence_gap", segment)
            segment["end_reconciliation"] = reconciliation
            segment["firmware_end_seen"] = end is not None
            segment["firmware_uptime_span_ms"] = (segment["last_uptime_ms"] - segment["first_uptime_ms"]) & UINT32_MAX
            segment["host_span_seconds"] = (segment["host_last"]["monotonic_ns"] - segment["host_first"]["monotonic_ns"]) / 1e9
            segment["partial"] = bool(segment["partial_reasons"])
            del segment["_seen"], segment["_expected"]
        host_end = _stamp(self.clock)
        metadata.update(host_start=self.host_start, host_end=host_end,
                        host_duration_seconds=(host_end["monotonic_ns"] - self.host_start["monotonic_ns"]) / 1e9,
                        firmware_time_basis="device uptime t_ms; NOT synchronized to host UTC or monotonic_ns",
                        stop_reason=stop_reason, exit_code=exit_code, error=error,
                        serial_write_bytes=0)
        frame_count = self.counts["TPRAW_FRAME"]
        warnings = []
        if self.begin_seen and frame_count == 0:
            warnings.append("合法 BEGIN 的零帧会话已保留；不等于脚本失败。需另做独立 label=preflight_motion 的普通滑动预检佐证通路。")
        if any(not segment["firmware_end_seen"] for segment in self.segments):
            warnings.append("未收到固件 END；DTR 关闭后的只读 drain 不保证 END 可接收，未伪造。")
        status = "error" if exit_code not in (0, 130) else ("partial" if self.partial_reasons else "complete")
        summary = {"schema_version": 1, "status": status, "partial": bool(self.partial_reasons),
                   "partial_reasons": sorted(self.partial_reasons), "stop_reason": stop_reason,
                   "exit_code": exit_code, "error": error, "label": self.label,
                   "declared_approx_count": metadata["declared_approx_count"], "notes": metadata["notes"],
                   "per_tap_ground_truth": False, "detection_rate_computable": False,
                   "host_start": self.host_start, "host_end": host_end,
                   "host_duration_seconds": metadata["host_duration_seconds"],
                   "firmware_time_basis": metadata["firmware_time_basis"],
                   "raw_bytes": self.raw_bytes, "serial_write_bytes": 0,
                   "firmware_begin_seen": self.begin_seen,
                   "firmware_end_seen": bool(self.counts["TPRAW_END"]),
                   "all_segments_have_firmware_end": bool(self.segments) and all(s["firmware_end_seen"] for s in self.segments),
                   "session_segment_count": len(self.segments), "frame_count": frame_count,
                   "ok_frame_count": sum(s["ok_count"] for s in self.segments),
                   "error_frame_count": sum(s["error_count"] for s in self.segments),
                   "malformed_line_count": self.counts["malformed_lines"],
                   "unparsed_line_count": self.counts["unparsed_lines"],
                   "legacy_rejected_line_count": self.counts["legacy_rejected_lines"],
                   "truncated_line_count": self.counts["truncated_lines"],
                   "boot_event_count": self.counts["boot_events"],
                   "seq_gaps": self.sequence_gaps,
                   "seq_gap_missing_count": sum(gap["missing_count"] for gap in self.sequence_gaps),
                   "event_counts": dict(self.counts), "sessions": self.segments, "warnings": warnings}
        metadata.update(firmware_begin_seen=self.begin_seen, firmware_end_seen=summary["firmware_end_seen"],
                        session_segment_count=len(self.segments), frame_count=frame_count,
                        partial=summary["partial"], partial_reasons=summary["partial_reasons"],
                        summary_file="summary.json", sessions_in_summary=True)
        self.host_event("HOST_END", stop_reason=stop_reason, status=status,
                        exit_code=exit_code, serial_write_bytes=0, host_end=host_end)
        for handle in (self.raw, self.frames, self.events):
            handle.close()
        _write_json(self.output / "metadata.json", metadata)
        _write_json(self.output / "summary.json", summary)
        hashes = {}
        for name in ("session.raw.bin", "frames.jsonl", "events.jsonl", "metadata.json", "summary.json"):
            digest = hashlib.sha256()
            path = self.output / name
            with path.open("rb") as handle:
                for chunk in iter(lambda: handle.read(65536), b""):
                    digest.update(chunk)
            hashes[name] = {"sha256": digest.hexdigest(), "bytes": path.stat().st_size}
        _write_json(self.output / "hashes.json", {"algorithm": "sha256", "files": hashes,
                                               "excluded": ["hashes.json (manifest cannot hash itself)"]})
        return summary


def _load_serial_factory() -> Any:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("缺少运行时依赖 pyserial；未安装任何依赖。请由本地 Agent 在已有环境中确认 pyserial 后重试。") from exc
    if not hasattr(serial, "Serial"):
        raise RuntimeError("当前 serial 模块不是可用的 pyserial（缺少 Serial）；未安装或修改依赖。")
    return serial.Serial


def capture(port: str, output: Any, seconds: float = 90.0, label: str = "natural_taps",
            approx_count: Optional[int] = None, notes: Optional[str] = None,
            serial_factory: Any = None, clock: Any = None) -> Dict[str, Any]:
    """Run one prepared continuous window. Injectable factory/clock are for tests.

    Serial API use is limited to configuring/opening an ordinary 115200 8N1
    session, DTR True/False, read, and close. There are NO serial writes, flushes,
    buffer resets, RTS manipulations, discovery, or firmware/USB operations.
    """
    if not port or not port.strip():
        raise ValueError("--port 必填，不能自动猜测端口")
    if not (0 < seconds <= MAX_CAPTURE_SECONDS):
        raise ValueError("--seconds 必须大于 0 且不超过 190（设为190可等到固件180秒自动END）")
    if not label or not label.strip():
        raise ValueError("--label 不能为空")
    if approx_count is not None and (isinstance(approx_count, bool) or not isinstance(approx_count, int) or approx_count < 0):
        raise ValueError("--approx-count 必须是非负整数，只是粗略元数据")
    factory = serial_factory if serial_factory is not None else _load_serial_factory()
    clock = clock if clock is not None else SystemClock()
    directory = Path(output).expanduser().absolute()
    directory.mkdir(parents=True, exist_ok=False)
    log = CaptureLog(directory, label, clock)
    metadata = {"schema_version": 1, "tool": "p9981_continuous_capture", "protocol": "TPRAW proto=1",
                "port": port, "baudrate": BAUDRATE, "bytesize": 8, "parity": "N", "stopbits": 1,
                "read_timeout_seconds": READ_TIMEOUT_SECONDS, "xonxoff": False, "rtscts": False, "dsrdtr": False,
                "requested_seconds": seconds, "begin_timeout_seconds": BEGIN_TIMEOUT_SECONDS,
                "drain_seconds": DRAIN_SECONDS, "label": label,
                "declared_approx_count": approx_count, "notes": notes,
                "per_tap_ground_truth": False, "detection_rate_computable": False,
                "readonly": True, "serial_write_bytes": 0, "explicit_rts_operations": 0,
                "dtr_policy": "True before ordinary open as log-session handshake; False once on stop; not a reset experiment",
                "os_driver_open_line_behavior_not_controlled": True,
                "local_only": True, "automatic_upload": False, "output_directory": str(directory)}
    log.host_event("HOST_BEGIN", port=port, baudrate=BAUDRATE, requested_seconds=seconds,
                   declared_approx_count=approx_count, notes=notes, serial_write_bytes=0)
    serial_port = None
    opened = False
    stop_reason, exit_code, error = "seconds_elapsed", 0, None
    try:
        serial_port = factory(port=None, baudrate=BAUDRATE, bytesize=8, parity="N", stopbits=1,
                              timeout=READ_TIMEOUT_SECONDS, xonxoff=False, rtscts=False, dsrdtr=False)
        serial_port.dtr = True
        serial_port.port = port
        serial_port.open()
        opened = True
        opened_at = _stamp(clock)
        metadata["host_serial_open"] = opened_at
        log.host_event("HOST_SERIAL_OPEN", dtr=True, handshake="log_session_not_reset")
        capture_deadline = opened_at["monotonic_ns"] + int(seconds * 1e9)
        begin_deadline = opened_at["monotonic_ns"] + int(BEGIN_TIMEOUT_SECONDS * 1e9)
        while True:
            now = clock.monotonic_ns()
            if not log.begin_seen and now >= min(begin_deadline, capture_deadline):
                stop_reason, exit_code = "diagnostic_interface_missing", 2
                error = "启动后未在允许窗口（最长 5 秒）见到合法 TPRAW_BEGIN：诊断接口缺失/协议不符；已停止而非空采 90 秒。"
                break
            if now >= capture_deadline:
                break
            log.feed(serial_port.read(4096))
            if log.active_session_ended:
                stop_reason = "firmware_end"
                break
    except KeyboardInterrupt:
        stop_reason, exit_code = "sigint", 130
    except Exception as exc:
        stop_reason, exit_code, error = "serial_error", 1, "%s: %s" % (type(exc).__name__, exc)
    finally:
        metadata["host_capture_stop"] = _stamp(clock)
        log.host_event("HOST_STOP", stop_reason=stop_reason, error=error)
        if opened:
            try:
                serial_port.dtr = False
                metadata["host_dtr_closed"] = _stamp(clock)
                log.host_event("HOST_DTR_CLOSED", dtr=False, end_reception_not_guaranteed=True)
                log.phase = "drain"
                drain_deadline = clock.monotonic_ns() + int(DRAIN_SECONDS * 1e9)
                while clock.monotonic_ns() < drain_deadline:
                    log.feed(serial_port.read(4096))
            except KeyboardInterrupt:
                stop_reason, exit_code = "sigint", 130
                log._partial("sigint")
            except Exception as exc:
                message = "%s: %s" % (type(exc).__name__, exc)
                metadata["drain_or_dtr_error"] = message
                log.host_event("HOST_DRAIN_OR_DTR_ERROR", error=message)
                log._partial("drain_or_dtr_error")
        if serial_port is not None:
            try:
                serial_port.close()
            except (Exception, KeyboardInterrupt) as exc:
                metadata["close_error"] = "%s: %s" % (type(exc).__name__, exc)
                log._partial("serial_close_error")
    return log.finish(metadata, stop_reason, exit_code, error)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="P9981 连续只读日志采集：准备好后启动一次，无逐次 tap 提示；仅本机保存。")
    parser.add_argument("--port", required=True, help="明确的串口路径/COM 名，必填；不自动枚举。固定 115200 baud。")
    parser.add_argument("--output", required=True, help="独立的新目录；已有目录一律拒绝，绝不覆盖采集。")
    parser.add_argument("--seconds", type=float, default=90.0, help="连续窗口秒数，默认90，最大190；设190可等待固件180秒自动END。")
    parser.add_argument("--label", default="natural_taps", help="整个采集的粗标签，默认 natural_taps；滑动预检另存 preflight_motion。")
    parser.add_argument("--approx-count", type=int, default=None, help="用户声明的约次数（可选），不是逐次真值，不计算检测率。")
    parser.add_argument("--notes", default=None, help="用户声明的误操作/备注（可选），仅元数据。")
    return parser


def main(argv: Optional[Any] = None) -> int:
    args = build_argument_parser().parse_args(argv)
    print("准备好后本次连续采集 %g 秒；无需逐次输入或数每一下。Ctrl-C 可提前停止并保留 partial。" % args.seconds,
          flush=True)
    try:
        summary = capture(args.port, args.output, args.seconds, args.label, args.approx_count, args.notes)
    except (OSError, ValueError, RuntimeError) as exc:
        print("采集未启动：%s" % exc, file=sys.stderr)
        return 2
    print("已本机保存：%s；状态=%s；有效 FRAME=%d；固件 END=%s；serial_write_bytes=0。" %
          (args.output, summary["status"], summary["frame_count"], summary["firmware_end_seen"]))
    if summary["error"]:
        print(summary["error"], file=sys.stderr)
    for warning in summary["warnings"]:
        print("提示：" + warning, file=sys.stderr)
    print("结束后仅需向本地 Agent 说明大概次数和误操作；粗标签/约次数不是逐次真值，不能计算检测率。")
    return summary["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
