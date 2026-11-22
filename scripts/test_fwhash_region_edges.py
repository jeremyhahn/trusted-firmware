# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The edges fwhash.py must accept: a region ending exactly at 4 GiB or exactly
# MAX_FLASH long, a record on the region's last byte, and the HEX a toolchain
# or an editor may write (blank lines, padding, CRLF, lowercase, an empty data
# record). test_fwhash.py pins the refusals; this pins that the refusals stop
# there. FWHASH_PY names another copy of the script, for a mutation run only.

import importlib.util
import os
import unittest

FWHASH_PY = os.environ.get(
    "FWHASH_PY", os.path.join(os.path.dirname(os.path.abspath(__file__)), "fwhash.py"))

_spec = importlib.util.spec_from_file_location("fwhash", FWHASH_PY)
fwhash = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fwhash)


class RegionEdges(unittest.TestCase):
    def test_a_region_up_to_the_top_of_the_address_space_is_accepted(self):
        for base, size in ((0xFFFF0000, 0x10000), (0, fwhash.MAX_FLASH), (0, 1),
                           (0x38000000, 0x20000)):
            with self.subTest(base=hex(base), size=size):
                self.assertIsNone(fwhash.check_region(base, size))

    def test_a_region_one_byte_past_4_gib_is_refused(self):
        with self.assertRaises(fwhash.HexError) as cm:
            fwhash.check_region(0xFFFF0001, 0x10000)
        self.assertIn("region 0xFFFF0001 + 65536 is not an application region", str(cm.exception))

    def test_a_record_on_the_region_last_byte_is_inside(self):
        self.assertEqual(fwhash.parse_hex(":01000F00AB45\n:00000001FF\n", base=0, size=0x10),
                         [(15, b"\xab")])

    def test_a_record_one_past_the_region_is_refused(self):
        with self.assertRaises(fwhash.HexError) as cm:
            fwhash.parse_hex(":01001000AB44\n:00000001FF\n", base=0, size=0x10)
        self.assertIn("past the 16-byte application region", str(cm.exception))

    def test_an_extended_address_inside_a_based_region_is_an_offset(self):
        text = ":020000040001F9\n:01001000AB44\n:00000001FF\n"
        self.assertEqual(fwhash.parse_hex(text, base=0x10000, size=0x100), [(16, b"\xab")])


class HexTolerances(unittest.TestCase):
    def test_blank_lines_padding_crlf_and_an_empty_data_record_are_skipped(self):
        text = "\n  :01001000AB44  \r\n\n:00002000E0\n:00000001FF\r\n"
        self.assertEqual(fwhash.parse_hex(text), [(16, b"\xab")])

    def test_lowercase_hex_is_read(self):
        self.assertEqual(fwhash.parse_hex(":01001000ab44\n:00000001ff\n"), [(16, b"\xab")])


if __name__ == "__main__":
    unittest.main()
