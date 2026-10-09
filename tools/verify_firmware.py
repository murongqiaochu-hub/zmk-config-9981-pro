#!/usr/bin/env python3
"""Validate 9981 Pro UF2 boundaries; never flash or write NVS/bootloader data."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

CODE_START = 0x1000
CODE_END = 0xD4000
FAMILY = 0xADA52840

def validate_uf2(data):
    if not data or len(data) % 512:
        raise ValueError('UF2 must be nonempty and a multiple of 512 bytes')
    count = len(data) // 512
    addresses = []
    for i in range(count):
        block = data[i*512:(i+1)*512]
        m0, m1, flags, addr, size, number, total, family = struct.unpack_from('<8I', block)
        if (m0, m1) != (0x0A324655, 0x9E5D5157) or struct.unpack_from('<I', block, 508)[0] != 0x0AB16F30:
            raise ValueError('Invalid UF2 magic')
        if flags != 0x2000 or family != FAMILY:
            raise ValueError('Unexpected UF2 flags or MCU family')
        if number != i or total != count or size != 256:
            raise ValueError('Invalid block numbering, count or payload size')
        if addr % 256 or addr < CODE_START or addr + size > CODE_END:
            raise ValueError('UF2 target falls outside the application code partition')
        addresses.append(addr)
    if addresses[0] != CODE_START or any(y-x != 256 for x,y in zip(addresses,addresses[1:])):
        raise ValueError('UF2 code blocks are not contiguous from application start')
    return {'bytes': len(data), 'blocks': count, 'family': hex(FAMILY),
            'address_start': hex(addresses[0]), 'address_end_exclusive': hex(addresses[-1]+256),
            'sha256': hashlib.sha256(data).hexdigest()}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--run-id', type=int, required=True)
    args = parser.parse_args()
    if len(args.commit) != 40 or any(c not in '0123456789abcdef' for c in args.commit) or args.run_id <= 0:
        parser.error('Expected a full source commit and positive source run ID')
    files = list(args.directory.rglob('*.uf2'))
    if len(files) != 1:
        parser.error('Expected exactly one UF2 file')
    p = files[0]
    result = validate_uf2(p.read_bytes())
    result.update(source_commit=args.commit, source_run=args.run_id,
                  file=str(p.relative_to(args.directory)), device_tested=False)
    (args.directory/'SHA256SUMS').write_text(result['sha256']+'  '+result['file']+'\n')
    (args.directory/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__ == '__main__':
    main()
