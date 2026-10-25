#!/usr/bin/env python3
"""Self-contained CDC tests; optionally add the exact historical UF2 regression.

python3 tests/test_cdc_mapping.py [--old-uf2 /path/to/verified.uf2]
No compilation, network or serial access. All mutations are temporary regular files.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.inspect_cdc_mapping import CALLBACK, inspect_raw, verify

OLD_SHA = '9dd7bbf5e33ef2a414e7725994c710131c9036e751f322f7009783e08e4691fe'
OLD_UF2 = None


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def thumb_branch(address, target, link=True):
    imm = (target - address - 4) & ((1 << 25) - 1)
    s, i1, i2 = (imm >> 24) & 1, (imm >> 23) & 1, (imm >> 22) & 1
    j1, j2 = (~(i1 ^ s)) & 1, (~(i2 ^ s)) & 1
    return struct.pack('<HH', 0xF000 | s << 10 | (imm >> 12) & 0x3FF,
                       (0xD000 if link else 0x9000) | j1 << 13 | j2 << 11 | (imm >> 1) & 0x7FF)


def pack_uf2(image):
    count = len(image) // 256
    blocks = []
    for i in range(count):
        block = bytearray(512)
        struct.pack_into('<8I', block, 0, 0x0A324655, 0x9E5D5157, 0x2000,
                         0x1000 + i * 256, 256, i, count, 0xADA52840)
        block[32:288] = image[i * 256:(i + 1) * 256]
        struct.pack_into('<I', block, 508, 0x0AB16F30)
        blocks.append(block)
    return b''.join(blocks)


def fixture(shift=0, swap_roles=False):
    """Independent synthetic SDK layout; shifted data/code defeat fixed addresses."""
    image = bytearray(0x3000)

    def put(addr, data):
        image[addr - 0x1000:addr - 0x1000 + len(data)] = data

    def word(addr, value):
        put(addr, struct.pack('<I', value))

    reset, startup, copy, helper, memcpy = (x + shift for x in (0x1100, 0x1200, 0x1400, 0x1500, 0x1600))
    callback, api, load = 0x1700 + shift, 0x17C0 + shift, 0x2000 + shift
    ram = 0x20000000 + shift
    word(0x1004, reset | 1)
    put(reset + 0x26, thumb_branch(reset + 0x26, startup))
    put(startup + 0x46, thumb_branch(startup + 0x46, copy))
    put(copy, bytes.fromhex('08b5'))
    for offset, reg, lit, value in ((2, 0, copy + 32, ram), (4, 2, copy + 36, ram + 0x400),
                                    (6, 1, copy + 40, load)):
        instruction = 0x4800 | reg << 8 | (lit - ((copy + offset + 4) & ~3)) // 4
        put(copy + offset, struct.pack('<H', instruction))
        word(lit, value)
    put(copy + 8, bytes.fromhex('121a'))
    put(copy + 10, thumb_branch(copy + 10, helper))
    put(helper, thumb_branch(helper, memcpy, False))
    put(memcpy, bytes.fromhex('7047'))
    put(callback, CALLBACK)
    cdc = bytes.fromhex('080b000202020000 090400000102020000 '
                        '0524001001 0524010201 04240202 0524060001 '
                        '0705800310000a 09040100020a000000 '
                        '07058002400000 07050002400000')
    hid = bytes.fromhex('090400000103000000 092111010001222000 07058003400001')
    device = bytes.fromhex('1201000200000040501d5e61000101020301')
    config = bytes.fromhex('090200000001008032')
    put(load, device + config + cdc + hid + cdc + hid + bytes.fromhex('04030904'))
    for i, name in enumerate(('debug_cdc_acm_uart', 'cdc_acm_uart')):
        string = 0x1800 + shift + i * 32
        dev = 0x1900 + shift + i * 32
        cfg = ram + 0x200 + i * 64
        ep = ram + 0x280 + i * 32
        # studio first, debug second by default, but derive instead of assuming.
        second = (i == 0) != swap_roles
        if0 = ram + 18 + 9 + (91 if second else 0) + 8
        put(string, name.encode() + b'\0')
        put(dev, struct.pack('<5I', string, cfg, api, ram + 0x500, ram + 0x600))
        put(load + cfg - ram, struct.pack('<9I', 0, if0, callback | 1, 0, 0, 0, 0, 3, ep))
        for n, endpoint in enumerate((0x80, 0, 0x80)):
            ep_cb = 0x1741 + shift + n * 0x20
            put(ep_cb & ~1, bytes.fromhex('7047'))
            put(load + ep - ram + n * 8, struct.pack('<IB3x', ep_cb, endpoint))
    return image


class MappingTests(unittest.TestCase):
    def check_image(self, image):
        raw = pack_uf2(image)
        return inspect_raw(raw, sha(raw))

    def test_mapping_and_relocated_copy(self):
        for shift in (0, 0x400):
            with self.subTest(shift=shift):
                result = self.check_image(fixture(shift))
                self.assertEqual(result['candidate_mapping'], {
                    'debug_console': {'control': 3, 'data': 4}, 'studio_rpc': {'control': 0, 'data': 1}})
                self.assertEqual(result['startup_copy']['flash_load_start'], hex(0x2000 + shift))
                self.assertEqual(result['startup_copy']['ram_start'], hex(0x20000000 + shift))
                self.assertIsNone(result['mac_report_correlation']['port_names'])
                self.assertNotIn('callback_machine_bytes', str(result))

    def test_role_numbers_derived_from_physical_order(self):
        result = self.check_image(fixture(swap_roles=True))
        self.assertEqual(result['candidate_mapping']['debug_console'], {'control': 0, 'data': 1})

    def test_regular_file_and_sha(self):
        raw = pack_uf2(fixture())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.uf2'
            path.write_bytes(raw)
            self.assertEqual(verify(path, sha(raw))['uf2_sha256'], sha(raw))
            for wrong in ('0' * 64, '', 'bad'):
                with self.assertRaises(ValueError):
                    verify(path, wrong)
            path.write_bytes(raw[:-1] + bytes((raw[-1] ^ 1,)))
            with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
                verify(path, sha(raw))

    def test_nonregular_rejected(self):
        import os
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                verify(root, '0' * 64)
            fifo = root / 'fifo'
            os.mkfifo(fifo)
            with self.assertRaises(ValueError):
                verify(fifo, '0' * 64)
            regular = root / 'regular'
            regular.write_bytes(b'not UF2')
            link = root / 'link'
            link.symlink_to(regular)
            with self.assertRaises(ValueError):
                verify(link, '0' * 64)

    def test_all_blocks_validated_even_with_matching_sha(self):
        original = pack_uf2(fixture())
        for offset in (0, 512 + 8, 512 + 12, 512 + 16, 512 + 20, 512 + 24,
                       512 + 28, len(original) - 4):
            with self.subTest(offset=offset):
                raw = bytearray(original)
                raw[offset] ^= 1
                with self.assertRaises(ValueError):
                    inspect_raw(raw, sha(raw))
        for raw in (b'', original[:-1], original + original[:512]):
            with self.assertRaises(ValueError):
                inspect_raw(raw, sha(raw))

    def test_semantic_tampering_with_recomputed_sha(self):
        # Template, BL chain, RAM end, fixup callback, endpoint table,
        # CDC IAD, functional descriptor, physical bLength, HID class.
        for address in (0x1408, 0x1126, 0x1246, 0x1424, 0x1700, 0x2284,
                        0x201B, 0x201B + 31, 0x2012, 0x205D + 5):
            with self.subTest(address=hex(address)):
                image = fixture()
                image[address - 0x1000] ^= 1
                with self.assertRaises(ValueError):
                    self.check_image(image)

    def test_ambiguous_copy_rejected(self):
        image = fixture()
        # Duplicate template including literals; PC-relative offsets stay valid.
        image[0xA00:0xA2C] = image[0x400:0x42C]
        image[0xA0A:0xA0E] = thumb_branch(0x1A0A, 0x1500)
        with self.assertRaisesRegex(ValueError, 'one plausible'):
            self.check_image(image)

    def test_exact_old_uf2_regression(self):
        if OLD_UF2 is None:
            self.skipTest('optional --old-uf2 not supplied')
        result = verify(OLD_UF2, OLD_SHA)
        self.assertEqual(result['candidate_mapping'], {
            'debug_console': {'control': 3, 'data': 4}, 'studio_rpc': {'control': 0, 'data': 1}})
        self.assertEqual(result['startup_copy']['ram_start'], '0x20000000')
        self.assertEqual(result['startup_copy']['ram_end_exclusive'], '0x20001db8')
        self.assertEqual(result['startup_copy']['flash_load_start'], '0x48de8')
        self.assertEqual(result['uf2_sha256'], OLD_SHA)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--old-uf2', type=Path)
    args, remaining = parser.parse_known_args()
    OLD_UF2 = args.old_uf2
    unittest.main(argv=[sys.argv[0]] + remaining)
