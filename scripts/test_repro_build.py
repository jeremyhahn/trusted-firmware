# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# scripts/repro_build.py is what `make reproducible-check` and `make
# bootloader-reproducible` build with. These tests run it on a throwaway git
# repository with a stand-in for make, so they need no AVR toolchain: the
# stand-in writes, as its "image", what it found in the tree it was given and
# the SOURCE_DATE_EPOCH it was run with.

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "repro_build.py")

COMMIT_TIME = "1700000000"
IMAGE = "build/fw.hex"

# Writes IMAGE in the tree -C names: the sorted file list, the epoch, and the
# tree's own path when the first make argument is "with-path".
FAKE_MAKE = f"""#!{sys.executable}
import os, sys
tree = sys.argv[sys.argv.index("-C") + 1]
args = sys.argv[sys.argv.index("--no-print-directory") + 1:]
files = sorted(os.path.relpath(os.path.join(d, f), tree)
               for d, _, fs in os.walk(tree) for f in fs)
out = os.path.join(tree, {IMAGE!r})
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w") as f:
    f.write("files=%s\\n" % ",".join(files))
    f.write("epoch=%s\\n" % os.environ.get("SOURCE_DATE_EPOCH", "<unset>"))
    if args[:1] == ["with-path"]:
        f.write("tree=%s\\n" % tree)
"""


def git(repo, *args):
    env = dict(os.environ, GIT_AUTHOR_DATE=f"@{COMMIT_TIME} +0000",
               GIT_COMMITTER_DATE=f"@{COMMIT_TIME} +0000")
    subprocess.run(["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@t",
                    "-c", "commit.gpgsign=false", "-c", "init.defaultBranch=main"] + list(args),
                   env=env, check=True, capture_output=True)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


class ReproBuild(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="tfw-repro-build-")
        self.addCleanup(self.tmp.cleanup)
        t = self.tmp.name
        self.src = os.path.join(t, "src")
        os.makedirs(self.src)
        git(self.src, "init", "-q")
        write(os.path.join(self.src, "Makefile"), "all:\n")
        write(os.path.join(self.src, "src", "main.c"), "int main(void) { return 0; }\n")
        git(self.src, "add", "-A")
        git(self.src, "commit", "-q", "-m", "source")
        # What a previous build and a working session leave in the tree.
        write(os.path.join(self.src, IMAGE), "STALE IMAGE\n")
        write(os.path.join(self.src, "build", "obj", "main.o"), "stale object\n")
        write(os.path.join(self.src, "untracked.c"), "int stray;\n")
        write(os.path.join(self.src, "src", "main.c"), "int main(void) { return 1; }\n")
        self.make = os.path.join(t, "fake-make")
        write(self.make, FAKE_MAKE)
        os.chmod(self.make, 0o755)
        self.a = os.path.join(t, "out-a")
        self.b = os.path.join(t, "out", "longer", "b")

    def run_script(self, *make_args, env=None):
        return subprocess.run([sys.executable, SCRIPT, "--src", self.src, "--make", self.make,
                               "--image", IMAGE, "--dir", self.a, "--dir", self.b, "--"]
                              + list(make_args),
                              env=env, capture_output=True, text=True, check=False)

    def image(self, d):
        with open(os.path.join(d, IMAGE)) as f:
            return dict(line.split("=", 1) for line in f.read().splitlines())

    def test_a_stale_build_and_untracked_files_do_not_reach_the_copy(self):
        # A stale image already sits where the copy goes, too.
        write(os.path.join(self.a, IMAGE), "STALE IMAGE\n")
        run = self.run_script()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("reproducible:", run.stdout)
        for d in (self.a, self.b):
            self.assertEqual(self.image(d)["files"], "Makefile,src/main.c")
            with open(os.path.join(d, "src", "main.c")) as f:
                self.assertEqual(f.read(), "int main(void) { return 0; }\n",
                                 "the copy is the commit, not the working tree")

    def test_source_date_epoch_is_the_commit_time(self):
        env = dict(os.environ, SOURCE_DATE_EPOCH="1")
        run = self.run_script(env=env)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            self.assertEqual(self.image(d)["epoch"], COMMIT_TIME)
        self.assertIn(f"SOURCE_DATE_EPOCH={COMMIT_TIME}", run.stdout)

    def test_different_images_fail(self):
        run = self.run_script("with-path")
        self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
        self.assertIn("NOT REPRODUCIBLE", run.stdout)

    def test_a_failed_build_fails(self):
        os.remove(self.make)
        write(self.make, "#!/bin/sh\nexit 2\n")
        os.chmod(self.make, 0o755)
        run = self.run_script()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("exited 2", run.stderr)

    def test_one_directory_twice_is_refused(self):
        self.b = self.a
        run = self.run_script()
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
