# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/optee_run.py's guest line (make scripts-test).

guest_line builds the one line typed at the guest's shell: it checks
tee-supplicant is running and the share is a 9p mount it can enter, runs the
command in a subshell there, and prints the command's status, or why it
refused, as a marker line. The text is checked first: one line ending in a
carriage return, and the echoed line never matches a marker. Then the line runs
under the host's sh in a temp directory, with stand-in pidof and grep first on
PATH, and what it prints is matched with optee_run's own patterns. No QEMU.

OPTEE_RUN_SCRIPT names another copy of the script to test (a mutation run).
"""

import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("OPTEE_RUN_SCRIPT") or os.path.join(REPO, "scripts", "optee_run.py")

spec = importlib.util.spec_from_file_location("optee_run", SCRIPT)
optee_run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(optee_run)


class TestText(unittest.TestCase):
    def setUp(self):
        self.line = optee_run.guest_line("xtest -l 0", "/mnt/host")

    def test_it_is_one_line_ending_in_a_carriage_return(self):
        self.assertTrue(self.line.endswith("\r"))
        self.assertEqual(self.line.count("\r"), 1)
        self.assertNotIn("\n", self.line)

    def test_it_sets_the_library_path_runs_the_command_in_a_subshell_and_checks_the_share(self):
        self.assertTrue(self.line.startswith("export LD_LIBRARY_PATH=/lib:/lib/arm-linux-gnueabihf; "))
        self.assertIn("( xtest -l 0 )", self.line)
        self.assertIn("grep -q ' /mnt/host 9p ' /proc/mounts", self.line)

    def test_the_echoed_line_matches_no_marker(self):
        typed = self.line.encode()
        self.assertIsNone(optee_run.STATUS.search(typed))
        self.assertIsNone(optee_run.ERROR.search(typed))


class TestUnderSh(unittest.TestCase):
    def setUp(self):
        self.dir = os.path.realpath(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.bin = os.path.join(self.dir, "bin")
        self.share = os.path.join(self.dir, "share")
        os.mkdir(self.bin)
        os.mkdir(self.share)

    def stub(self, name, status):
        path = os.path.join(self.bin, name)
        with open(path, "w") as f:
            f.write(f"#!/bin/sh\nexit {status}\n")
        os.chmod(path, 0o755)

    def run_line(self, cmd, mount=None, pidof=0, grep=0):
        self.stub("pidof", pidof)
        self.stub("grep", grep)
        line = optee_run.guest_line(cmd, mount or self.share)
        env = dict(os.environ, PATH=self.bin + os.pathsep + os.environ.get("PATH", ""))
        return subprocess.run(["sh", "-c", line.rstrip("\r")], env=env, cwd=self.dir,
                              capture_output=True, text=True, timeout=30)

    def test_the_command_status_comes_back_from_its_subshell(self):
        got = self.run_line("exit 3")
        self.assertEqual(got.stdout, "TFW-GUEST-STATUS=3\n", got.stderr)
        m = optee_run.STATUS.search(got.stdout.encode())
        self.assertEqual(m.group(1), b"3")

    def test_the_command_runs_in_the_share(self):
        got = self.run_line("pwd")
        self.assertEqual(got.stdout, self.share + "\nTFW-GUEST-STATUS=0\n", got.stderr)

    def test_no_tee_supplicant_refuses_and_never_runs_the_command(self):
        ran = os.path.join(self.dir, "ran")
        got = self.run_line(f": > {ran}", pidof=1)
        self.assertEqual(got.stdout, "TFW-GUEST-ERROR=no-tee-supplicant\n", got.stderr)
        self.assertFalse(os.path.exists(ran))
        m = optee_run.ERROR.search(got.stdout.encode())
        self.assertEqual(m.group(1), b"no-tee-supplicant")

    def test_a_share_that_is_not_mounted_or_cannot_be_entered_refuses(self):
        missing = os.path.join(self.dir, "missing")
        for mount, grep in ((self.share, 1), (missing, 0)):
            with self.subTest(mount=mount, grep=grep):
                ran = os.path.join(self.dir, "ran")
                got = self.run_line(f": > {ran}", mount=mount, grep=grep)
                self.assertEqual(got.stdout, "TFW-GUEST-ERROR=no-share\n", got.stderr)
                self.assertFalse(os.path.exists(ran))
                m = optee_run.ERROR.search(got.stdout.encode())
                self.assertEqual(m.group(1), b"no-share")


if __name__ == "__main__":
    unittest.main()
