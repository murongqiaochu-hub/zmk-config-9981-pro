#!/usr/bin/env python3
"""Pure host tests; never open a device or configure termios/DTR."""
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import capture_ipdiag as cap

LINE = b'IPDIAG v=1 build=ipad_enum_diag1_20261025 t=123 seq=45 k=3 c=0 ep=1 a=2 b=247 d=7 e=1 f=1 g=1'


class FakeIO:
    def __init__(self, fail=None):
        self.events = []
        self.fail = fail
    def preflight(self, port):
        self.events.append('preflight')
        if self.fail == 'busy':
            raise cap.base.CaptureError('busy')
    def open(self, port):
        self.events.append('open'); return 7
    def is_character(self, fd): return True
    def get_attrs(self, fd): return ['original']
    def set_attrs(self, fd, attrs): self.events.append(('attrs', attrs))
    def configured_attrs(self, attrs): return ['temporary']
    def dtr(self, fd, high):
        self.events.append(('dtr', high))
        if high and self.fail == 'dtr': raise OSError('private detail')
    def wait_readable(self, fd, timeout): return True
    def read(self, fd, size):
        if self.fail == 'read': raise OSError('private detail')
        return LINE + b'\nPRIVATE INPUT\n'
    def close(self, fd): self.events.append('close')


class Tests(unittest.TestCase):
    def test_valid_and_prefix(self):
        row = cap.parse(LINE)
        self.assertEqual((row['kind'], row['a'], row['b']), (3, 2, 247))
        self.assertEqual(row, cap.parse(b'[00:01:00.000,000] <inf> ipdiag: ' + LINE + b'\r'))
    def test_privacy_and_range(self):
        for line in (b'MDIAG v=2 a=secret', LINE + b' password=secret',
                     LINE.replace(b't=123', b't=4294967296'),
                     LINE.replace(b'c=0', b'c=256'), LINE.replace(b'a=2', b'a=2147483648'),
                     LINE.replace(b'k=3', b'k=9'), LINE.replace(b'20261025', b'20261026')):
            self.assertIsNone(cap.parse(line))
    def test_identity(self):
        line = b'IPDIAG_ID v=1 build=ipad_enum_diag1_20261025 config=' + b'a'*40 + b' zmk=' + b'b'*40 + b' zephyr=' + b'c'*40
        self.assertEqual(cap.parse(line)['config'], 'a'*40)
        self.assertIsNone(cap.parse(line + b' secret'))
    def test_framing(self):
        f = cap.Framer()
        self.assertEqual(f.feed(b'x'*5000), [])
        self.assertLessEqual(len(f.buf), 1024)
        rows = f.feed(b'\n' + LINE[:30]) + f.feed(LINE[30:] + b'\nMDIAG secret\n')
        self.assertEqual(len(rows), 1)
        self.assertEqual(f.omitted, 2)
    def test_gates(self):
        for confirm, allow in ((False, False), (True, False), (False, True)):
            device = FakeIO()
            with self.assertRaises(cap.base.CaptureError):
                cap.capture(cap.base.Config('/dev/cu.usbmodemTEST', 5, allow, confirm), io.StringIO(), device)
            self.assertEqual(device.events, [])
    def test_capture_and_cleanup(self):
        for fail in (None, 'busy', 'dtr', 'read'):
            device = FakeIO(fail)
            output = io.StringIO()
            tick = [0]
            def clock():
                tick[0] += 0.1
                return tick[0]
            with patch('sys.stdout', io.StringIO()):
                result = cap.capture(cap.base.Config('/dev/cu.usbmodemTEST', 5, True, True), output, device, clock)
            rows = [json.loads(s) for s in output.getvalue().splitlines()]
            self.assertNotIn('PRIVATE', output.getvalue())
            self.assertNotIn('private detail', output.getvalue())
            self.assertEqual(rows[-1]['event'], 'capture_end')
            if fail == 'busy':
                self.assertNotIn('open', device.events)
            else:
                self.assertEqual(device.events[-1], 'close')
                self.assertIn(('attrs', ['original']), device.events)
                self.assertIn(('dtr', False), device.events)
            self.assertEqual(result, 3 if fail else 0)


if __name__ == '__main__': unittest.main()
