#!/usr/bin/env python3
"""Cross-check actual compiled GATT Report References against parsed HID fields.
Host-only: no identity/advertising/notify worker or Apple/Android acceptance.
"""
from collections import defaultdict
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DESCRIPTOR_SHA = '4317e34b66d9c247b0fb12327516c38c74deea04619db54a05cf190444482a3f'

def parse(raw):
    state = dict(page=0, lo=0, hi=0, size=0, count=0, id=0)
    stack, collections, top, usages = [], [], [], []
    totals, fields = defaultdict(int), defaultdict(list)
    umin = umax = None
    pos = 0
    while pos < len(raw):
        prefix = raw[pos]; pos += 1
        assert prefix != 0xfe, 'long items unsupported'
        n = (0,1,2,4)[prefix & 3]
        assert pos+n <= len(raw), 'truncated item'
        data = raw[pos:pos+n]; pos += n
        unsigned = int.from_bytes(data, 'little')
        signed = int.from_bytes(data, 'little', signed=True)
        kind, tag = (prefix >> 2) & 3, prefix >> 4
        if kind == 1:
            if tag == 0: state['page'] = unsigned
            elif tag == 1: state['lo'] = signed
            elif tag == 2: state['hi'] = signed if state['lo'] < 0 else unsigned
            elif tag == 7: state['size'] = unsigned
            elif tag == 8: state['id'] = unsigned
            elif tag == 9: state['count'] = unsigned
            elif tag == 10: stack.append(state.copy())
            elif tag == 11: state = stack.pop()
        elif kind == 2:
            if tag == 0: usages.append(unsigned)
            elif tag == 1: umin = unsigned
            elif tag == 2: umax = unsigned
        elif kind == 0:
            if tag == 10:
                if not collections: top.append((state['page'], usages[-1], unsigned))
                collections.append(unsigned)
            elif tag == 12:
                collections.pop()
            elif tag in (8,9,11):
                assert collections
                typ = {8:1,9:2,11:3}[tag]
                key = state['id'], typ
                local = usages or (list(range(umin,umax+1)) if umin is not None and umax is not None else [])
                for index in range(state['count']):
                    usage = local[min(index,len(local)-1)] if local else None
                    fields[key].append(dict(offset=totals[key], width=state['size'], flags=unsigned,
                                            page=state['page'], usage=usage, lo=state['lo'], hi=state['hi']))
                    totals[key] += state['size']
            usages = []; umin = umax = None
    assert not collections and not stack
    return dict(totals), dict(fields), top

def validate(raw, refs):
    totals, fields, top = parse(raw)
    assert top == [(1,5,1)], 'not one pure Game Pad application collection'
    assert len(refs) == len(set(refs)), 'duplicate Report Reference'
    assert set(refs) == set(totals), 'Map/GATT report identity mismatch'
    assert totals == {(3,1):80, (5,2):32}, totals
    inputs = fields[3,1]
    live = [f for f in inputs if not f['flags'] & 1]
    assert (live[0]['offset'],live[0]['width'],live[0]['usage'],live[0]['lo'],live[0]['hi']) == (0,4,0x39,0,7)
    assert live[0]['flags'] & 0x40, 'hat must have null state'
    buttons = [f for f in live if f['page']==9]
    assert [f['usage'] for f in buttons] == [18,17,20,19,13,12,11,15,14,8,7,5,4,2,1]
    assert [f['offset'] for f in buttons] == list(range(8,23))
    axes = [f for f in live if f['page']==1 and f['usage'] in (0x30,0x31,0x32,0x35)]
    assert [(f['offset'],f['width'],f['lo'],f['hi']) for f in axes] == [(24,8,1,255),(32,8,1,255),(40,8,1,255),(48,8,1,255)]
    triggers = [f for f in live if f['page']==2]
    assert [(f['offset'],f['width'],f['usage'],f['lo'],f['hi']) for f in triggers] == [(56,8,0xc5,0,255),(64,8,0xc4,0,255)]
    assert [(f['offset'],f['width'],f['lo'],f['hi']) for f in fields[5,2]] == [(0,16,0,65535),(16,16,0,65535)]

def must_reject(raw, refs):
    try:
        validate(raw, refs)
    except AssertionError:
        return
    raise AssertionError('bad Map/GATT contract unexpectedly passed')

def main():
    with tempfile.TemporaryDirectory(prefix='9981-pure-hids-') as directory:
        exe = Path(directory)/'pure_hids'
        subprocess.run([os.environ.get('CC','cc'), '-std=c11','-Wall','-Wextra','-Werror',
                        '-Wno-unused-parameter','-fsanitize=undefined','-fno-sanitize-recover=all',
                        '-I'+str(ROOT/'tests/gamepad_ble_mocks'),
                        str(ROOT/'tests/test_gamepad_pure_hids.c'),'-o',str(exe)], check=True)
        subprocess.run([str(exe)],check=True)
        raw = subprocess.check_output([str(exe),'--descriptor'])
        assert hashlib.sha256(raw).hexdigest() == DESCRIPTOR_SHA, 'pinned descriptor changed'
        refs = [tuple(map(int,line.split())) for line in subprocess.check_output([str(exe),'--refs'],text=True).splitlines()]
        validate(raw, refs)
        sample = subprocess.check_output([str(exe),'--sample'])
        assert sample == bytes([15,4,64,255,128,128,128,255,0,0]), 'independent GATT wire vector'
        _, fields, _ = parse(raw)
        sample_word = int.from_bytes(sample, 'little')
        for field in fields[3,1]:
            actual = (sample_word >> field['offset']) & ((1 << field['width'])-1)
            if field['flags'] & 1:
                assert actual == 0, 'nonzero reserved input'
            else:
                expected = {(1,0x39):15, (1,0x30):255, (1,0x31):128,
                            (1,0x32):128, (1,0x35):128, (2,0xc5):255,
                            (9,1):1, (9,20):1}.get((field['page'],field['usage']),0)
                assert actual == expected, (field,actual,expected)
        must_reject(raw, [(3,1)])  # input-only GATT omits declared Output ID5
        must_reject(raw, refs+[(1,2)])  # stray LED Output absent from Map
        must_reject(raw, [(4,1),(5,2)])  # incorrect Input Report ID
        must_reject(raw, refs+[(3,1)])  # duplicate reference
        must_reject(raw.replace(bytes([0x75,0x04]),bytes([0x75,0x08]),1), refs)
        must_reject(raw.replace(bytes([0x75,0x10]),bytes([0x75,0x08]),1), refs)
        must_reject(raw.replace(bytes([0x09,0x12]),bytes([0x09,0x01]),1), refs)
        print('PASS parsed HID/actual GATT bijection: ID3 input80bit, ID5 output32bit; actual GATT wire vector independently decoded; seven Map/ref counterfactuals rejected')

if __name__ == '__main__':
    main()
