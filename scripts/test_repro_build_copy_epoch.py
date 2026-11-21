# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# scripts/repro_build.py's commit_epoch and clean_copy on their own, on a
# throwaway git repository: the epoch is HEAD's committer time as a digit
# string, and the copy holds the committed bytes only. Needs git and tar, no
# make, no build, no network. REPRO_BUILD_SCRIPT overrides the script path (for
# a mutation run against a scratch copy).

import importlib.util
import os
import subprocess
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.environ.get("REPRO_BUILD_SCRIPT", os.path.join(HERE, "repro_build.py"))

spec = importlib.util.spec_from_file_location("repro_build", SCRIPT)
rb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rb)

COMMIT_TIME = "1700000000"
GIT_ENV = {
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_AUTHOR_NAME": "Test",
    "GIT_AUTHOR_EMAIL": "test@example.com",
    "GIT_COMMITTER_NAME": "Test",
    "GIT_COMMITTER_EMAIL": "test@example.com",
    "GIT_AUTHOR_DATE": f"@{COMMIT_TIME} +0000",
    "GIT_COMMITTER_DATE": f"@{COMMIT_TIME} +0000",
}


def git(repo, *args):
    subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def files_in(tree):
    return sorted(os.path.relpath(os.path.join(d, f), tree)
                  for d, _, fs in os.walk(tree) for f in fs)


class CopyEpochTest(unittest.TestCase):
    def setUp(self):
        env = mock.patch.dict(os.environ, GIT_ENV)
        env.start()
        self.addCleanup(env.stop)
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = tmp.name
        self.repo = os.path.join(self.tmp, "repo")
        os.makedirs(self.repo)
        git(self.repo, "init", "-q")
        write(os.path.join(self.repo, "a.txt"), "one\n")
        write(os.path.join(self.repo, "sub", "b.txt"), "two\n")
        git(self.repo, "add", "a.txt", "sub/b.txt")
        git(self.repo, "commit", "-q", "-m", "init")

    def test_epoch_is_committer_time_digits(self):
        self.assertEqual(rb.commit_epoch(self.repo), COMMIT_TIME)

    def test_epoch_of_empty_repository_fails(self):
        empty = os.path.join(self.tmp, "empty")
        os.makedirs(empty)
        git(empty, "init", "-q")
        with self.assertRaises(subprocess.CalledProcessError):
            rb.commit_epoch(empty)

    def test_copy_holds_committed_bytes_only(self):
        write(os.path.join(self.repo, "a.txt"), "edited\n")
        write(os.path.join(self.repo, "untracked.txt"), "x\n")
        dst = os.path.join(self.tmp, "dst")
        write(os.path.join(dst, "stale"), "old\n")
        rb.clean_copy(self.repo, dst)
        self.assertEqual(files_in(dst), ["a.txt", "sub/b.txt"])
        with open(os.path.join(dst, "a.txt")) as f:
            self.assertEqual(f.read(), "one\n")

    def test_copy_makes_missing_target_and_parents(self):
        dst = os.path.join(self.tmp, "new", "deep")
        rb.clean_copy(self.repo, dst)
        self.assertEqual(files_in(dst), ["a.txt", "sub/b.txt"])


if __name__ == "__main__":
    unittest.main()
