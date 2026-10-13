#!/usr/bin/env python3
"""Guided, read-only capture for the temporary P9981 A320 diagnostic firmware."""

from __future__ import annotations

import argparse
import base64
import json
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

try:
    from serial import Serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - exercised by the CLI environment
    raise SystemExit(
        "pyserial is required. Install with: python3 -m pip install -r "
        "scripts/requirements-p9981-capture.txt"
    ) from exc


DIAG_MARKER = b"DIAG "
SAMPLE_SEQUENCE = re.compile(rb"DIAG sample seq=(\d+)")
GPIO_SEQUENCE = re.compile(rb"DIAG gpio .*last_seq=(\d+)")
UPTIME_SEQUENCE = re.compile(rb"uptime_ms=(\d+)")
DEFAULT_VID = 0x1D50
DEFAULT_PID = 0x615E
DEFAULT_PRODUCT = "bbp9981"
SERIAL_BAUD = 115200
READ_TIMEOUT_S = 0.10
MAX_CAPTURE_S = 30 * 60


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def device_metadata(port: Any) -> dict[str, Any]:
    # Deliberately omit USB serial numbers and OS location identifiers.
    return {
        "port": port.device,
        "vid": port.vid,
        "pid": port.pid,
        "product": port.product,
        "manufacturer": port.manufacturer,
        "interface": port.interface,
        "description": port.description,
    }


def matching_ports(vid: int, pid: int, product: str | None) -> list[Any]:
    matches = []
    for port in list_ports.comports():
        if port.vid != vid or port.pid != pid:
            continue
        if product and product.casefold() not in (port.product or "").casefold():
            continue
        matches.append(port)
    return matches


def open_serial_read_only(device: str) -> Any:
    serial_port = Serial(port=None, baudrate=SERIAL_BAUD, timeout=READ_TIMEOUT_S,
                         write_timeout=0, exclusive=True)
    # Set modem-control outputs before opening. The collector never calls write().
    serial_port.dtr = False
    serial_port.rts = False
    serial_port.port = device
    serial_port.open()
    return serial_port


def port_has_diagnostic_marker(port: Any, seconds: int) -> bool:
    """Identify the CDC log endpoint by observed DIAG records without sending bytes."""
    serial_port = open_serial_read_only(port.device)
    try:
        reader = CompleteLineReader(serial_port)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            raw = reader.readline()
            if raw and (b"DIAG gpio " in raw or b"DIAG sample " in raw):
                return True
        return False
    finally:
        serial_port.close()


def write_event(stream: Any, **event: Any) -> None:
    stream.write(json.dumps(event, ensure_ascii=False, separators=(",", ":")) + "\n")
    stream.flush()


def log_firmware_line(stream: Any, raw: bytes, stage: str, last_sequence: int | None,
                      gap_events: list[dict[str, int]], read_call_ms: float) -> tuple[int | None, bool]:
    if DIAG_MARKER not in raw:
        return last_sequence, False

    recv_utc = utc_now()
    recv_monotonic_ns = time.monotonic_ns()
    text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
    event: dict[str, Any] = {
        "type": "firmware_log",
        "host_recv_utc": recv_utc,
        "host_recv_monotonic_ns": recv_monotonic_ns,
        "stage": stage,
        "raw_line": text,
        "raw_b64": base64.b64encode(raw).decode("ascii"),
        "firmware_uptime_ms": None,
        "host_read_call_ms": round(read_call_ms, 3),
    }
    uptime = UPTIME_SEQUENCE.search(raw)
    if uptime:
        event["firmware_uptime_ms"] = int(uptime.group(1))

    sequence = SAMPLE_SEQUENCE.search(raw)
    heartbeat = GPIO_SEQUENCE.search(raw)
    observed_sequence = sequence or heartbeat
    if observed_sequence:
        current = int(observed_sequence.group(1))
        if last_sequence is not None and current > last_sequence + 1:
            missing = current - last_sequence - 1
            gap_events.append({"after_seq": last_sequence, "before_seq": current, "missing": missing})
            write_event(stream, type="sequence_gap", host_recv_utc=recv_utc,
                        host_recv_monotonic_ns=recv_monotonic_ns, stage=stage,
                        after_seq=last_sequence, before_seq=current, missing=missing)
        if last_sequence is None or current > last_sequence:
            last_sequence = current
        event["last_attempt_sequence"] = current
        if sequence:
            event["sample_sequence"] = current
    write_event(stream, **event)
    return last_sequence, True


class CompleteLineReader:
    """Join timeout-split reads and return only complete, newline-terminated records."""

    def __init__(self, serial_port: Any) -> None:
        self.serial_port = serial_port
        self.pending = bytearray()

    def readline(self) -> bytes | None:
        fragment = self.serial_port.readline()
        if not fragment:
            return None
        self.pending.extend(fragment)
        newline = self.pending.find(b"\n")
        if newline < 0:
            return None
        line = bytes(self.pending[:newline + 1])
        del self.pending[:newline + 1]
        return line


def build_stages(round_number: int) -> Iterable[tuple[str, str, int]]:
    prefix = f"第 {round_number} 轮"
    return [
        ("no_touch", f"{prefix}：手离开触控板，保持无手指 5 秒。", 5),
        ("static_contact", f"{prefix}：手指轻放在触控板上并保持静止 5 秒。", 5),
        ("post_lift_idle", f"{prefix}：抬起手指，保持无接触 3 秒。", 3),
        ("light_taps", f"{prefix}：轻拍并抬起共 10 次，每次间隔至少 1 秒；本段 12 秒。", 12),
        ("swipe_hold_lift", f"{prefix}：重复 5 次：滑动一下、停住保持 3 秒、抬手；本段 25 秒。", 25),
        ("long_contact", f"{prefix}：重复 3 次：放指静止 5 秒后抬起，每次之间留约 3 秒；本段 24 秒。", 24),
        ("back_forth_swipes", f"{prefix}：做 3 组往返滑动，动作均匀；本段 15 秒。", 15),
        ("tap_groups", f"{prefix}：做 3 组连续轻拍，每组轻拍 2 次，组间留出停顿；本段 12 秒。", 12),
        ("scroll_mode", f"{prefix}：轻点现有 aA 键进入滚轮模式；滑动约 5 秒、停住 3 秒、抬手，再轻点 aA 退出；本段 15 秒。", 15),
    ]


def capture(args: argparse.Namespace) -> int:
    output = Path(args.output).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        print(f"输出文件已存在，为保护旧日志不覆盖：{output}", file=sys.stderr)
        return 2
    matches = matching_ports(args.vid, args.pid, args.product)
    if not matches:
        print("没有串口符合 USB VID/PID/product 筛选；不打开任何端口。", file=sys.stderr)
        for port in list_ports.comports():
            print(json.dumps(device_metadata(port), ensure_ascii=False), file=sys.stderr)
        print("连接诊断固件后可重试；如本机标识不同，先核对设备信息再传入 --vid/--pid/--product。", file=sys.stderr)
        return 2

    if len(matches) > 8:
        print(f"匹配到 {len(matches)} 个候选串口，超过安全上限 8；不打开任何端口。", file=sys.stderr)
        return 2
    detected = []
    for candidate in matches:
        try:
            if port_has_diagnostic_marker(candidate, args.probe_seconds):
                detected.append(candidate)
        except Exception as exc:
            print(f"候选 {candidate.device} 探测失败：{type(exc).__name__}: {exc}", file=sys.stderr)
    if len(detected) != 1:
        print(f"在 {len(matches)} 个候选中检测到 {len(detected)} 个 DIAG 日志端口；安全起见不开始采集。", file=sys.stderr)
        return 2
    port = detected[0]
    total_seconds = sum(seconds for r in range(1, args.rounds + 1)
                        for _, _, seconds in build_stages(r))
    if total_seconds > MAX_CAPTURE_S:
        print("采集总时长超过安全上限。", file=sys.stderr)
        return 2

    gap_events: list[dict[str, int]] = []
    last_sequence: int | None = None
    diagnostic_lines = 0
    status = "completed"
    stage_name = "preflight"
    capture_start_monotonic = time.monotonic()

    try:
        with output.open("x", encoding="utf-8", newline="\n") as stream:
            write_event(stream, type="capture_start", host_utc=utc_now(),
                        host_monotonic_ns=time.monotonic_ns(),
                        host_clock="Mac wall UTC + monotonic_ns; firmware uptime is a separate, unsynchronized clock",
                        receive_timing_note=("host_recv_* is recorded when a complete serial line is returned; "
                                             "serial read timeout is 100 ms. This bounds local read polling, "
                                             "not Zephyr logger queue or USB buffering, which are not independently measured."),
                        serial=device_metadata(port), baud=SERIAL_BAUD,
                        read_timeout_s=READ_TIMEOUT_S, requested_rounds=args.rounds,
                        planned_duration_s=total_seconds,
                        outbound_serial_bytes=0)
            print(f"已用 USB 特征和 DIAG 标记唯一识别 {port.device}。只读采集，不发送串口数据。")
            serial_port = open_serial_read_only(port.device)
            try:
                # Remove diagnostics accumulated during endpoint discovery; stage timestamps start below.
                serial_port.reset_input_buffer()
                line_reader = CompleteLineReader(serial_port)
                write_event(stream, type="endpoint_selected", host_utc=utc_now(),
                            host_monotonic_ns=time.monotonic_ns(), serial=device_metadata(port),
                            matched_candidates=len(matches), diagnostic_candidates=len(detected),
                            endpoint_identification="USB VID/PID/product plus a DIAG gpio/sample record",
                            preflight_input_buffer_cleared=True)
                print("端口通过 DIAG 标记确认。接下来按终端每段提示操作；每段自动计时，无需逐段按键。")
                stage_name = "capture_lead_in"
                write_event(stream, type="stage_start", host_utc=utc_now(),
                            host_monotonic_ns=time.monotonic_ns(), stage=stage_name,
                            instruction="准备开始：手指离开触控板。", planned_duration_s=3)
                lead_in_deadline = time.monotonic() + 3
                while time.monotonic() < lead_in_deadline:
                    read_started = time.monotonic_ns()
                    raw = line_reader.readline()
                    if raw:
                        last_sequence, is_diagnostic = log_firmware_line(
                            stream, raw, stage_name, last_sequence, gap_events,
                            (time.monotonic_ns() - read_started) / 1_000_000)
                        diagnostic_lines += int(is_diagnostic)
                write_event(stream, type="stage_end", host_utc=utc_now(),
                            host_monotonic_ns=time.monotonic_ns(), stage=stage_name)
                for round_number in range(1, args.rounds + 1):
                    write_event(stream, type="round_start", host_utc=utc_now(),
                                host_monotonic_ns=time.monotonic_ns(), round=round_number)
                    for name, instruction, seconds in build_stages(round_number):
                        stage_name = f"round_{round_number}:{name}"
                        print(f"\n{instruction}")
                        write_event(stream, type="stage_start", host_utc=utc_now(),
                                    host_monotonic_ns=time.monotonic_ns(), stage=stage_name,
                                    instruction=instruction, planned_duration_s=seconds)
                        deadline = time.monotonic() + seconds
                        while time.monotonic() < deadline:
                            read_started = time.monotonic_ns()
                            raw = line_reader.readline()
                            if raw:
                                last_sequence, is_diagnostic = log_firmware_line(
                                    stream, raw, stage_name, last_sequence, gap_events,
                                    (time.monotonic_ns() - read_started) / 1_000_000)
                                diagnostic_lines += int(is_diagnostic)
                        write_event(stream, type="stage_end", host_utc=utc_now(),
                                    host_monotonic_ns=time.monotonic_ns(), stage=stage_name)
                # Drain remaining buffered diagnostics for at most two seconds.
                stage_name = "post_capture_drain"
                drain_deadline = time.monotonic() + 2
                while time.monotonic() < drain_deadline:
                    read_started = time.monotonic_ns()
                    raw = line_reader.readline()
                    if raw:
                        last_sequence, is_diagnostic = log_firmware_line(
                            stream, raw, stage_name, last_sequence, gap_events,
                            (time.monotonic_ns() - read_started) / 1_000_000)
                        diagnostic_lines += int(is_diagnostic)
                if line_reader.pending and DIAG_MARKER in line_reader.pending:
                    write_event(stream, type="partial_firmware_record", host_utc=utc_now(),
                                host_monotonic_ns=time.monotonic_ns(), stage=stage_name,
                                raw_b64=base64.b64encode(line_reader.pending).decode("ascii"),
                                note="Incomplete DIAG line retained at end; excluded from sample interpretation.")
                elapsed = time.monotonic() - capture_start_monotonic
                write_event(stream, type="capture_end", host_utc=utc_now(),
                            host_monotonic_ns=time.monotonic_ns(), status=status,
                        elapsed_s=round(elapsed, 3), last_attempt_sequence=last_sequence,
                            diagnostic_lines=diagnostic_lines, sequence_gaps=gap_events,
                            missing_sequence_count=sum(gap["missing"] for gap in gap_events),
                            partial_diagnostic_record=bool(line_reader.pending and DIAG_MARKER in line_reader.pending),
                        interpretation="Missing attempt sequence values mean no corresponding DIAG record was captured; this may reflect logger/transport loss and is not a sensor sample.")
            finally:
                serial_port.close()
    except KeyboardInterrupt:
        status = "interrupted"
        print("用户中断；串口会关闭，已写日志保留。", file=sys.stderr)
        # The context manager closes the file/port while unwinding.
        return 130
    except FileExistsError:
        print(f"输出文件已存在，为保护旧日志不覆盖：{output}", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"采集失败：{type(exc).__name__}: {exc}", file=sys.stderr)
        return 1

    print(f"采集结束：{output}")
    print(f"诊断行 {diagnostic_lines} 条；序号缺口 {sum(gap['missing'] for gap in gap_events)} 个；文件大小可供复核。")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--diagnostic-fw-active", action="store_true", required=True,
                        help="确认诊断固件已刷入且 Studio 已按诊断配置关闭")
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=DEFAULT_VID,
                        help="匹配的 USB VID（默认 0x1d50）")
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=DEFAULT_PID,
                        help="匹配的 USB PID（默认 0x615e）")
    parser.add_argument("--product", default=DEFAULT_PRODUCT,
                        help="USB product 字符串必须包含该值（默认 bbp9981）")
    parser.add_argument("--rounds", type=int, choices=range(1, 4), default=3,
                        help="采集轮数，默认 3，最大 3")
    parser.add_argument("--probe-seconds", type=int, choices=range(2, 11), default=3,
                        help="每个候选 CDC 端口的只读探测时长，默认 3 秒")
    parser.add_argument("--output", default=None, help="JSONL 输出路径；默认在仓库 captures/ 下新建")
    args = parser.parse_args()
    if args.output is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        args.output = str(Path(__file__).resolve().parents[1] / "captures" / f"p9981-diagnostic-{stamp}.jsonl")
    return capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
