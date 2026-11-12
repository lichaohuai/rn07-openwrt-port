# SPDX-License-Identifier: GPL-2.0-only
import pathlib, struct, sys, unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from ubi_layout import LayoutError, squashfs_check, verify
class LayoutTests(unittest.TestCase):
    def test_real_static_volume_truncation_regression(self):
        data = bytearray(7386580); data[:4] = b'hsqs'
        struct.pack_into('<Q', data, 40, len(data)); struct.pack_into('<Q', data, 48, 7386572)
        with self.assertRaises(LayoutError): squashfs_check(data, 7386112)
        self.assertEqual(squashfs_check(data, 7491584)['bytes_used'], len(data))
    def test_reject_partial_peb(self):
        with self.assertRaises(LayoutError): verify(bytes(131071))
    def test_reject_bad_erase_header(self):
        with self.assertRaises(LayoutError): verify(bytes(131072))
    def test_reject_invalid_squashfs_id_table(self):
        data = bytearray(96); data[:4] = b'hsqs'
        struct.pack_into('<Q', data, 40, 96); struct.pack_into('<Q', data, 48, 95)
        with self.assertRaises(LayoutError): squashfs_check(data)
if __name__ == '__main__': unittest.main()
