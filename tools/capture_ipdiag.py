#!/usr/bin/env python3
"""Bounded Mac IPDIAG capture; no HID/Bluetooth/serial-payload writes.

Keep capture_menu_diag.py alongside this file. Before real use independently
identify debug CDC, explain temporary termios/DTR changes, obtain user consent.
--confirm-debug-cdc and --allow-session-config are NOT substitutes for consent.
Drops all non-whitelisted text including MDIAG input records before persistence.
No automatic reconnect, route change, key simulation, bond/settings operation.
"""
import argparse
import datetime
import json
import re
import sys
import time
from pathlib import Path

import capture_menu_diag as base

BUILD = 'ipad_enum_diag1_20261025'
# Firmware logger prefix is stripped in memory; it is never retained.
PREFIX = re.compile(rb'(?:\[[0-9:, .]+\]\s*)?<(?:inf|dbg|wrn|err)>\s+\w+:\s*')
RECORD = re.compile(rb'IPDIAG v=1 build=' + BUILD.encode() +
    rb' t=([0-9]{1,10}) seq=([0-9]{1,10}) k=([1-6]) c=([0-9]{1,3}) ep=([0-9]{1,10})'
    rb' a=(-?[0-9]{1,11}) b=(-?[0-9]{1,11}) d=(-?[0-9]{1,11})'
    rb' e=(-?[0-9]{1,11}) f=(-?[0-9]{1,11}) g=(-?[0-9]{1,11})')
IDENTITY = re.compile(rb'IPDIAG_ID v=1 build=' + BUILD.encode() +
    rb' config=([0-9a-f]{40}) zmk=([0-9a-f]{40}) zephyr=([0-9a-f]{40})')
NAMES = ('t_ms', 'seq', 'kind', 'conn_index', 'epoch', 'a', 'b', 'd', 'e', 'f', 'g')


def parse(line):
    text = base.ANSI.sub(b'', line).strip()
    text = PREFIX.sub(b'', text, count=1)
    identity = IDENTITY.fullmatch(text)
    if identity:
        return dict(event='identity', build=BUILD,
                    **dict(zip(('config', 'zmk', 'zephyr'),
                               (s.decode('ascii') for s in identity.groups()))))
    match = RECORD.fullmatch(text)
    if not match:
        return None
    values = tuple(map(int, match.groups()))
    if any(not 0 <= n <= 0xFFFFFFFF for n in (values[0], values[1], values[4])):
        return None
    if values[3] > 255 or any(not -(1 << 31) <= n < (1 << 31) for n in values[5:]):
        return None
    return dict(event='ipdiag', build=BUILD, **dict(zip(NAMES, values)))


class Framer:
    """Bound memory, discard oversize/partial lines without saving a hash."""
    def __init__(self):
        self.buf = bytearray()
        self.overflow = False
        self.omitted = 0

    def feed(self, data):
        result = []
        for byte in data:
            if byte == 10:
                row = None if self.overflow else parse(bytes(self.buf))
                if row is None:
                    self.omitted += 1
                else:
                    result.append(row)
                self.buf.clear()
                self.overflow = False
            elif len(self.buf) < 1024 and not self.overflow:
                self.buf.append(byte)
            else:
                self.buf.clear()
                self.overflow = True
        return result


class IO(base.MacSyscalls):
    def preflight(self, port):
        base.preflight_readers(port)
        base.preflight_readers(port.replace('/dev/cu.', '/dev/tty.', 1))


def capture(config, output, io, clock=time.monotonic):
    config.validate()
    session = base.SerialSession(io, config)
    guard = base.SignalGuard()
    framer = Framer()
    started = clock()
    count = 0
    failed = False

    def emit(row):
        row = dict(row, host_elapsed_s=round(clock() - started, 6),
                   host_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        output.write(json.dumps(row, ensure_ascii=True, separators=(',', ':')) + '\n')
        output.flush()

    guard.install()
    try:
        emit(dict(event='capture_start', seconds=config.seconds,
                  no_serial_payload_writes=True, ordinary_and_mdiag_discarded=True))
        session.open(should_stop=lambda: guard.received is not None)
        emit(dict(event='serial_open', termios_temporary=True, dtr_low_then_high_requested=True))
        print('LISTENING: serial reader opened; this is NOT freshness, BLE or HID acceptance.', flush=True)
        deadline = clock() + config.seconds
        while not guard.received and clock() < deadline:
            if not io.wait_readable(session.fd, min(0.25, max(0, deadline - clock()))):
                continue
            try:
                data = io.read(session.fd, 4096)
            except BlockingIOError:
                continue
            if not data:
                raise OSError('stream ended')
            for row in framer.feed(data):
                emit(row)
                count += 1
                if row['event'] == 'identity' or row.get('kind') in (1, 3):
                    print(json.dumps(row, ensure_ascii=True), flush=True)
    except (OSError, base.CaptureError, base.CaptureStopped):
        # No exception text (could contain device paths or unrelated raw input).
        failed = True
        emit(dict(event='capture_error', reason='session_unavailable_or_interrupted'))
    finally:
        cleanup = session.cleanup()
        emit(cleanup)
        failed = failed or bool(cleanup['cleanup_errors'])
        emit(dict(event='capture_end', accepted_records=count, omitted_lines=framer.omitted,
                  partial_line_omitted=bool(framer.buf or framer.overflow),
                  signal_received=guard.received is not None, failed=failed))
        guard.restore()
    return 3 if failed else (0 if count else 2)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True)
    ap.add_argument('--confirm-debug-cdc', action='store_true')
    ap.add_argument('--allow-session-config', action='store_true')
    ap.add_argument('--seconds', type=int, default=120)
    ap.add_argument('--output', required=True, type=Path)
    args = ap.parse_args()
    config = base.Config(args.port, args.seconds, args.allow_session_config, args.confirm_debug_cdc)
    try:
        config.validate()
        if sys.platform != 'darwin':
            raise base.CaptureError('real_capture_requires_Mac')
        with base.private_text_file(args.output) as output:
            return capture(config, output, IO())
    except (OSError, base.CaptureError):
        print('STOP: configuration/authorization/output unavailable; no overwrite.', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())
