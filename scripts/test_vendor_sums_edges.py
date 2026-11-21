#!/usr/bin/env python3
"""Unit tests for scripts/vendor_sums.py's edges (make scripts-test).

test_vendor_sums.py checks the real lwIP tree. These cases build small trees in
a temp directory instead: VERSION, README.tfw.md and SHA256SUMS are ours only at
the top of the tree (the same name deeper down is an upstream file the list must
name), a `./` prefix and blank lines in the list are read, and the command line
exits 2 on bad usage, 1 on a missing list or a changed byte, and 0 with its match
line. VENDOR_SUMS_SCRIPT points the tests at another copy of the script (for a
mutation run only). No network.
"""

import hashlib
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("VENDOR_SUMS_SCRIPT", os.path.join(REPO, "scripts", "vendor_sums.py"))

_spec = importlib.util.spec_from_file_location("vendor_sums_under_test", SCRIPT)
vendor_sums = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vendor_sums)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write(root, rel, data):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def write_sums(root, text):
    with open(os.path.join(root, "SHA256SUMS"), "w", encoding="utf-8") as f:
        f.write(text)


class Tree(unittest.TestCase):
    """A tree of upstream files plus our own top-level VERSION and README."""

    FILES = {"a.c": b"int a;\n", "src/b.h": b"#define B 1\n"}

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        for rel, data in self.FILES.items():
            write(self.root, rel, data)
        write(self.root, "VERSION", b"STABLE-2_2_0_RELEASE\n")
        write(self.root, "README.tfw.md", b"# ours\n")
        write_sums(self.root, "".join(f"{digest(d)}  {n}\n" for n, d in self.FILES.items()))

    def tearDown(self):
        self._tmp.cleanup()


class OursAtTheTopOnly(Tree):
    def test_a_fully_listed_tree_passes(self):
        self.assertEqual(vendor_sums.check(self.root), [])

    def test_a_nested_version_must_be_listed(self):
        write(self.root, "sub/VERSION", b"1\n")
        self.assertEqual(vendor_sums.check(self.root), ["not in SHA256SUMS: sub/VERSION"])

    def test_a_nested_readme_must_be_listed(self):
        write(self.root, "src/README.tfw.md", b"x\n")
        self.assertEqual(vendor_sums.check(self.root), ["not in SHA256SUMS: src/README.tfw.md"])

    def test_the_top_level_files_are_never_reported(self):
        files = vendor_sums.tree_files(self.root)
        self.assertEqual(files, {"a.c", "src/b.h"})


class ReadSums(Tree):
    def test_dot_slash_and_blank_lines_are_read(self):
        a = digest(self.FILES["a.c"])
        b = digest(self.FILES["src/b.h"])
        write_sums(self.root, f"\n{a}  ./a.c\n\n\n{b}  src/b.h\n\n")
        sums = vendor_sums.read_sums(os.path.join(self.root, "SHA256SUMS"))
        self.assertEqual(sums, {"a.c": a, "src/b.h": b})
        self.assertEqual(vendor_sums.check(self.root), [])

    def test_one_space_is_not_a_sha256sum_line(self):
        a = digest(self.FILES["a.c"])
        write_sums(self.root, f"{a}  a.c\n\n{a} src/b.h\n")
        path = os.path.join(self.root, "SHA256SUMS")
        with self.assertRaisesRegex(ValueError, f"^{re.escape(path)}:3: not a sha256sum line$"):
            vendor_sums.read_sums(path)

    def test_a_short_digest_is_not_a_sha256sum_line(self):
        a = digest(self.FILES["a.c"])
        write_sums(self.root, f"{a[:63]}  a.c\n")
        path = os.path.join(self.root, "SHA256SUMS")
        with self.assertRaisesRegex(ValueError, f"^{re.escape(path)}:1: not a sha256sum line$"):
            vendor_sums.read_sums(path)


class CommandLine(Tree):
    def run_script(self, *args):
        return subprocess.run(
            [sys.executable, "-B", SCRIPT, *args],
            capture_output=True, text=True, check=False,
        )

    def test_no_argument_is_usage(self):
        r = self.run_script()
        self.assertEqual(r.returncode, 2)
        self.assertIn("vendor_sums.py <dir>", r.stderr)
        self.assertEqual(r.stdout, "")

    def test_two_arguments_are_usage(self):
        r = self.run_script(self.root, self.root)
        self.assertEqual(r.returncode, 2)
        self.assertIn("vendor_sums.py <dir>", r.stderr)

    def test_a_missing_list_exits_1(self):
        os.remove(os.path.join(self.root, "SHA256SUMS"))
        r = self.run_script(self.root)
        self.assertEqual(r.returncode, 1)
        self.assertTrue(r.stderr.startswith("vendor-sums: "), r.stderr)
        self.assertEqual(r.stdout, "")

    def test_a_matching_tree_exits_0(self):
        r = self.run_script(self.root)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, f"vendor-sums: {self.root}: every file matches SHA256SUMS\n")
        self.assertEqual(r.stderr, "")

    def test_a_changed_byte_exits_1(self):
        write(self.root, "a.c", b"int b;\n")
        r = self.run_script(self.root)
        self.assertEqual(r.returncode, 1)
        self.assertEqual(r.stderr, f"vendor-sums: {self.root}: differs from upstream: a.c\n")
        self.assertEqual(r.stdout, "")


if __name__ == "__main__":
    unittest.main()
