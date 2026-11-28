# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# scripts/repro_build.py is what `make reproducible-check` and `make
# bootloader-reproducible` build with. These tests run it on a throwaway git
# repository with a stand-in for make, so they need no AVR toolchain: the
# stand-in writes, as its "image", what it found in the tree it was given and
# the SOURCE_DATE_EPOCH it was run with.

import json
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
import json, os, sys
tree = sys.argv[sys.argv.index("-C") + 1]
args = sys.argv[sys.argv.index("--no-print-directory") + 1:]
argv = list(args)
# As in make, the last command-line assignment beats the environment.
epoch = os.environ.get("SOURCE_DATE_EPOCH", "<unset>")
for a in args:
    if a.startswith("SOURCE_DATE_EPOCH="):
        epoch = a.split("=", 1)[1]
last = args[-1] if args else ""
if last.startswith("SOURCE_DATE_EPOCH="):
    args = args[:-1]
files = sorted(os.path.relpath(os.path.join(d, f), tree)
               for d, _, fs in os.walk(tree) for f in fs)
out = os.path.join(tree, {IMAGE!r})
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w") as f:
    f.write("files=%s\\n" % ",".join(files))
    f.write("epoch=%s\\n" % epoch)
    f.write("env_epoch=%s\\n" % os.environ.get("SOURCE_DATE_EPOCH", "<unset>"))
    f.write("last=%s\\n" % last)
    f.write("args=%s\\n" % " ".join(args))
    f.write("argv=%s\\n" % json.dumps(argv))
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


def make_free_env(**extra):
    """os.environ without the calling make's MAKEFLAGS, MFLAGS and MAKELEVEL."""
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
    env.update(extra)
    return env


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
        # `make scripts-test` sets MAKEFLAGS itself: a run that brings no env of
        # its own must not see the calling make's flags or variables.
        if env is None:
            env = make_free_env()
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
        env = make_free_env(SOURCE_DATE_EPOCH="1")
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
        self.assertIn("--dir names two different directories", run.stderr)

    def test_a_symlink_to_the_other_directory_is_refused(self):
        # Two names, one directory: both builds would run at one path, so a
        # path-dependent image would compare equal to itself.
        os.makedirs(self.a)
        self.b = os.path.join(self.tmp.name, "out-a-link")
        os.symlink(self.a, self.b)
        run = self.run_script()
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertIn("--dir names two different directories", run.stderr)

    def test_the_source_tree_as_a_directory_is_refused(self):
        self.a = self.src
        run = self.run_script()
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertIn("is or holds the source tree", run.stderr)
        self.assertTrue(os.path.isdir(os.path.join(self.src, ".git")))
        self.assertTrue(os.path.isfile(os.path.join(self.src, "untracked.c")),
                        "the uncommitted work is still there")

    def test_the_source_tree_as_the_second_directory_is_refused(self):
        self.b = self.src
        run = self.run_script()
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertIn("is or holds the source tree", run.stderr)
        self.assertTrue(os.path.isdir(os.path.join(self.src, ".git")))
        self.assertTrue(os.path.isfile(os.path.join(self.src, "untracked.c")),
                        "the uncommitted work is still there")

    def test_a_symlink_to_the_source_tree_is_refused(self):
        self.a = os.path.join(self.tmp.name, "src-link")
        os.symlink(self.src, self.a)
        run = self.run_script()
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertIn("is or holds the source tree", run.stderr)
        self.assertTrue(os.path.isfile(os.path.join(self.src, "untracked.c")),
                        "the uncommitted work is still there")

    def test_a_directory_holding_the_source_tree_is_refused(self):
        # A clone in a fresh parent, so the parent holds the source and
        # nothing else of the test's own.
        outer = os.path.join(self.tmp.name, "outer")
        subprocess.run(["git", "clone", "-q", self.src, os.path.join(outer, "src")],
                       check=True, capture_output=True)
        self.src = os.path.join(outer, "src")
        self.a = outer
        run = self.run_script()
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertIn("is or holds the source tree", run.stderr)
        self.assertTrue(os.path.isdir(os.path.join(self.src, ".git")))

    def run_app(self, app_dir):
        return subprocess.run([sys.executable, SCRIPT, "--src", self.src, "--make", self.make,
                               "--image", IMAGE, "--dir", self.a, "--dir", self.b,
                               "--app-dir", app_dir],
                              env=make_free_env(), capture_output=True, text=True, check=False)

    def test_an_application_in_the_tree_is_the_committed_one_in_each_copy(self):
        write(os.path.join(self.src, "app", "ex", "app.mk"), "APP_NAME := ex\n")
        git(self.src, "add", "app")
        git(self.src, "commit", "-q", "-m", "app")
        run = self.run_app(os.path.join(self.src, "app", "ex"))
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            self.assertEqual(self.image(d)["args"], "APP_DIR=app/ex")
            self.assertIn("app/ex/app.mk", self.image(d)["files"])

    def test_an_uncommitted_application_in_the_tree_is_refused(self):
        write(os.path.join(self.src, "app", "ex", "app.mk"), "APP_NAME := ex\n")
        run = self.run_app("app/ex")
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("no committed app.mk", run.stdout + run.stderr)

    def test_an_application_outside_the_tree_is_one_directory_for_both(self):
        ext = os.path.join(self.tmp.name, "ext")
        write(os.path.join(ext, "app.mk"), "APP_NAME := ext\n")
        run = self.run_app(ext)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            self.assertEqual(self.image(d)["args"], "APP_DIR=" + os.path.realpath(ext))

    def test_an_application_outside_the_tree_needs_an_app_mk(self):
        run = self.run_app(self.tmp.name)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("has no app.mk", run.stdout + run.stderr)

    # The calling make's command-line variables (#473, #635): GNU make puts
    # them in MAKEFLAGS after " -- ", and the script must pass them on.

    def run_with_makeflags(self, makeflags, *make_args):
        return self.run_script(*make_args, env=make_free_env(MAKEFLAGS=makeflags))

    def test_command_line_variables_of_the_calling_make_reach_both_builds(self):
        run = self.run_with_makeflags(
            "s -j4 --jobserver-auth=fifo:/tmp/x -- BUILD_DIR=build/imx95-imxboot IMX_BOOT=1",
            "fw.hex")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            args = self.image(d)["args"]
            self.assertEqual(args, "BUILD_DIR=build/imx95-imxboot IMX_BOOT=1 fw.hex")
            self.assertNotIn("-j4", args.split())
            self.assertNotIn("--jobserver", args)

    def test_the_explicit_arguments_follow_the_forwarded_ones(self):
        run = self.run_with_makeflags("-- BOARD=a", "BOARD=b")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            self.assertEqual(self.image(d)["args"], "BOARD=a BOARD=b")

    # A command-line variable beats the environment in make: the commit epoch
    # must be the last SOURCE_DATE_EPOCH assignment each build's make gets.
    def assert_commit_epoch_is_pinned_last(self, run):
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            image = self.image(d)
            self.assertEqual(image["epoch"], COMMIT_TIME)
            self.assertEqual(image["env_epoch"], COMMIT_TIME)
            self.assertEqual(image["last"], f"SOURCE_DATE_EPOCH={COMMIT_TIME}")
        self.assertIn(f"SOURCE_DATE_EPOCH={COMMIT_TIME}", run.stdout)

    def test_a_forwarded_source_date_epoch_does_not_replace_the_commit_time(self):
        self.assert_commit_epoch_is_pinned_last(
            self.run_with_makeflags("-- SOURCE_DATE_EPOCH=1", "fw.hex"))

    def test_an_explicit_source_date_epoch_does_not_replace_the_commit_time(self):
        self.assert_commit_epoch_is_pinned_last(self.run_script("SOURCE_DATE_EPOCH=1"))

    def test_a_forwarded_absolute_build_dir_is_refused(self):
        out = os.path.join(self.tmp.name, "shared-build")
        run = self.run_with_makeflags("-- BUILD_DIR=" + out)
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertIn("BUILD_DIR", run.stderr)
        self.assertFalse(os.path.lexists(self.a))
        self.assertFalse(os.path.lexists(self.b))

    def test_a_forwarded_build_dir_that_is_a_make_reference_is_refused(self):
        out = os.path.join(self.tmp.name, "shared-build")
        run = self.run_with_makeflags("-- BUILD_DIR=$(OUT) OUT=" + out)
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertIn("BUILD_DIR", run.stderr)
        self.assertFalse(os.path.lexists(self.a))
        self.assertFalse(os.path.lexists(self.b))

    def test_a_forwarded_build_dir_outside_the_copy_is_refused(self):
        run = self.run_with_makeflags("-- BUILD_DIR=../out")
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertIn("BUILD_DIR", run.stderr)

    def test_jobserver_flags_alone_forward_nothing(self):
        run = self.run_with_makeflags("-j8 --jobserver-auth=3,4")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            self.assertEqual(self.image(d)["args"], "")

    def test_an_escaped_space_stays_in_one_variable(self):
        run = self.run_with_makeflags("-- EXTRA_CFLAGS=-DA\\ -DB V=1")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            image = self.image(d)
            self.assertEqual(image["args"], "EXTRA_CFLAGS=-DA -DB V=1")
            # The joined args line cannot tell "-DA -DB" in one argument
            # from "-DA" and "-DB" in two; the JSON argv can.
            argv = json.loads(image["argv"])
            self.assertNotIn("-DB", argv)
            self.assertEqual(argv, ["EXTRA_CFLAGS=-DA -DB", "V=1",
                                    f"SOURCE_DATE_EPOCH={COMMIT_TIME}"])

    def test_a_forwarded_variable_reference_reaches_the_builds_unexpanded_once(self):
        # `make IMX_BOOT='$(MODE)' MODE=1` puts IMX_BOOT=$$(MODE) in MAKEFLAGS;
        # each clean build must get IMX_BOOT=$(MODE) so its make expands it.
        run = self.run_with_makeflags("-- IMX_BOOT=$$(MODE) MODE=1")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            argv = json.loads(self.image(d)["argv"])
            self.assertEqual(argv, ["IMX_BOOT=$(MODE)", "MODE=1",
                                    f"SOURCE_DATE_EPOCH={COMMIT_TIME}"])

    def test_a_literal_dollar_stays_escaped_for_the_child_make(self):
        # The calling make got V=a$$b (a literal $); MAKEFLAGS doubles each $.
        run = self.run_with_makeflags("-- V=a$$$$b")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for d in (self.a, self.b):
            argv = json.loads(self.image(d)["argv"])
            self.assertEqual(argv[0], "V=a$$b")


if __name__ == "__main__":
    unittest.main()
