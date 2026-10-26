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
MAP_H = ROOT / 'config/boards/arm/bbp9981/custom_driver/gamepad_ble_map.h'

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


def parse_arrays():
    text = MAP_H.read_text()
    out = {}
    for name in ('bbp_map_safe', 'bbp_map_gamepad'):
        marker = f'static const uint8_t {name}[] = {{'
        body = text[text.index(marker) + len(marker):]
        body = body[:body.index('};')]
        out[name] = bytes(int(tok.strip(), 16) for tok in body.replace('\n', ' ').split(',')
                          if tok.strip().startswith('0x'))
    return out


def main():
    stock = bytes.fromhex(STOCK.read_text().strip())
    maps = parse_arrays()
    safe, pad = maps['bbp_map_safe'], maps['bbp_map_gamepad']
    assert len(stock) == 196, len(stock)
    assert len(safe) == 172 and len(pad) == 209, (len(safe), len(pad))
    assert max(len(safe), len(pad)) <= 244, 'must fit one 247-byte ATT MTU read'

    stock_reports, stock_apps = parse(stock)
    assert stock_apps == [(0x01, 0x06), (0x0C, 0x01), (0x01, 0x02)], stock_apps
    assert stock_reports == {1: {'input': 8, 'output': 1},
                             2: {'input': 12, 'output': 0},
                             3: {'input': 9, 'output': 0}}, stock_reports

    safe_reports, safe_apps = parse(safe)
    assert safe_apps == stock_apps + [(0xFF00, 0x01)], safe_apps
    assert safe_reports == {1: {'input': 8, 'output': 0},
                            2: {'input': 12, 'output': 0},
                            3: {'input': 9, 'output': 0},
                            4: {'input': 9, 'output': 0}}, safe_reports

    pad_reports, pad_apps = parse(pad)
    assert pad_apps == stock_apps + [(0x01, 0x05)], pad_apps
    assert pad_reports == safe_reports, pad_reports
    print('PASS dual maps: safe 172B (vendor ID4) and gamepad 209B (Game Pad ID4), '
          'IDs 1/2/3/4 unique, bodies 8/12/9/9 bytes, all collections balanced; HOST PARSER ONLY')


if __name__ == '__main__':
    sys.exit(main())
