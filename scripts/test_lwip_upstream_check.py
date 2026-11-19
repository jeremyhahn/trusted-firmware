"""lwip_upstream_check.py against a local fake upstream: a match passes, one
changed byte, a file upstream lacks, or a commit upstream lacks fails, and an
unreachable upstream skips with its reason instead of passing."""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
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

    def archive(self, kind, crlf=False):
        """Packs the upstream tree under one top directory, as a release
        archive does; returns its file:// URL and SHA-256."""
        out = os.path.join(self.tmp, "release." + kind)
        top = "lwip-STABLE-9_9_9_RELEASE/"
        names = []
        for d, _, fs in os.walk(self.up):
            if ".git" in d.split(os.sep):
                continue
            names += [os.path.join(d, f) for f in fs]
        if kind == "zip":
            with zipfile.ZipFile(out, "w") as z:
                for n in names:
                    with open(n, "rb") as f:
                        data = f.read()
                    z.writestr(top + os.path.relpath(n, self.up),
                               data.replace(b"\n", b"\r\n") if crlf else data)
        else:
            with tarfile.open(out, "w:gz") as t:
                for n in names:
                    t.add(n, top + os.path.relpath(n, self.up))
        with open(out, "rb") as f:
            return "file://" + out, hashlib.sha256(f.read()).hexdigest()

    def check_archive(self, url, sha):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = luc.main(["x", "--archive", self.vend, url, sha])
        return rc, out.getvalue()

    def test_a_byte_identical_release_tarball_passes_with_its_sha256(self):
        url, sha = self.archive("tar.gz")
        rc, out = self.check_archive(url, sha.upper())
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS: 4 vendored files", out)
        self.assertIn(sha + ", matched", out)

    def test_a_byte_identical_release_zip_passes(self):
        url, sha = self.archive("zip")
        rc, out = self.check_archive(url, sha)
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS: 4 vendored files", out)

    def test_an_archive_with_another_sha256_fails_and_compares_nothing(self):
        url, sha = self.archive("tar.gz")
        rc, out = self.check_archive(url, "0" * 64)
        self.assertEqual(rc, 1, out)
        self.assertIn("FAIL: %s has SHA-256 %s" % (url, sha), out)
        self.assertNotIn("PASS", out)

    def test_one_changed_byte_against_the_archive_fails_naming_the_file(self):
        url, sha = self.archive("tar.gz")
        write(os.path.join(self.vend, "src/core/a.c"), b"int b;\n")
        rc, out = self.check_archive(url, sha)
        self.assertEqual(rc, 1, out)
        self.assertIn("src/core/a.c: differs from the archive at byte 4\n", out)
        self.assertIn("FAIL: 1 of 4", out)

    def test_an_archive_with_crlf_line_endings_is_not_byte_identical(self):
        url, sha = self.archive("zip", crlf=True)
        rc, out = self.check_archive(url, sha)
        self.assertEqual(rc, 1, out)
        self.assertIn("(line endings only)", out)
        self.assertIn("FAIL: 4 of 4", out)

    def test_an_unreachable_archive_skips_with_its_reason(self):
        rc, out = self.check_archive("file://" + os.path.join(self.tmp, "nowhere.tar.gz"), "0" * 64)
        self.assertEqual(rc, 0, out)
        self.assertIn("SKIP (no network:", out)
        self.assertNotIn("PASS", out)


if __name__ == "__main__":
    unittest.main()
