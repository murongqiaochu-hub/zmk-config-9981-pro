import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('capture',ROOT/'tools/p9981_continuous_capture.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
BEGIN='TPRAW_BEGIN proto=1 session=1 t_ms=10 readonly=1 synthetic_click=0 gate=motion_low addr=57 reg=0a len=7 path=pointer_stop_then_burst poll_ms=10 max_ms=180000 first_seq=1\n'
def frame(seq=1,t=20):
    return f'TPRAW_FRAME session=1 seq={seq} t_ms={t} read=ok stage=complete raw=ff,ff,ff,ff,01,24,9a obtained=7 dx=-1 dy=-1 scroll=0 err=0\n'
def end(count=1):
    return f'TPRAW_END session=1 t_ms=100 reason=dtr_closed first_seq=1 last_seq={count} attempts={count} ok={count} errors=0\n'
class Clock:
    def __init__(self):self.ns=0
    def monotonic_ns(self):return self.ns
    def utc_now(self):return '2026-10-15T00:00:00Z'
class Serial:
    def __init__(self,clock,initial,closing):
        self.clock=clock;self.chunks=[initial.encode()];self.closing=closing;self.opened=False;self.writes=0
    @property
    def dtr(self):return getattr(self,'_dtr',False)
    @dtr.setter
    def dtr(self,v):
        if not v and self.opened and self.closing:self.chunks.append(self.closing.encode())
        self._dtr=v
    def open(self):self.opened=True
    def close(self):self.opened=False
    def read(self,n):
        self.clock.ns+=100000000
        return self.chunks.pop(0) if self.chunks else b''
    def write(self,*a):self.writes+=1;raise AssertionError('serial writes forbidden')
class Tests(unittest.TestCase):
    def test_protocol(self):
        self.assertTrue(m.parse_line(('\x1b[32m[00:00] <inf> a320: '+BEGIN).encode())['valid'])
        for s in [frame(),'TPRAW_FRAME session=1 seq=2 t_ms=30 read=error stage=burst raw=none obtained=0 dx=na dy=na scroll=1 err=-5\n']:
            self.assertTrue(m.parse_line(s.encode())['valid'])
        self.assertEqual(m.parse_line(b'TPL seq=1 b=00000000000000')['type'],'LEGACY_REJECTED')
        self.assertFalse(m.parse_line(frame().replace('dx=-1','dx=50').encode())['valid'])
        self.assertFalse(m.parse_line(frame().replace('ff,ff,ff,ff,01,24,9a','00').encode())['valid'])
        self.assertFalse(m.parse_line(frame().encode(),False)['valid'])
        self.assertTrue(m._uptime_backwards(90,100));self.assertFalse(m._uptime_backwards(10,0xffffffff))
    def run_capture(self,initial,closing,seconds=2):
        with tempfile.TemporaryDirectory(prefix='dsh-p9981-test-') as d:
            root=Path(d).resolve();self.assertTrue(root.is_absolute())
            clock=Clock();serial=Serial(clock,initial,closing)
            def factory(**kw):
                self.assertEqual(kw['baudrate'],115200);return serial
            summary=m.capture('fake-readonly',root/'capture',seconds=seconds,serial_factory=factory,clock=clock)
            self.assertEqual(serial.writes,0)
            files={p.name:p.read_bytes() for p in (root/'capture').iterdir()}
            hashes=json.loads(files['hashes.json'])['files']
            for name,record in hashes.items():
                import hashlib
                self.assertEqual(record['sha256'],hashlib.sha256(files[name]).hexdigest())
            return summary,files
    def test_live_readonly_and_raw_bytes(self):
        s,files=self.run_capture(BEGIN+frame(),end())
        self.assertEqual(s['status'],'complete');self.assertEqual(s['frame_count'],1)
        self.assertEqual(files['session.raw.bin'],(BEGIN+frame()+end()).encode())
        self.assertEqual(len(files['frames.jsonl'].splitlines()),1)
    def test_zero_frames_is_not_failed_tap(self):
        s,_=self.run_capture(BEGIN,end(0));self.assertEqual(s['exit_code'],0)
        self.assertEqual(s['status'],'complete');self.assertEqual(s['frame_count'],0)
        self.assertFalse(s['detection_rate_computable'])
    def test_missing_begin_bounded_and_end_missing(self):
        s,_=self.run_capture('DIAG gpio=1 raw=none\n','',8)
        self.assertEqual(s['exit_code'],2);self.assertLess(s['host_duration_seconds'],6)
        s,_=self.run_capture(BEGIN+frame(),'')
        self.assertTrue(s['partial']);self.assertFalse(s['firmware_end_seen'])
    def test_trailing_frame_loss(self):
        s,_=self.run_capture(BEGIN+frame(),end(2))
        self.assertTrue(s['partial']);self.assertEqual(s['sessions'][0]['end_reconciliation']['trailing_missing_count'],1)
    def test_sequence_gap_and_gpio_not_frame(self):
        gpio='TPRAW_GPIO session=1 t_ms=25 irq=1 gpio_err=0 last_seq=1\n'
        s,_=self.run_capture(BEGIN+frame()+gpio+frame(3,40),end(3))
        self.assertEqual(s['frame_count'],2);self.assertEqual(s['seq_gap_missing_count'],1)
    def test_interrupt_preserves_partial(self):
        clock=Clock()
        class InterruptSerial(Serial):
            interrupted=False
            def read(self,n):
                if not self.interrupted:
                    self.interrupted=True
                    raise KeyboardInterrupt()
                return super().read(n)
        serial=InterruptSerial(clock,BEGIN+frame(),end())
        with tempfile.TemporaryDirectory(prefix='dsh-p9981-test-') as d:
            s=m.capture('fake',Path(d)/'partial',serial_factory=lambda **kw:serial,clock=clock)
            self.assertEqual(s['exit_code'],130);self.assertTrue(s['partial'])
            self.assertEqual(serial.writes,0);self.assertTrue((Path(d)/'partial/session.raw.bin').exists())
    def test_actual_c_adapter_protocol(self):
        exe=ROOT/'.host-test-build/test_raw_driver'
        if not exe.exists():self.skipTest('C fixture not built')
        env=dict(os.environ,TPRAW_EMIT_FIXTURE='1')
        lines=subprocess.check_output([str(exe)],env=env).splitlines()
        self.assertGreater(len(lines),8)
        for line in lines:self.assertTrue(m.parse_line(line)['valid'],line)
    def test_refuse_overwrite_and_invalid_duration(self):
        with tempfile.TemporaryDirectory(prefix='dsh-p9981-test-') as d:
            with self.assertRaises(FileExistsError):m.capture('fake',d,serial_factory=lambda **kw:None)
        with self.assertRaises(ValueError):m.capture('fake','unused',seconds=191,serial_factory=lambda **kw:None)
if __name__=='__main__':unittest.main()
