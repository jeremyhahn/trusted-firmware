"""lwip_upstream_check.py against a local fake upstream: a match passes, one
changed byte, a file upstream lacks, or a commit upstream lacks fails, and an
unreachable upstream skips with its reason instead of passing."""
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwip_upstream_check as luc  # noqa: E402

GIT = ["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid",
       "-c", "commit.gpgsign=false", "-c", "init.defaultBranch=master"]


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


class LwipUpstreamCheckTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.up = os.path.join(self.tmp, "upstream")
        files = {"COPYING": b"licence\n", "README": b"readme\n",
                 "src/core/a.c": b"int a;\n", "src/include/lwip/a.h": b"#define A 1\n",
                 "src/apps/unvendored.c": b"not copied\n", "doc/x.txt": b"doc\n"}
        for p, d in files.items():
            write(os.path.join(self.up, p), d)
        run = lambda *a: subprocess.run(GIT + ["-C", self.up, *a], check=True,
                                        capture_output=True)
        run("init", "-q")
        run("config", "uploadpack.allowAnySHA1InWant", "true")
        run("add", "-A")
        run("commit", "-q", "-m", "upstream")
        self.commit = subprocess.run(["git", "-C", self.up, "rev-parse", "HEAD"],
                                     check=True, capture_output=True).stdout.decode().strip()
        self.url = "file://" + self.up
        self.vend = os.path.join(self.tmp, "vendored")
        for p in ("COPYING", "README", "src/core/a.c", "src/include/lwip/a.h"):
            write(os.path.join(self.vend, p), files[p])
        write(os.path.join(self.vend, "SHA256SUMS"), b"not compared\n")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def check(self, url=None, commit=None):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = luc.main(["x", self.vend, url or self.url, commit or self.commit])
        return rc, out.getvalue()

    def test_a_byte_identical_subset_passes(self):
        rc, out = self.check()
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS: 4 vendored files", out)

    def test_one_changed_byte_fails(self):
        write(os.path.join(self.vend, "src/core/a.c"), b"int b;\n")
        rc, out = self.check()
        self.assertEqual(rc, 1, out)
        self.assertIn("src/core/a.c: differs from upstream at byte 4", out)
        self.assertIn("FAIL", out)

    def test_a_file_upstream_lacks_fails(self):
        write(os.path.join(self.vend, "src/core/extra.c"), b"x\n")
        rc, out = self.check()
        self.assertEqual(rc, 1, out)
        self.assertIn("src/core/extra.c: not in upstream", out)

    def test_a_missing_licence_fails(self):
        os.remove(os.path.join(self.vend, "COPYING"))
        rc, out = self.check()
        self.assertEqual(rc, 1, out)
        self.assertIn("COPYING: missing from the vendored tree", out)

    def test_a_commit_upstream_lacks_fails(self):
        rc, out = self.check(commit="0" * 40)
        self.assertEqual(rc, 1, out)
        self.assertIn("FAIL", out)

    def test_an_unreachable_upstream_skips_with_its_reason(self):
        rc, out = self.check(url="file://" + os.path.join(self.tmp, "nowhere"))
        self.assertEqual(rc, 0, out)
        self.assertIn("SKIP (no network:", out)
        self.assertNotIn("PASS", out)


if __name__ == "__main__":
    unittest.main()
