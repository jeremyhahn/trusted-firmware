# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for make bootloader-candidate (make scripts-test).

The target measures a tfw-boot row that is not in BOOT_SUPPORTED yet. These
tests read what make decides, without building: the recursion it runs for a
candidate, and its refusal of a candidate that is not six fields, names no
board, or is already a row. The rows are literals, as the caller types them.
"""

import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

CANDIDATES = (
    "atmega32u4:atecc608a:atecc608a:atecc608a:0:1",
)


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


class BootCandidateTarget(unittest.TestCase):
    def test_the_recursion_builds_and_sizes_the_candidate_row(self):
        for row in CANDIDATES:
            board = row.split(":")[0]
            with self.subTest(row=row):
                rc, out = make("bootloader-candidate", "BOOT_CANDIDATE=" + row, "MAKE=echo")
                self.assertEqual(rc, 0, out)
                words = out.split()
                for w in ("boot-size-check", "boot-load-end-check", "BOARD=" + board,
                          "BOOT_TIER=atecc608a", "BOOT_MEASURE=atecc608a",
                          "BOOT_SIG=atecc608a", "BOOT_OTA=0", "BOOT_STORE=1"):
                    self.assertIn(w, words)
                self.assertNotIn("bootloader", words)

    def test_the_candidates_are_not_rows_yet(self):
        rc, out = make("bootloader-candidate", "BOOT_CANDIDATE=" + CANDIDATES[0], "MAKE=echo")
        self.assertNotIn("already in BOOT_SUPPORTED", out)
        self.assertIn("not a row until it is in BOOT_SUPPORTED", out)

    def test_an_unknown_board_is_refused_before_anything_is_built(self):
        rc, out = make("bootloader-candidate",
                       "BOOT_CANDIDATE=atmega9999:atecc608a:atecc608a:atecc608a:0:1",
                       "MAKE=echo")
        self.assertNotEqual(rc, 0, out)
        self.assertIn('no such board "atmega9999"', out)
        self.assertNotIn("boot-size-check", out)

    def test_a_candidate_without_six_fields_is_refused(self):
        for row in ("", "atmega328p", "atmega328p:atecc608a:atecc608a:atecc608a:0",
                    "atmega328p:atecc608a:atecc608a:atecc608a:0:1:1"):
            with self.subTest(row=row):
                rc, out = make("bootloader-candidate", "BOOT_CANDIDATE=" + row, "MAKE=echo")
                self.assertNotEqual(rc, 0, out)
                self.assertIn("is not a row. Name six fields", out)
                self.assertNotIn("boot-size-check", out)

    def test_a_supported_row_is_refused(self):
        # The 2560's store row is one; the 328P's does not fit its boot section.
        for row in ("atmega2560:atecc608a:atecc608a:atecc608a:0:1",):
            with self.subTest(row=row):
                rc, out = make("bootloader-candidate", "BOOT_CANDIDATE=" + row, "MAKE=echo")
                self.assertNotEqual(rc, 0, out)
                self.assertIn(row + " is already in BOOT_SUPPORTED", out)
                self.assertNotIn("boot-size-check", out)


if __name__ == "__main__":
    unittest.main()
