# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for make integration-test-boot-prev (make scripts-test).

The target runs the simavr harness's prev row for the board the caller names.
These tests read what make decides, without building: which half of the
harness names PREV, the recursion the target runs, and the refusal of a board
with no PREV. The part each board is comes from its profile, so a board here
is named only as the caller would name it.
"""

import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

PREV = "test/integration/simavr/boot_harness_prev.c"
PREV_NONE = "test/integration/simavr/boot_harness_prev_none.c"


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


class BootPrevTarget(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fd, cls.print_mk = tempfile.mkstemp(suffix=".mk")
        with os.fdopen(fd, "w") as f:
            f.write("print-%:\n\t@echo '$($*)'\n")

    @classmethod
    def tearDownClass(cls):
        os.unlink(cls.print_mk)

    def var(self, board, name):
        rc, out = make("-f", "Makefile", "-f", self.print_mk, "print-" + name,
                       "BOARD=" + board)
        self.assertEqual(rc, 0, out)
        return out.strip().splitlines()[-1] if out.strip() else ""

    def test_a_one_slot_board_with_a_store_and_a_prev_links_prev(self):
        for board in ("atmega32u4",):
            with self.subTest(board=board):
                self.assertEqual(self.var(board, "BOOT_INT_PREV_SRC"), PREV)

    def test_a_two_slot_board_or_one_without_a_store_or_a_prev_links_the_stub(self):
        # The 328P has a store and one slot and no boot-time rollback (owner
        # queue 176, option (a)): BOOT_PREV=0 in its profile.
        for board in ("atmega2560", "attiny85", "atmega328p"):
            with self.subTest(board=board):
                self.assertEqual(self.var(board, "BOOT_INT_PREV_SRC"), PREV_NONE)

    def test_the_recursion_names_the_callers_board_and_the_prev_row(self):
        for board in ("atmega32u4",):
            with self.subTest(board=board):
                rc, out = make("integration-test-boot-prev", "BOARD=" + board, "MAKE=echo")
                self.assertEqual(rc, 0, out)
                words = out.split()
                self.assertIn("boot-harness-run", words)
                self.assertIn("BOARD=" + board, words)
                self.assertIn("BOOT_STORE=1", words)
                self.assertIn("BOOT_HARNESS_MODE=prev", words)

    def test_boot_prev_names_the_build_directory(self):
        # BOOT_PREV changes the linked source list, so the two values must not
        # share a directory: make would not relink an ELF built with the other.
        dirs = {}
        for prev in ("0", "1"):
            rc, out = make("-f", "Makefile", "-f", self.print_mk, "print-BOOT_DIR_OUT",
                           "BOARD=atmega32u4", "BOOT_STORE=1", "BOOT_PREV=" + prev)
            self.assertEqual(rc, 0, out)
            dirs[prev] = out.strip().splitlines()[-1]
        self.assertNotEqual(dirs["0"], dirs["1"])

    def test_a_board_without_prev_is_refused_before_anything_is_built(self):
        for board, why in (("atmega2560", "BOOT_AB=1"), ("attiny85", "no OTA_STORE"),
                           ("atmega328p", "BOOT_PREV=0")):
            with self.subTest(board=board):
                rc, out = make("integration-test-boot-prev", "BOARD=" + board, "MAKE=echo")
                self.assertNotEqual(rc, 0, out)
                self.assertIn(board + " has no PREV in a store", out)
                self.assertIn(why, out)
                self.assertNotIn("boot-harness-run", out)


if __name__ == "__main__":
    unittest.main()
