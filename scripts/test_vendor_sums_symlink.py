#!/usr/bin/env python3
"""A symlink to a directory inside a vendored tree, for scripts/vendor_sums.py.

os.walk does not descend into a symlink to a directory and does not list it as
a file, so the check must count it itself: unlisted, it is reported as not in
SHA256SUMS; listed, it cannot be summed and the check exits 1. Either way make
lwip-check fails closed on it. VENDOR_SUMS_SCRIPT points the tests at another
copy of the script, as in test_vendor_sums_edges.py.
"""

import hashlib
import importlib.util
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("VENDOR_SUMS_SCRIPT", os.path.join(REPO, "scripts", "vendor_sums.py"))

_spec = importlib.util.spec_from_file_location("vendor_sums_under_test", SCRIPT)
vendor_sums = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vendor_sums)


def write(root, rel, data):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


class SymlinkToDirectory(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = os.path.join(self._tmp.name, "tree")
        elsewhere = os.path.join(self._tmp.name, "elsewhere")
        write(self.root, "src/a.c", b"int a;\n")
        write(elsewhere, "evil.c", b"int evil;\n")
        os.symlink(elsewhere, os.path.join(self.root, "extra"))
        self.sums = hashlib.sha256(b"int a;\n").hexdigest() + "  src/a.c\n"

    def tearDown(self):
        self._tmp.cleanup()

    def write_sums(self, text):
        with open(os.path.join(self.root, "SHA256SUMS"), "w", encoding="utf-8") as f:
            f.write(text)

    def test_an_unlisted_symlink_to_a_directory_is_reported(self):
        self.write_sums(self.sums)
        self.assertIn("not in SHA256SUMS: extra", vendor_sums.check(self.root))
        self.assertEqual(vendor_sums.main(["vendor_sums.py", self.root]), 1)

    def test_a_listed_symlink_to_a_directory_still_fails(self):
        self.write_sums(self.sums + "0" * 64 + "  extra\n")
        # It is there, so not "missing: extra": summing a directory is an OSError.
        with self.assertRaises(OSError):
            vendor_sums.check(self.root)
        self.assertEqual(vendor_sums.main(["vendor_sums.py", self.root]), 1)


if __name__ == "__main__":
    unittest.main()
