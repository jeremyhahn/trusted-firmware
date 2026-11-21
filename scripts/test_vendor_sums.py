#!/usr/bin/env python3
"""Tests for vendor_sums.py: the vendored lwIP matches its SHA256SUMS, and a
changed byte, a missing file or an extra file fails the check."""

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vendor_sums  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LWIP = os.path.join(REPO, "third_party", "lwip")


class VendoredLwIP(unittest.TestCase):
    def test_the_tree_matches_its_sums(self):
        self.assertEqual(vendor_sums.check(LWIP), [])

    def test_every_vendored_file_is_listed(self):
        sums = vendor_sums.read_sums(os.path.join(LWIP, "SHA256SUMS"))
        self.assertIn("COPYING", sums)
        self.assertIn("src/core/tcp.c", sums)
        self.assertNotIn("VERSION", sums)


class Check(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root)
        shutil.copytree(LWIP, self.root, dirs_exist_ok=True)

    def test_a_changed_byte_fails(self):
        path = os.path.join(self.root, "src", "core", "tcp.c")
        with open(path, "r+b") as f:
            b = f.read(1)
            f.seek(0)
            f.write(bytes([b[0] ^ 1]))
        self.assertEqual(vendor_sums.check(self.root),
                         ["differs from upstream: src/core/tcp.c"])
        self.assertEqual(vendor_sums.main(["vendor_sums.py", self.root]), 1)

    def test_a_missing_file_fails(self):
        os.remove(os.path.join(self.root, "README"))
        self.assertEqual(vendor_sums.check(self.root), ["missing: README"])

    def test_an_extra_file_fails(self):
        with open(os.path.join(self.root, "src", "core", "extra.c"), "w",
                  encoding="utf-8") as f:
            f.write("int x;\n")
        self.assertEqual(vendor_sums.check(self.root),
                         ["not in SHA256SUMS: src/core/extra.c"])

    def test_a_malformed_line_is_reported(self):
        with open(os.path.join(self.root, "SHA256SUMS"), "a",
                  encoding="utf-8") as f:
            f.write("nonsense\n")
        self.assertEqual(vendor_sums.main(["vendor_sums.py", self.root]), 1)


if __name__ == "__main__":
    unittest.main()
