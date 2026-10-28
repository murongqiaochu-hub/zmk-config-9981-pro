#!/usr/bin/env python3
"""Isolated composite Map/codec oracle; no firmware or GATT service is built.

Compile the host-only C slice/codec model. Independently parse the complete
stock+gamepad HID byte stream, including global state that survives collection
boundaries. Obtain *stock* Report References from the unmodified pinned HOG
fixture; the two new gamepad references remain a model, not registered GATT.
"""
from collections import defaultdict
import hashlib
import os
from pathlib import Path
import resource
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STOCK_SHA = '5c7e4106019e5baf9027c8a24e334c58f5e09c65cd3ff46b338c551c61da4ee3'
PURE_SHA = '4317e34b66d9c247b0fb12327516c38c74deea04619db54a05cf190444482a3f'
EXPECTED_BITS = {(1,1):64, (1,2):8, (2,1):96, (3,1):72, (4,1):80, (5,2):32}
EXPECTED_TOP = [(1,6,1), (12,1,1), (1,2,1), (1,5,1)]


def parse(raw):
    """Parse short HID items in one stream; preserve ALL tracked global state.

    This is an independent test oracle, not a complete HID implementation;
    deliberately reject unsupported long/reserved items rather than guessing.
    """
    state = dict(page=0, lo=0, hi=0, phys_lo=0, phys_hi=0, unit_exp=0,
                 unit=0, size=0, count=0, report_id=0)
    states, collections, top = [], [], []
    usages, umin, umax = [], None, None
    totals, fields, owners = defaultdict(int), defaultdict(list), defaultdict(set)
    pos = 0
    while pos < len(raw):
        prefix = raw[pos]; pos += 1
        assert prefix != 0xfe, 'unsupported HID long item'
        length = (0, 1, 2, 4)[prefix & 3]
        assert pos + length <= len(raw), 'truncated HID item'
        data = raw[pos:pos + length]; pos += length
        uval = int.from_bytes(data, 'little') if length else 0
        sval = int.from_bytes(data, 'little', signed=True) if length else 0
        kind, tag = (prefix >> 2) & 3, prefix >> 4
        if kind == 1:  # globals persist across End Collection
            if tag == 0: state['page'] = uval
            elif tag == 1: state['lo'] = sval
            elif tag == 2: state['hi'] = sval if state['lo'] < 0 else uval
            elif tag == 3: state['phys_lo'] = sval
            elif tag == 4: state['phys_hi'] = sval if state['phys_lo'] < 0 else uval
            elif tag == 5:
                # HID short unit exponent is a signed four-bit nibble.
                state['unit_exp'] = (uval - 16 if uval & 8 else uval) if length == 1 and uval <= 15 else sval
            elif tag == 6: state['unit'] = uval
            elif tag == 7: state['size'] = uval
            elif tag == 8:
                assert 0 < uval < 256, 'invalid Report ID'
                state['report_id'] = uval
            elif tag == 9: state['count'] = uval
            elif tag == 10: states.append(state.copy())
            elif tag == 11:
                assert states, 'global POP without PUSH'
                state = states.pop()
            else: raise AssertionError(f'unsupported global item {tag}')
        elif kind == 2:
            # A four-byte Extended Usage embeds a different Usage Page. This
            # oracle does not decode it; rejecting it is safer than silently
            # interpreting it under the current global page.
            assert length != 4, 'extended HID usage unsupported by host oracle'
            if tag == 0: usages.append(uval)
            elif tag == 1:
                assert umin is None, 'repeated usage minimum unsupported'
                umin = uval
            elif tag == 2:
                assert umax is None, 'repeated usage maximum unsupported'
                umax = uval
            else: raise AssertionError(f'unsupported local item {tag}')
        elif kind == 0:
            if tag == 10:
                if not collections:
                    assert usages, 'top-level collection lacks usage'
                    top.append((state['page'], usages[-1], uval))
                collections.append(len(top) - 1)
            elif tag == 12:
                assert collections, 'unbalanced End Collection'
                collections.pop()
            elif tag in (8, 9, 11):
                assert collections and state['report_id'] and state['size'] and state['count']
                typ = {8:1, 9:2, 11:3}[tag]
                key = (state['report_id'], typ)
                assert not (usages and (umin is not None or umax is not None)), 'mixed Usage/range unsupported'
                assert (umin is None) == (umax is None), 'incomplete Usage range'
                if usages:
                    local = usages
                elif umin is not None:
                    assert umin <= umax and umax - umin <= 65535
                    local = list(range(umin, umax + 1))
                else:
                    local = []
                # Array items describe a choice from the *entire* Usage set,
                # not one Usage per field. Preserve that entire domain even
                # when Report Count is smaller than the declared range.
                domain = (tuple(usages), umin, umax)
                attrs = {k:v for k,v in state.items() if k != 'report_id'}
                for i in range(state['count']):
                    usage = local[min(i, len(local) - 1)] if local and (uval & 2) else None
                    fields[key].append(dict(offset=totals[key], flags=uval, usage=usage,
                                            usage_domain=domain, **attrs))
                    totals[key] += state['size']
                owners[key].add(collections[-1])
            else: raise AssertionError(f'unsupported main item {tag}')
            usages, umin, umax = [], None, None
        else:
            raise AssertionError('reserved HID item')
    assert not collections and not states, 'unbalanced HID state'
    return dict(totals), dict(fields), top, dict(owners)


def validate(raw, stock, pure, refs):
    assert len(raw) == 380 and raw[:196] == stock, 'stock prefix/length changed'
    totals, fields, top, owners = parse(raw)
    stock_totals, stock_fields, stock_top, _ = parse(stock)
    pure_totals, pure_fields, pure_top, _ = parse(pure)
    assert top == EXPECTED_TOP and stock_top == EXPECTED_TOP[:3] and pure_top == [EXPECTED_TOP[3]]
    assert totals == EXPECTED_BITS, totals
    assert all(len(v) == 1 for v in owners.values()), 'same input/output Report ID shared by applications'
    assert len(refs) == len(set(refs)) and set(refs) == set(totals), 'Report Reference/type mismatch'
    assert (1,1) in refs and (1,2) in refs, 'keyboard input and LED output legitimately share ID1'
    for key in stock_totals:
        assert fields[key] == stock_fields[key], ('stock semantics changed', key)
    assert pure_totals == {(3,1):80, (5,2):32}
    assert fields[4,1] == pure_fields[3,1], 'gamepad Input semantics differ from pure map'
    assert fields[5,2] == pure_fields[5,2], 'gamepad Output semantics differ from pure map'
    hat = next(f for f in fields[4,1] if f['page'] == 1 and f['usage'] == 0x39 and not f['flags'] & 1)
    assert (hat['offset'],hat['size'],hat['lo'],hat['hi'],hat['phys_lo'],hat['phys_hi'],hat['unit']) == (0,4,0,7,0,315,0x14)
    assert hat['flags'] & 0x40, 'hat null state lost'
    return fields


def must_reject(raw, stock, pure, refs, label):
    try:
        validate(raw, stock, pure, refs)
    except (AssertionError, IndexError):
        return
    raise AssertionError('negative unexpectedly accepted: ' + label)


def sample_validate(sample, fields):
    assert len(sample) == 10, 'BLE gamepad report must be a 10-byte body, no ID prefix'
    word = int.from_bytes(sample, 'little')
    for field in fields[4,1]:
        got = (word >> field['offset']) & ((1 << field['size']) - 1)
        if field['flags'] & 1:
            assert got == 0, 'nonzero reserved bits'
        else:
            want = {(1,0x39):15, (1,0x30):255, (1,0x31):128,
                    (1,0x32):128, (1,0x35):128, (2,0xc5):255,
                    (9,1):1, (9,20):1}.get((field['page'],field['usage']),0)
            assert got == want, (field, got, want)


def main():
    if not __debug__:
        raise RuntimeError('the oracle must not run under Python -O')
    stock = bytes.fromhex((ROOT/'tests/hid_map_fixtures/stock_196.hex').read_text())
    assert len(stock) == 196 and hashlib.sha256(stock).hexdigest() == STOCK_SHA
    cc = shlex.split(os.environ.get('CC', 'cc'))
    assert cc, 'empty CC'
    common = [*cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
              '-fno-omit-frame-pointer','-fno-pie','-no-pie']
    with tempfile.TemporaryDirectory(prefix='9981-composite-map-') as directory:
        temp = Path(directory)
        assert temp.resolve().parent == Path(tempfile.gettempdir()).resolve()
        assert temp.name.startswith('9981-composite-map-') and not temp.is_symlink()
        model = ROOT/'tests/test_composite_map_host.c'
        for sanitizer in ('undefined','address'):
            exe = temp/('map-' + sanitizer)
            flags = ['-fsanitize=' + sanitizer]
            if sanitizer == 'undefined': flags += ['-fno-sanitize-recover=all']
            subprocess.run([*common,*flags,'-DBBP_COMPOSITE_HOST_ONLY=1',str(model),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],input=stock,check=True)
        raw = subprocess.check_output([str(exe),'--map'],input=stock)
        pure = subprocess.check_output([str(exe),'--pure'],input=stock)
        sample = subprocess.check_output([str(exe),'--sample'],input=stock)
        model_refs = [tuple(map(int,x.split())) for x in subprocess.check_output([str(exe),'--refs'],input=stock).decode().splitlines()]
        assert len(pure) == 182 and hashlib.sha256(pure).hexdigest() == PURE_SHA
        assert model_refs == [(4,1),(5,2)]
        expected = stock + pure[:7] + b'\x04\x15\x00' + pure[8:]
        assert raw == expected, 'C map differs from independent transformation'

        # Probe the stock references from the unmodified pinned hog.c fixture.
        # The old extension's final ID4/9-byte input is deliberately excluded:
        # the candidate's new refs above are only model constants, not GATT.
        probe = temp/'stock_refs.c'
        fixture = str(ROOT/'tests/test_gamepad_ble.c').replace('\\','\\\\').replace('"','\\"')
        probe.write_text('#define main bbp_old_test_main\n#include "' + fixture + '"\n#undef main\n'
                         'int main(void) { for (size_t i=0;i<hog_svc.attr_count;++i) { '
                         'const struct bt_gatt_attr *a=&hog_svc.attrs[i]; '
                         'if(a->uuid->val==0x2908) { unsigned char r[2]; '
                         'assert(a->read(NULL,a,r,2,0)==2); printf("%u %u %zu\\n",r[0],r[1],i); } } return 0; }\n')
        stock_exe = temp/'stock-refs'
        subprocess.run([*common,'-fsanitize=undefined','-fno-sanitize-recover=all',
                        '-I'+str(ROOT/'tests/gamepad_ble_mocks'),str(probe),'-o',str(stock_exe)],check=True)
        actual = [tuple(map(int,line.split())) for line in subprocess.check_output([str(stock_exe)],text=True).splitlines()]
        assert actual == [(1,1,8),(2,1,12),(3,1,16),(1,2,19),(4,1,25)], actual
        stock_refs = [(rid,typ) for rid,typ,index in actual if index < 22]
        refs = stock_refs + model_refs
        fields = validate(raw,stock,pure,refs)
        sample_validate(sample,fields)
        assert sample == bytes([15,4,64,255,128,128,128,255,0,0])

        # Array usages are a whole valid domain, not the first N entries. A
        # previous oracle wrongly treated this shrink as semantically equal.
        array_pattern = bytes.fromhex('190029ff75089506')
        assert stock.count(array_pattern) == 1
        narrowed = stock.replace(array_pattern, bytes.fromhex('1900290575089506'), 1)
        _, original_fields, _, _ = parse(stock)
        _, narrowed_fields, _, _ = parse(narrowed)
        assert original_fields[1,1] != narrowed_fields[1,1], 'array Usage range shrink was missed'
        assert any(f['usage_domain'] == ((),0,255) for f in original_fields[1,1])
        assert any(f['usage_domain'] == ((),0,5) for f in narrowed_fields[1,1])
        extended = bytes.fromhex('05010905a1018501750895010b010009008102c0')
        try: parse(extended)
        except AssertionError as error:
            assert 'extended HID usage unsupported' in str(error)
        else: raise AssertionError('32-bit Extended Usage was silently misparsed')

        # Both negatives retain the correct total length, so rejection is due
        # to semantics/ID collision rather than the generic size guard.
        no_min = raw[:204] + b'\x55\x00' + raw[206:]  # unit exponent 0, NOT logical minimum 0
        _, id_fields, _, _ = parse(no_min)
        bad_hat = next(f for f in id_fields[4,1] if f['page']==1 and f['usage']==0x39 and not f['flags'] & 1)
        assert len(no_min) == len(raw) and bad_hat['lo'] == -32768, bad_hat
        must_reject(no_min,stock,pure,refs,'missing global logical minimum reset')
        collision = raw[:203] + b'\x03' + raw[204:]
        collision_totals, _, _, collision_owners = parse(collision)
        assert len(collision) == len(raw) and collision_totals[3,1] == 152
        assert len(collision_owners[3,1]) == 2
        must_reject(collision,stock,pure,refs,'mouse/gamepad input ID3 collision')
        must_reject(raw[:-1],stock,pure,refs,'truncated descriptor')
        must_reject(raw,stock,pure,refs[:-1],'missing gamepad output ref')
        must_reject(raw,stock,pure,refs+[(4,1)],'duplicate input ref')
        must_reject(raw,stock,pure,[(1,1),(1,1),(2,1),(3,1),(4,1),(5,2)],'missing LED ref')
        must_reject(raw,stock,pure,stock_refs+[(3,1),(5,2)],'wrong gamepad input ref')
        must_reject(raw,stock,pure,stock_refs+[(4,1),(5,1)],'wrong output report type')
        for bad in (sample[:-1], b'\x04'+sample, sample[:9]+b'\x80'):
            try: sample_validate(bad,fields)
            except AssertionError: pass
            else: raise AssertionError('bad gamepad body accepted')

        # The host-only gate and the C oracle must reject real code mutants;
        # merely checking altered byte arrays in Python is insufficient.
        for guard in ([], ['-DBBP_COMPOSITE_HOST_ONLY=0']):
            missing_gate = subprocess.run([*common,*guard,'-fsyntax-only',str(model)],capture_output=True,text=True)
            assert missing_gate.returncode != 0 and 'requires BBP_COMPOSITE_HOST_ONLY=1' in missing_gate.stderr
        no_assert = subprocess.run([*common,'-DBBP_COMPOSITE_HOST_ONLY=1','-DNDEBUG','-fsyntax-only',str(model)],capture_output=True,text=True)
        assert no_assert.returncode != 0 and 'requires enabled assertions' in no_assert.stderr
        header = ROOT/'experiments/gamepad_protocol/composite_map_host.h'
        header_text, test_text = header.read_text(), model.read_text()
        include = '#include "../experiments/gamepad_protocol/composite_map_host.h"'
        assert test_text.count(include) == 1
        mutations = [
            ('missing-minimum', 'else if (p == 8U) { out[i] = 0x15; }',
                                'else if (p == 8U) { out[i] = 0x55; }'),
            ('colliding-input-id', '#define BBP_COMPOSITE_INPUT_ID 4U',
                                   '#define BBP_COMPOSITE_INPUT_ID 3U'),
        ]
        for name, before, after in mutations:
            assert header_text.count(before) == 1, name
            mutant = temp/(name + '.h')
            mutant.write_text(header_text.replace(before,after,1))
            testcase = temp/(name + '.c')
            testcase.write_text(test_text.replace(include, '#include "' + str(mutant) + '"',1))
            mutant_exe = temp/(name + '-test')
            subprocess.run([*common,'-fsanitize=undefined','-fno-sanitize-recover=all',
                            '-DBBP_COMPOSITE_HOST_ONLY=1',
                            '-I'+str(ROOT/'experiments/gamepad_protocol'),str(testcase),'-o',str(mutant_exe)],check=True)
            result = subprocess.run([str(mutant_exe)],input=stock,capture_output=True,
                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE,(0,0)))
            assert result.returncode != 0 and b'Assertion' in result.stderr, name
        print('PASS compiled code counterfactuals: host-only gate, missing minimum and colliding ID rejected')
        print('PASS array Usage domain shrink detected; unsupported 32-bit Extended Usage rejected')
        print('PASS complete-stream HID oracle: logical/physical ranges, units, usages, flags, stock prefix; ID collision, missing minimum, refs, LED and body negatives rejected')
        print('PASS original stock refs read through pinned HOG fixture; input4/output5 references are HOST MODEL ONLY, not a newly registered GATT service')
        print('LIMIT: no production wiring, ATT dispatch, subscription, ARM build, device, iPadOS, Android or Moonlight acceptance')


if __name__ == '__main__':
    main()
