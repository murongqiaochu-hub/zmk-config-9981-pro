#!/usr/bin/env python3
"""Reject configuration edits outside the new, precisely projected policy layer."""
from pathlib import Path
import unittest
from ble_single_host_scope import ROOT, ADDED, base, base_paths, verify_configuration


class SingleHostScopeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.current = {str(p.relative_to(ROOT)): p.read_bytes()
                       for p in (ROOT / 'config').rglob('*') if p.is_file()}

    def rejects(self, mutate):
        current = dict(self.current)
        mutate(current)
        with self.assertRaises(AssertionError):
            verify_configuration(current)

    def test_exact_v233_projection(self):
        view = verify_configuration(self.current)
        self.assertEqual(set(view), set(base_paths()))
        for name, data in view.items():
            self.assertEqual(data, base(name), name)

    def test_no_keymap_exception(self):
        self.rejects(lambda v: v.__setitem__('config/bbp9981.keymap',
                     v['config/bbp9981.keymap'] + b'\n/* unauthorized */\n'))

    def test_no_hog_or_usb_exception(self):
        for leaf in ['trackpad_hog_snapshot.h', 'trackpad_usb_snapshot.h',
                     'trackpad_pointer.h', 'keyboard_backlight.c']:
            with self.subTest(leaf=leaf):
                name = 'config/boards/arm/bbp9981/custom_driver/' + leaf
                self.rejects(lambda v, p=name: v.__setitem__(p, v[p] + b'\n'))

    def test_no_existing_config_value_change(self):
        self.rejects(lambda v: v.__setitem__('config/bbp9981.conf',
                     v['config/bbp9981.conf'].replace(b'CONFIG_ZMK_SLEEP=n',
                                                    b'CONFIG_ZMK_SLEEP=y')))

    def test_no_unknown_file(self):
        self.rejects(lambda v: v.__setitem__('config/unreviewed.c', b''))

    def test_every_new_file_is_required(self):
        for name in ADDED:
            with self.subTest(name=name):
                self.rejects(lambda v, p=name: v.pop(p))


if __name__ == '__main__':
    unittest.main(verbosity=2)
