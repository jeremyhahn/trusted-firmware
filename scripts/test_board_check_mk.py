# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/board-check.py's read_mk and mk_int (make scripts-test). No network."""

import importlib.util
import os
import shutil
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
# BOARD_CHECK_PY points the suite at a scratch copy of the script (the mutation run only).
SCRIPT = os.environ.get("BOARD_CHECK_PY", os.path.join(HERE, "board-check.py"))
spec = importlib.util.spec_from_file_location("board_check_mk", SCRIPT)
bc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bc)

PROFILE_MK = (
    "A := 1\n"
    "A := 2\n"
    "B ?= 0x7000 # flash\n"
    "C=  x y  \n"
    "D += 3\n"
    "\tE := 4\n"
    "export F := 5\n"
    "# G := 6\n"
    "H := 7#c\n"
    "I:=\n"
)


class ReadMkTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)

    def test_assignments(self):
        path = os.path.join(self.tmp, "profile.mk")
        with open(path, "w") as f:
            f.write(PROFILE_MK)
        self.assertEqual(bc.read_mk(path),
                         {"A": "1", "B": "0x7000", "C": "x y", "H": "7", "I": ""})


class MkIntTest(unittest.TestCase):
    def test_numbers(self):
        for value, want in (("0x7000", 28672), ("4096", 4096), ("0o17", 15)):
            fails = []
            self.assertEqual(bc.mk_int({"X": value}, "X", fails.append), want, value)
            self.assertEqual(fails, [], value)

    def test_not_numbers(self):
        for value in ("010", "32K", ""):
            fails = []
            self.assertIsNone(bc.mk_int({"X": value}, "X", fails.append), value)
            self.assertEqual(fails, ["profile.mk X is %r, not a number" % value], value)


if __name__ == "__main__":
    unittest.main()
