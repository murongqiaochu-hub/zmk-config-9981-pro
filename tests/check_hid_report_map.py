#!/usr/bin/env python3
"""Parse the real 196-byte HOG map + the appended Game Pad collection.

The BLE gamepad reuses the stock HID service and extends its Report Map by a
two-segment read. That design was only ever exercised against a 45-byte toy
map, so parse the REAL map plus the real 141-byte collection here and prove
the concatenation is a well-formed descriptor with no report-ID collision.

Host-only static evidence: no radio, no host acceptance.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
STOCK = ROOT / 'tests/hid_map_fixtures/stock_196.hex'
REPORT_H = ROOT / 'config/boards/arm/bbp9981/custom_driver/gamepad_report.h'

MAIN, GLOBAL, LOCAL = 0, 1, 2


def items(data, start=0, end=None):
    """Yield (kind, tag, value_list) for each descriptor item; strict length."""
    end = len(data) if end is None else end
    i = start
    while i < end:
        prefix = data[i]
        size = (0, 1, 2, 4)[prefix & 0x3] if (prefix & 0x3) != 3 else 4
        kind = (prefix >> 2) & 0x3
        tag = prefix >> 4
        if i + 1 + size > end:
            raise AssertionError(f'truncated item at {i}')
        value = int.from_bytes(data[i + 1:i + 1 + size], 'little') if size else 0
        yield kind, tag, value
        i += 1 + size


def parse(data):
    """Return (reports, collections) with report-ID scoped input sizes."""
    report_id = None
    sizes = []
    reports, stack, apps = {}, [], []
    count, size_bits = 0, 0
    usage_page = usage = None
    for kind, tag, value in items(data):
        if kind == GLOBAL:
            if tag == 0:      # Usage Page
                usage_page = value
            elif tag == 7:    # Report Size
                size_bits = value
            elif tag == 8:    # Report ID
                report_id = value
            elif tag == 9:    # Report Count
                count = value
        elif kind == LOCAL:
            if tag == 0:
                usage = value
        else:
            if tag == 8:      # Input
                if report_id is None:
                    raise AssertionError('input report without Report ID')
                kind_bits = 0 if value & 0x1 else (2 if value & 0x2 else 1)
                entry = reports.setdefault(report_id, {'in_bits': 0, 'out_bits': 0})
                entry['in_bits'] += count * size_bits
            elif tag == 9:    # Output
                if report_id is not None:
                    entry = reports.setdefault(report_id, {'in_bits': 0, 'out_bits': 0})
                    entry['out_bits'] += count * size_bits
            elif tag == 10:   # Collection
                if not stack:
                    apps.append((usage_page, usage))
                stack.append((usage_page, usage))
            elif tag == 12:   # End Collection
                if not stack:
                    raise AssertionError('unbalanced End Collection')
                stack.pop()
    if stack:
        raise AssertionError(f'{len(stack)} unterminated collection(s)')
    return ({rid: {'input': (v.get('in_bits', 0) + 7) // 8,
                    'output': (v.get('out_bits', 0) + 7) // 8}
             for rid, v in reports.items()}, apps)


def gamepad_bytes():
    text = REPORT_H.read_text()
    macro = re.search(r'#define BBP_GAMEPAD_DESCRIPTOR_BYTES(.*?)\n\n', text, re.S).group(1)
    report_id = int(re.search(r'#define BBP_GAMEPAD_REPORT_ID\s+(\d+)', text).group(1))
    out = []
    for token in macro.replace('\n', ' ').split(','):
        token = token.strip().lstrip('\\').strip()
        if not token:
            continue
        if token.startswith('0x'):
            out.append(int(token, 16))
        elif token.isdigit():
            out.append(int(token))
        elif token == 'BBP_GAMEPAD_REPORT_ID':
            out.append(report_id)
        else:
            raise AssertionError(f'unexpected descriptor token {token!r}')
    return bytes(out), report_id


def main():
    stock = bytes.fromhex(STOCK.read_text().strip())
    gp, gp_id = gamepad_bytes()
    assert len(stock) == 196, len(stock)
    assert len(gp) == 141, len(gp)
    assert gp_id == 4, gp_id

    stock_reports, stock_colls = parse(stock)
    gp_reports, gp_colls = parse(gp)
    assert stock_colls == [(0x01, 0x06), (0x0C, 0x01), (0x01, 0x02)], stock_colls
    assert gp_colls == [(0x01, 0x05)], gp_colls
    assert stock_reports == {1: {'input': 8, 'output': 1},
                             2: {'input': 12, 'output': 0},
                             3: {'input': 9, 'output': 0}}, stock_reports
    assert gp_reports == {4: {'input': 9, 'output': 0}}, gp_reports

    combined = stock + gp
    reports, colls = parse(combined)
    assert colls == stock_colls + gp_colls, colls
    assert reports == {**stock_reports, 4: {'input': 9, 'output': 0}}, reports

    # Trailing bytes of the stock map must not be swallowed by the appendix.
    assert combined[:196] == stock and combined[196:] == gp
    print('PASS composite HID map: real 196B HOG + 141B Game Pad, IDs 1/2/3/4 unique, '
          'gamepad input 9 bytes, all collections balanced; HOST PARSER ONLY')


if __name__ == '__main__':
    sys.exit(main())
