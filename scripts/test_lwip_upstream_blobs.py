"""lwip_upstream_check.upstream_blobs on its own, against a local repository:
an empty upstream file is b"" and not None, multi-line and binary blobs come
back whole and in the asked order, a path or a commit upstream lacks is None,
and a repository that is not there raises RuntimeError. Needs git, no network."""
import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
# The mutation run points this at a scratch copy of the script.
SCRIPT = os.environ.get("LWIP_UPSTREAM_CHECK", os.path.join(HERE, "lwip_upstream_check.py"))
spec = importlib.util.spec_from_file_location("lwip_upstream_check", SCRIPT)
luc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(luc)

GIT = ["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid",
       "-c", "commit.gpgsign=false", "-c", "init.defaultBranch=master"]

FILES = {"src/e.c": b"", "src/n.c": b"a\nb\n\n", "src/z.bin": b"\x00\n\xff",
         "COPYING": b"licence\n"}


class UpstreamBlobsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.repo = os.path.join(self.tmp, "upstream")
        for p, d in FILES.items():
            path = os.path.join(self.repo, p)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(d)
        run = lambda *a: subprocess.run(GIT + ["-C", self.repo, *a], check=True,
                                        capture_output=True)
        run("init", "-q")
        run("add", "-A")
        run("commit", "-q", "-m", "upstream")
        self.commit = run("rev-parse", "HEAD").stdout.decode().strip()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_empty_multiline_binary_and_missing_paths(self):
        paths = ["src/gone.c", "src/e.c", "src/n.c", "src/z.bin", "COPYING", "nope/x"]
        got = luc.upstream_blobs(self.repo, self.commit, paths)
        self.assertEqual(got, {"src/gone.c": None, "src/e.c": b"", "src/n.c": b"a\nb\n\n",
                               "src/z.bin": b"\x00\n\xff", "COPYING": b"licence\n",
                               "nope/x": None})
        self.assertEqual(list(got), paths)

    def test_commit_upstream_lacks_is_none(self):
        self.assertEqual(luc.upstream_blobs(self.repo, "0" * 40, ["COPYING"]),
                         {"COPYING": None})

    def test_repository_not_there_raises(self):
        with self.assertRaises(RuntimeError) as cm:
            luc.upstream_blobs(os.path.join(self.tmp, "absent"), self.commit, ["COPYING"])
        self.assertIn("cannot change to", str(cm.exception))

    def test_no_paths_is_empty(self):
        self.assertEqual(luc.upstream_blobs(self.repo, self.commit, []), {})


if __name__ == "__main__":
    unittest.main()
