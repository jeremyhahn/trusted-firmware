"""lwip_upstream_check.upstream_blobs when a path names a directory upstream:
the tree's body in the cat-file reply is stepped past, so every later path
still gets its own blob (or None). Local repository only, no network."""
import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("lwip_upstream_check", os.path.join(HERE, "lwip_upstream_check.py"))
luc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(luc)

GIT = ["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid",
       "-c", "commit.gpgsign=false", "-c", "init.defaultBranch=master"]


class UpstreamBlobsTreeBodyTest(unittest.TestCase):
    def setUp(self):
        self.repo = tempfile.mkdtemp()
        files = {"src/core/a.c": b"aaa\n", "src/b.c": b"bbb\n", "COPYING": b"licence\n"}
        for p, d in files.items():
            path = os.path.join(self.repo, p)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(d)
        for args in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "upstream"]):
            subprocess.run(GIT + ["-C", self.repo, *args], check=True, capture_output=True)
        self.commit = subprocess.run(["git", "-C", self.repo, "rev-parse", "HEAD"],
                                     check=True, capture_output=True).stdout.decode().strip()

    def tearDown(self):
        shutil.rmtree(self.repo)

    def test_file_after_directory(self):
        got = luc.upstream_blobs(self.repo, self.commit, ["src/core", "COPYING"])
        self.assertEqual(got, {"src/core": None, "COPYING": b"licence\n"})

    def test_files_after_top_directory(self):
        got = luc.upstream_blobs(self.repo, self.commit, ["src", "src/b.c", "src/core/a.c"])
        self.assertEqual(got, {"src": None, "src/b.c": b"bbb\n", "src/core/a.c": b"aaa\n"})

    def test_directory_and_missing_mixed(self):
        got = luc.upstream_blobs(self.repo, self.commit, ["COPYING", "src/core", "nope/x", "src/b.c"])
        self.assertEqual(got, {"COPYING": b"licence\n", "src/core": None, "nope/x": None,
                               "src/b.c": b"bbb\n"})


if __name__ == "__main__":
    unittest.main()
