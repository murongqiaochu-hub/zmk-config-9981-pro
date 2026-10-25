#!/usr/bin/env python3
"""Offline, fail-closed CDC mapping for the 9981 Zephyr legacy USB ABI.

No network, serial, build, device access or HID payload output. Addresses are
recovered from this image, not fitted to pointer differences. This intentionally
rejects other SDK layouts instead of guessing. Port names require Mac IORegistry.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys

try:
    from .verify_firmware import validate_uf2
except ImportError:
    from verify_firmware import validate_uf2

CALLBACK = bytes.fromhex('4b1cdbb28170817680f82530c37600f8061c7047')
RAM_MIN, RAM_MAX = 0x20000000, 0x20040000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_regular(path):
    """Reject special files before open, and recheck the opened inode.

    O_NONBLOCK prevents FIFO races; O_NOFOLLOW rejects final symlinks. No reads
    occur unless fstat confirms the same regular inode as lstat.
    """
    path = Path(path)
    before = path.lstat()
    require(stat.S_ISREG(before.st_mode), 'Input must be a regular file, not a device/symlink')
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        opened = os.fstat(fd)
        require(stat.S_ISREG(opened.st_mode) and
                (opened.st_dev, opened.st_ino) == (before.st_dev, before.st_ino),
                'Input changed or is not a regular file')
        # Application partition bounds also bound host memory use.
        require(0 < opened.st_size <= 2 * (0xD4000 - 0x1000), 'Invalid UF2 file size')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            raw = stream.read(opened.st_size + 1)
        require(len(raw) == opened.st_size, 'Input size changed during read')
        return raw
    finally:
        os.close(fd)


class Image:
    def __init__(self, raw):
        self.uf2 = validate_uf2(raw)
        self.base = int(self.uf2['address_start'], 16)
        self.end = int(self.uf2['address_end_exclusive'], 16)
        self.data = b''.join(raw[i + 32:i + 288] for i in range(0, len(raw), 512))

    def read(self, address, size):
        require(size >= 0 and self.base <= address and address + size <= self.end,
                'Address outside validated image: ' + hex(address))
        return self.data[address - self.base:address - self.base + size]

    def u16(self, address):
        return struct.unpack('<H', self.read(address, 2))[0]

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def find(self, needle):
        offset = self.data.find(needle)
        while offset >= 0:
            yield self.base + offset
            offset = self.data.find(needle, offset + 1)

    def literal(self, address, register):
        insn = self.u16(address)
        require(insn & 0xF800 == 0x4800 and (insn >> 8) & 7 == register,
                'Unexpected Thumb literal load')
        literal = ((address + 4) & ~3) + (insn & 255) * 4
        return literal, self.u32(literal)

    def branch(self, address, link=True):
        h1, h2 = self.u16(address), self.u16(address + 2)
        require(h1 & 0xF800 == 0xF000 and
                h2 & 0xD000 == (0xD000 if link else 0x9000), 'Unexpected Thumb branch')
        s, j1, j2 = (h1 >> 10) & 1, (h2 >> 13) & 1, (h2 >> 11) & 1
        imm = (s << 24 | ((~(j1 ^ s)) & 1) << 23 | ((~(j2 ^ s)) & 1) << 22 |
               (h1 & 0x3FF) << 12 | (h2 & 0x7FF) << 1)
        target = address + 4 + (imm - (1 << 25) if s else imm)
        self.read(target, 2)
        return target

    def calls(self, start, length):
        for address in range(start, min(start + length, self.end - 3), 2):
            try:
                yield address, self.branch(address)
            except ValueError:
                continue


def discover_copy(image):
    candidates = []
    for address in image.find(b'\x08\xb5'):
        if address % 2:
            continue
        try:
            dst_literal, ram_start = image.literal(address + 2, 0)
            end_literal, ram_end = image.literal(address + 4, 2)
            src_literal, load_start = image.literal(address + 6, 1)
            require(image.u16(address + 8) == 0x1A12, 'Not data copy subtraction')
            helper = image.branch(address + 10)
            memcpy = image.branch(helper, link=False)
            require(RAM_MIN <= ram_start < ram_end <= RAM_MAX and
                    ram_start % 4 == ram_end % 4 == load_start % 4 == 0,
                    'Unreasonable RAM copy range')
            require(image.base <= address < load_start and helper < load_start and
                    memcpy < load_start, 'Copy code must precede initialized data')
            image.read(load_start, ram_end - ram_start)
        except ValueError:
            continue
        candidates.append(dict(function=address, ram_start_literal=dst_literal,
                               ram_end_literal=end_literal, load_start_literal=src_literal,
                               ram_start=ram_start, ram_end_exclusive=ram_end,
                               flash_load_start=load_start, early_copy_helper=helper,
                               memcpy_target=memcpy))
    require(len(candidates) == 1, 'Expected one plausible Thumb RAM data-copy template')
    copy = candidates[0]
    reset = image.u32(image.base + 4)
    require(reset & 1, 'Reset vector is not Thumb')
    image.read(reset & ~1, 64)
    # Narrow SDK startup ABI: reset calls z_cstart, which calls data_copy.
    # These windows cover actual instructions, not pointer-table references;
    # this is a static linkage check, not a general control-flow proof.
    chains = []
    for reset_call, startup in image.calls(reset & ~1, 64):
        for startup_call, target in image.calls(startup, 128):
            if target == copy['function']:
                chains.append((reset_call, startup, startup_call))
    require(len(chains) == 1, 'Expected unique reset -> startup -> data-copy BL chain')
    copy.update(reset_vector=reset, reset_call=chains[0][0], startup=chains[0][1],
                startup_call=chains[0][2])
    return copy


def inspect_raw(raw, expected_sha):
    require(re.fullmatch(r'[0-9a-fA-F]{64}', expected_sha or '') is not None,
            'Expected SHA256 must be exactly 64 hex characters')
    digest = hashlib.sha256(raw).hexdigest()
    require(digest == expected_sha.lower(), 'UF2 SHA256 mismatch')
    image = Image(raw)
    copy = discover_copy(image)
    ram_start, ram_end, load_start = (copy[k] for k in
                                    ('ram_start', 'ram_end_exclusive', 'flash_load_start'))

    def lma(vma, size=1):
        require(ram_start <= vma and vma + size <= ram_end, 'VMA outside initialized RAM')
        return load_start + vma - ram_start

    def code(pointer):
        require(pointer & 1 and image.base <= (pointer & ~1) < load_start,
                'Invalid Thumb callback pointer')
        image.read(pointer & ~1, 2)

    devices = {}
    for name in ('debug_cdc_acm_uart', 'cdc_acm_uart'):
        found = []
        for string_addr in image.find(name.encode() + b'\0'):
            for device in image.find(struct.pack('<I', string_addr)):
                if device % 4:
                    continue
                try:
                    config, api, state, data = struct.unpack('<4I', image.read(device + 4, 16))
                    require(ram_start <= config < ram_end and image.base <= api < load_start
                            and RAM_MIN <= state < RAM_MAX and RAM_MIN <= data < RAM_MAX,
                            'Not a device object')
                    cfg_lma = lma(config, 36)
                    words = struct.unpack('<9I', image.read(cfg_lma, 36))
                    require(words[0] == 0 and words[5:8] == (0, 0, 3), 'Unexpected CDC config ABI')
                    if0, callback = words[1:3]
                    if0_lma = lma(if0, 9)
                    require(image.read(if0_lma, 9) == bytes.fromhex('090400000102020000'),
                            'Unexpected CDC control descriptor')
                except ValueError:
                    continue
                # Once identified, structural damage is an error, not a fallback.
                block = image.read(if0_lma - 8, 66)
                require(block[:8] == bytes.fromhex('080b000202020000') and
                        block[17:22] == bytes.fromhex('0524001001') and
                        block[22:25] == bytes.fromhex('052401') and
                        block[25] & ~3 == 0 and block[26] == 1 and
                        block[27:30] == bytes.fromhex('042402') and block[30] & ~0x0F == 0 and
                        block[31:36] == bytes.fromhex('0524060001') and
                        block[43:52] == bytes.fromhex('09040100020a000000'),
                        'Unexpected CDC IAD/functional/data descriptors')
                # Packed descriptors: interrupt IN, bulk IN, bulk OUT. The
                # endpoint callback table below instead uses interrupt/OUT/IN.
                for offset, initial, transfer in ((36, 0x80, 3), (52, 0x80, 2), (59, 0, 2)):
                    ep = block[offset:offset + 7]
                    require(ep[:4] == bytes((7, 5, initial, transfer)) and
                            0 < int.from_bytes(ep[4:6], 'little') <= 64,
                            'Unexpected CDC endpoint descriptor')
                code(callback)
                require(image.read(callback & ~1, len(CALLBACK)) == CALLBACK,
                        'CDC N/N+1 fixup callback differs from verified SDK instructions')
                ep_lma = lma(words[8], 24)
                endpoints = []
                for i, initial in enumerate((0x80, 0, 0x80)):
                    cb = image.u32(ep_lma + 8 * i)
                    code(cb)
                    require(image.read(ep_lma + 8 * i + 4, 1)[0] == initial,
                            'CDC endpoint table differs from descriptors')
                    endpoints.append({'callback': hex(cb), 'initial_endpoint': hex(initial)})
                found.append(dict(name=name, name_string=hex(string_addr), device_rom=hex(device),
                                  device_api=hex(api), config_vma=hex(config), config_lma=hex(cfg_lma),
                                  if0_vma=hex(if0), if0_lma=hex(if0_lma),
                                  cdc_block_lma=hex(if0_lma - 8), callback_thumb=hex(callback),
                                  endpoint_table_vma=hex(words[8]), endpoint_table_lma=hex(ep_lma),
                                  endpoints=endpoints))
        require(len(found) == 1, 'Expected unique device object: ' + name)
        devices[name] = found[0]
    debug, studio = devices.values()
    require(debug['device_api'] == studio['device_api'] and
            debug['callback_thumb'] == studio['callback_thumb'] and
            debug['endpoints'] == studio['endpoints'] and
            debug['config_vma'] != studio['config_vma'] and
            debug['if0_vma'] != studio['if0_vma'], 'CDC instances are not distinct/shared as expected')

    starts = []
    for addr in image.find(b'\x12\x01'):
        if (load_start <= addr and addr + 27 <= load_start + ram_end - ram_start and
                image.read(addr + 8, 4) == bytes.fromhex('501d5e61') and
                image.read(addr + 18, 2) == b'\x09\x02'):
            starts.append(addr)
    require(len(starts) == 1, 'Expected unique USB device/configuration descriptor start')
    start = addr = starts[0]
    roles = {int(d['if0_lma'], 16): d for d in devices.values()}
    interfaces = []
    while True:
        require(addr - start < 1024 and addr + 2 <= load_start + ram_end - ram_start,
                'USB descriptor walk exceeds initialized data or size bound')
        length, kind = image.read(addr, 2)
        if length == 0 or kind == 3:
            break
        require(length >= 2 and addr + length <= load_start + ram_end - ram_start,
                'Invalid USB bLength')
        descriptor = image.read(addr, length)
        if kind == 4:
            require(length == 9 and descriptor[3] == 0, 'Alternate/unexpected interface ABI')
            number = len(interfaces)
            role = roles.get(addr)
            if role is not None:
                role.update(control_interface=number, data_interface=number + 1)
            interfaces.append(dict(assigned_number=number, descriptor_lma=hex(addr),
                                   descriptor_vma=hex(ram_start + addr - load_start),
                                   initial_number_before_fixup=descriptor[2],
                                   **{'class': descriptor[5], 'subclass': descriptor[6]}))
        addr += length
    require([x['class'] for x in interfaces] == [2, 10, 3, 2, 10, 3],
            'USB class order changed; do not infer ports')
    require(addr - (start + 18) == 191, 'Unexpected physical configuration descriptor length')
    require(all('control_interface' in d for d in devices.values()), 'CDC outside physical USB table')
    for d in devices.values():
        n = d['control_interface']
        require(interfaces[n]['class'] == 2 and interfaces[n + 1]['class'] == 10 and
                int(interfaces[n + 1]['descriptor_lma'], 16) == int(d['if0_lma'], 16) + 35,
                'CDC data interface is not adjacent to its control interface')
    return dict(status='PASS', analysis='offline parsing and narrow SDK Thumb decoding',
                uf2_sha256=digest, uf2=image.uf2,
                startup_copy={**{k: hex(v) for k, v in copy.items()},
                              'length': ram_end - ram_start,
                              'flash_load_end_exclusive': hex(load_start + ram_end - ram_start)},
                devices=devices, descriptor_start_lma=hex(start),
                descriptor_start_vma=hex(ram_start + start - load_start),
                configuration_total_length_after_fixup=addr - start - 18, interfaces=interfaces,
                candidate_mapping={'debug_console': {'control': debug['control_interface'],
                                                     'data': debug['data_interface']},
                                   'studio_rpc': {'control': studio['control_interface'],
                                                  'data': studio['data_interface']}},
                mac_report_correlation={'port_names': None,
                    'required_evidence': 'Match Mac IORegistry USB interface numbers to serial nodes; '
                                         'usbmodem suffixes alone are not evidence.'},
                boundary='Exact input UF2 only, not running-device readback. Static SDK ABI check, '
                         'not general Thumb control-flow analysis. No serial payload is read or output.',
                serial_ports_opened=0, termios_or_dtr_changed=False,
                firmware_compiled_or_modified=False, actions_triggered=False)


def verify(path, expected_sha):
    return inspect_raw(read_regular(path), expected_sha)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uf2', required=True, type=Path, help='regular UF2 file, never a device')
    parser.add_argument('--expected-sha', required=True, help='independently obtained exact UF2 SHA256')
    args = parser.parse_args()
    try:
        result = verify(args.uf2, args.expected_sha)
    except (ValueError, OSError) as exc:
        print(json.dumps({'status': 'FAIL', 'error': str(exc)}, ensure_ascii=False), file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
