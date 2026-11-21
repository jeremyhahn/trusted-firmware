# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/lwip_upstream_check.py's helpers (make scripts-test).

test_lwip_upstream_check.py drives main end to end and sees PASS, FAIL and
SKIP; these pin the exact answers underneath: the vendored file list and its
order, the byte offset a difference names, the line-endings note, the usage
exit and the upper-case SHA-256 archive mode accepts. No network: the archive
is a file:// tarball built here. LWIP_UPSTREAM_CHECK names another copy of the
script, for a mutation run only.
"""

import contextlib
import hashlib
import importlib.util
import io
import os
import shutil
import tarfile
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("LWIP_UPSTREAM_CHECK",
                        os.path.join(REPO, "scripts", "lwip_upstream_check.py"))
USAGE = "lwip_upstream_check.py <vendored dir> <upstream url> <commit>\n"


def load():
    spec = importlib.util.spec_from_file_location("lwip_upstream_check", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


check = load()


def write(root, rel, data):
    path = os.path.join(root, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


class TempTree(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.root = os.path.join(self.tmp, "vendored")
        os.mkdir(self.root)

    def tearDown(self):
        shutil.rmtree(self.tmp)


class TestVendoredFiles(TempTree):
    def test_top_files_first_then_src_sorted_and_nothing_else(self):
        for rel in ("COPYING", "README", "src/b/z.c", "src/a.c", "src/b/a.h", "contrib/x.c"):
            write(self.root, rel, b"x")
        self.assertEqual(check.vendored_files(self.root),
                         ["COPYING", "README", "src/a.c", "src/b/a.h", "src/b/z.c"])


class TestCompare(TempTree):
    def compare(self, mine, theirs, where="upstream"):
        if mine is not None:
            write(self.root, "a.c", mine)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            bad = check.compare(self.root, ["a.c"], {"a.c": theirs}, where)
        self.assertEqual(out.getvalue(), "".join("lwip-upstream-check: %s\n" % b for b in bad))
        return bad

    def test_equal_bytes_are_no_difference(self):
        self.assertEqual(self.compare(b"abc", b"abc"), [])

    def test_a_difference_names_its_first_differing_byte(self):
        self.assertEqual(self.compare(b"abcX", b"abcY"), ["a.c: differs from upstream at byte 3"])

    def test_a_prefix_differs_at_the_shorter_length(self):
        for mine, theirs in ((b"abc", b"abcdef"), (b"abcdef", b"abc")):
            with self.subTest(mine=mine, theirs=theirs):
                self.assertEqual(self.compare(mine, theirs),
                                 ["a.c: differs from upstream at byte 3"])

    def test_line_endings_are_named_only_when_they_are_the_whole_difference(self):
        self.assertEqual(self.compare(b"a\r\nb\r\n", b"a\nb\n"),
                         ["a.c: differs from upstream at byte 1 (line endings only)"])
        self.assertEqual(self.compare(b"a\r\nc\r\n", b"a\nb\n"),
                         ["a.c: differs from upstream at byte 1"])

    def test_a_path_with_no_local_file_is_missing_from_the_vendored_tree(self):
        self.assertEqual(self.compare(None, b"abc"), ["a.c: missing from the vendored tree"])

    def test_a_path_with_no_blob_is_not_in_where(self):
        self.assertEqual(self.compare(b"abc", None, "the archive"), ["a.c: not in the archive"])


class TestMain(TempTree):
    def main(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = check.main(argv)
        return rc, out.getvalue(), err.getvalue()

    def test_a_wrong_argument_count_prints_the_usage_line_and_exits_2(self):
        for argv in (["x"], ["x", "a"], ["x", "a", "b", "c", "d", "e"]):
            with self.subTest(argv=argv):
                rc, out, err = self.main(argv)
                self.assertEqual(rc, 2)
                self.assertEqual(out, "")
                self.assertEqual(err, USAGE)

    def test_archive_mode_takes_an_upper_case_sha256(self):
        files = {"COPYING": b"copying\n", "README": b"readme\n", "src/a.c": b"int a;\n"}
        top = "lwip-STABLE-9_9_9_RELEASE/"
        for rel, data in files.items():
            write(self.root, rel, data)
        tar = os.path.join(self.tmp, "lwip.tar.gz")
        with tarfile.open(tar, "w:gz") as t:
            for rel, data in files.items():
                info = tarfile.TarInfo(top + rel)
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
        with open(tar, "rb") as f:
            sha = hashlib.sha256(f.read()).hexdigest()
        rc, out, err = self.main(["x", "--archive", self.root, "file://" + tar, sha.upper()])
        self.assertEqual(rc, 0, out + err)
        self.assertIn("lwip-upstream-check: PASS: 3 vendored files", out)


if __name__ == "__main__":
    unittest.main()
