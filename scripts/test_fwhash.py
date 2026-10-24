# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# fwhash.py's measurement of a fixed image, pinned as a literal. The same image
# and the same literal are in test/unit/boot/test_boot_measure.c, measured there
# by tfw-boot's own tfw_boot_measure: a change to either side that moves the
# measurement fails one of the two, and the pair cannot drift apart.

import contextlib
import importlib.util
import io
import os
import tempfile
import unittest

_spec = importlib.util.spec_from_file_location(
    "fwhash", os.path.join(os.path.dirname(os.path.abspath(__file__)), "fwhash.py"))
fwhash = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fwhash)

# 0x0000-0x000F: 00..0F; 0x0010-0x00FF: named by no record, so erased (0xFF);
# 0x0100-0x011F: i ^ 0x5A. image_len 0x120.
FIXED_HEX = """\
:10000000000102030405060708090A0B0C0D0E0F78
:100100005A5B58595E5F5C5D525350515657545577
:100110004A4B48494E4F4C4D424340414647444567
:00000001FF
"""
FIXED_MEASUREMENT = "121f2ecdba08af292814f0c4dc753886230f35664a3e147317b7805c9a0da330"
# The same bytes with the gap filled with 0x00, as `objcopy -O binary` writes it.
ZERO_GAP_MEASUREMENT = "10ae7788bf4c12743052c4b5165ca18f191a704dc1fdaaace75547029caefb90"
# image_len 0x140: the device measures 0x20 bytes of erased flash past the image.
PADDED_MEASUREMENT = "6be6bbdf4ff09a239952192d043a7bbf43714778d1b493d6bd5d2a5300be9403"
# image_len 0x100: the bytes at 0x100 (a trailer, say) are not measured.
SHORT_MEASUREMENT = "e05c7088ef5309f1955e3f5d155546f47e31d58209e6116feeb17e34ff31b09c"

# The mps2-an505's application slot (boards/mps2-an505/profile.mk): 128 KiB at
# 0x38000000, tfw-boot's region at 0x10000000 and nothing at flash 0.
SLOT_BASE, SLOT_SIZE = 0x38000000, 0x20000
# FIXED_HEX's records behind an extended linear address of 0x3800: the same
# bytes at the slot's base, so the same measurement.
SLOT_HEX = ":020000043800C2\n" + FIXED_HEX
# tfw-boot's region, below the slot.
BOOT_REGION_HEX = ":020000041000EA\n:01000000AA55\n:00000001FF\n"
# The slot's last byte, then the first byte past it.
SLOT_LAST_HEX = ":020000043801C1\n:01FFFF00AB56\n:00000001FF\n"
PAST_SLOT_HEX = ":020000043802C0\n:01000000AA55\n:00000001FF\n"


class Measurement(unittest.TestCase):
    def test_the_fixed_image_measures_to_the_pinned_literal(self):
        digest, n = fwhash.measure(FIXED_HEX)
        self.assertEqual(n, 0x120)
        self.assertEqual(digest.hex(), FIXED_MEASUREMENT)

    def test_a_gap_is_erased_flash_not_zeros(self):
        digest, _ = fwhash.measure(FIXED_HEX)
        self.assertNotEqual(digest.hex(), ZERO_GAP_MEASUREMENT)
        image = fwhash.flash_image(fwhash.parse_hex(FIXED_HEX))
        self.assertEqual(image[0x10:0x100], b"\xff" * 0xF0)

    def test_an_image_len_past_the_image_measures_erased_flash(self):
        digest, n = fwhash.measure(FIXED_HEX, 0x140)
        self.assertEqual((digest.hex(), n), (PADDED_MEASUREMENT, 0x140))

    def test_bytes_past_image_len_are_not_measured(self):
        digest, n = fwhash.measure(FIXED_HEX, 0x100)
        self.assertEqual((digest.hex(), n), (SHORT_MEASUREMENT, 0x100))

    def test_the_record_order_does_not_move_the_measurement(self):
        lines = FIXED_HEX.splitlines()
        shuffled = "\n".join([lines[2], lines[0], lines[1], lines[3]]) + "\n"
        self.assertEqual(fwhash.measure(shuffled)[0].hex(), FIXED_MEASUREMENT)

    def test_an_extended_linear_address_moves_the_record(self):
        # 0x00010010, past 64 KB: the ATmega2560's application reaches there.
        image = fwhash.flash_image(fwhash.parse_hex(":020000040001F9\n:01001000AB44\n:00000001FF\n"))
        self.assertEqual(len(image), 0x10011)
        self.assertEqual(image[0x10010], 0xAB)
        self.assertEqual(image[:0x10010], b"\xff" * 0x10010)

    def test_an_extended_segment_address_moves_the_record(self):
        # 0x1000 << 4 = 0x10000, the form avr-objcopy writes past 64 KB.
        image = fwhash.flash_image(fwhash.parse_hex(":020000021000EC\n:01001000AB44\n:00000001FF\n"))
        self.assertEqual(len(image), 0x10011)
        self.assertEqual(image[0x10010], 0xAB)


class ApplicationRegion(unittest.TestCase):
    def test_a_slot_image_measures_from_the_slot_base(self):
        digest, n = fwhash.measure(SLOT_HEX, base=SLOT_BASE, size=SLOT_SIZE)
        self.assertEqual((digest.hex(), n), (FIXED_MEASUREMENT, 0x120))
        image = fwhash.flash_image(fwhash.parse_hex(SLOT_HEX, SLOT_BASE, SLOT_SIZE))
        self.assertEqual(image[0x10:0x100], b"\xff" * 0xF0)

    def test_the_slot_last_byte_is_inside(self):
        image = fwhash.flash_image(fwhash.parse_hex(SLOT_LAST_HEX, SLOT_BASE, SLOT_SIZE),
                                   size=SLOT_SIZE)
        self.assertEqual(len(image), SLOT_SIZE)
        self.assertEqual(image[-1], 0xAB)
        self.assertEqual(image[:-1], b"\xff" * (SLOT_SIZE - 1))

    def test_a_record_outside_the_slot_is_refused(self):
        for text in (BOOT_REGION_HEX, PAST_SLOT_HEX, FIXED_HEX):
            with self.subTest(text=text), self.assertRaises(fwhash.HexError):
                fwhash.measure(text, base=SLOT_BASE, size=SLOT_SIZE)

    def test_an_image_len_past_the_slot_is_refused(self):
        with self.assertRaises(fwhash.HexError):
            fwhash.measure(SLOT_HEX, SLOT_SIZE + 1, base=SLOT_BASE, size=SLOT_SIZE)

    def test_the_slot_image_is_refused_at_flash_0(self):
        # The AVR default: a HEX at 0x38000000 is past any AVR part's flash.
        with self.assertRaises(fwhash.HexError):
            fwhash.measure(SLOT_HEX, size=256 * 1024)

    def test_a_region_past_any_flash_is_refused(self):
        for base, size in ((0, 0), (0, fwhash.MAX_FLASH + 1), (0xFFFF0000, 0x20000), (-1, 16)):
            with self.subTest(base=base, size=size), self.assertRaises(fwhash.HexError):
                fwhash.measure(FIXED_HEX, base=base, size=size)


class Refusals(unittest.TestCase):
    def refused(self, text, image_len=None):
        with self.assertRaises(fwhash.HexError):
            fwhash.measure(text, image_len)

    def test_a_bad_checksum_is_refused(self):
        self.refused(":10000000000102030405060708090A0B0C0D0E0F79\n:00000001FF\n")

    def test_a_count_that_does_not_match_the_record_is_refused(self):
        self.refused(":11000000000102030405060708090A0B0C0D0E0F77\n:00000001FF\n")

    def test_a_truncated_file_is_refused(self):
        self.refused(FIXED_HEX.replace(":00000001FF\n", ""))

    def test_a_record_after_the_end_is_refused(self):
        self.refused(FIXED_HEX + ":01001000AB44\n")

    def test_two_records_naming_one_byte_are_refused(self):
        self.refused(":01000F00AA46\n" + FIXED_HEX)

    def test_a_line_that_is_not_a_record_is_refused(self):
        self.refused("00000001FF\n")

    def test_a_line_that_is_not_hex_is_refused(self):
        self.refused(":0000000XFF\n")

    def test_an_unknown_record_type_is_refused(self):
        self.refused(":00000006FA\n:00000001FF\n")

    def test_an_address_record_of_the_wrong_length_is_refused(self):
        self.refused(":0100000400FB\n:00000001FF\n")

    def test_an_empty_image_is_refused(self):
        self.refused(":00000001FF\n")
        self.refused(FIXED_HEX, 0)

    def test_an_image_past_any_flash_is_refused(self):
        self.refused(":020000040100F9\n:01000000AA55\n:00000001FF\n")
        self.refused(FIXED_HEX, fwhash.MAX_FLASH + 1)

    def test_a_start_address_is_not_flash_content(self):
        with_start = ":0400000300000000F9\n:0400000500000000F7\n" + FIXED_HEX
        self.assertEqual(fwhash.measure(with_start)[0].hex(), FIXED_MEASUREMENT)


class CommandLine(unittest.TestCase):
    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = fwhash.main(list(argv))
        return rc, out.getvalue(), err.getvalue()

    def test_it_prints_the_measurement_and_image_len(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "app.hex")
            with open(path, "w", encoding="ascii") as f:
                f.write(FIXED_HEX)
            rc, out, _ = self.run_main(path)
            self.assertEqual((rc, out), (0, f"{FIXED_MEASUREMENT}  288  {path}\n"))
            rc, out, _ = self.run_main("--image-len", "0x100", path)
            self.assertEqual((rc, out), (0, f"{SHORT_MEASUREMENT}  256  {path}\n"))

    def test_write_image_writes_the_bytes_it_measured(self):
        with tempfile.TemporaryDirectory() as d:
            path, out_path = os.path.join(d, "app.hex"), os.path.join(d, "app.bin")
            with open(path, "w", encoding="ascii") as f:
                f.write(FIXED_HEX)
            rc, out, _ = self.run_main("--write-image", out_path, path)
            self.assertEqual((rc, out), (0, f"{FIXED_MEASUREMENT}  288  {path}\n"))
            with open(out_path, "rb") as f:
                image = f.read()
        self.assertEqual(image[:0x10], bytes(range(0x10)))
        self.assertEqual(image[0x10:0x100], b"\xff" * 0xF0)
        self.assertEqual(image[0x100:], bytes(i ^ 0x5A for i in range(0x20)))

    def test_base_and_size_name_the_region(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "app.hex")
            with open(path, "w", encoding="ascii") as f:
                f.write(SLOT_HEX)
            rc, out, _ = self.run_main("--base", "0x38000000", "--size", "131072", path)
            self.assertEqual((rc, out), (0, f"{FIXED_MEASUREMENT}  288  {path}\n"))
            rc, out, err = self.run_main(path)
            self.assertEqual((rc, out), (1, ""))
            self.assertIn("past the", err)

    def test_a_bad_file_exits_1_and_says_why(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "app.hex")
            with open(path, "w", encoding="ascii") as f:
                f.write(":00000001FE\n")
            rc, out, err = self.run_main(path)
            self.assertEqual((rc, out), (1, ""))
            self.assertIn("bad checksum", err)
            rc, _, err = self.run_main(os.path.join(d, "missing.hex"))
            self.assertEqual(rc, 1)
            self.assertIn("missing.hex", err)


if __name__ == "__main__":
    unittest.main()
