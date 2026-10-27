# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests for scripts/optee_run.py, the console side of make optee-run
(make scripts-test).

A fake QEMU stands in for the guest: it prints a login prompt and a shell
prompt, echoes what is typed as a terminal does, and answers with the marker
line, a refusal, a panic or nothing. Each step waits on the fake's output or
its exit, never on a clock; the fake and its record live in a temporary
directory the test removes."""

import contextlib
import io
import os
import shlex
import sys
import tempfile
import unittest

import optee_run

FAKE = r'''
import sys

mode, record = sys.argv[1], sys.argv[2]
out = sys.stdout.buffer


def say(b):
    out.write(b)
    out.flush()


def typed():
    line = b""
    while not line.endswith(b"\r"):
        c = sys.stdin.buffer.read(1)
        if not c:
            sys.exit(3)
        line += c
    say(line + b"\n")
    return line


if mode == "eof":
    say(b"NOTICE:  BL1: Booting BL2\n")
    sys.exit(0)
say(b"NOTICE:  BL1: Booting BL2\nWelcome to Buildroot, type root to login\nbuildroot login: ")
if typed() != b"root\r":
    sys.exit(4)
if mode == "panic":
    say(b"Kernel panic - not syncing: Attempted to kill init!\n")
    sys.stdin.buffer.read()
    sys.exit(0)
say(b"# ")
line = typed()
with open(record, "wb") as f:
    f.write(line)
if mode == "status":
    say(b"Running test suite\nTFW-GUEST-STATUS=3\n# ")
elif mode == "noshare":
    say(b"TFW-GUEST-ERROR=no-share\n# ")
elif mode == "echo-only":
    sys.exit(0)
sys.stdin.buffer.read()
'''


class OpteeRun(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(self.cleanup)
        self.fake = os.path.join(self.tmp, "fake_qemu.py")
        self.record = os.path.join(self.tmp, "typed")
        with open(self.fake, "w", encoding="utf-8") as f:
            f.write(FAKE)

    def cleanup(self):
        for name in os.listdir(self.tmp):
            os.unlink(os.path.join(self.tmp, name))
        os.rmdir(self.tmp)

    def run_fake(self, mode, cmd="xtest -l 0"):
        qemu = " ".join(shlex.quote(a) for a in (sys.executable, "-I", self.fake, mode, self.record))
        console, err = io.BytesIO(), io.StringIO()
        with contextlib.redirect_stderr(err):
            status = optee_run.run(qemu, cmd, "/mnt/host", console)
        return status, console.getvalue(), err.getvalue()

    def typed(self):
        with open(self.record, "rb") as f:
            return f.read().decode()

    def test_the_guest_commands_status_is_the_runs(self):
        status, console, _ = self.run_fake("status", cmd="xtest -t regression 1001")
        self.assertEqual(status, 3)
        self.assertIn(b"Running test suite", console)
        line = self.typed()
        self.assertIn("( xtest -t regression 1001 );", line)
        self.assertIn("pidof tee-supplicant", line)
        self.assertIn("cd /mnt/host", line)

    def test_the_echoed_line_is_not_taken_for_the_status(self):
        status, _, err = self.run_fake("echo-only")
        self.assertIsNone(status)
        self.assertIn("QEMU exited", err)
        self.assertNotIn("TFW-GUEST-STATUS=", self.typed())
        self.assertNotIn("TFW-GUEST-ERROR=", self.typed())

    def test_a_guest_refusal_fails_the_run(self):
        status, _, err = self.run_fake("noshare")
        self.assertIsNone(status)
        self.assertIn("refused the command: no-share", err)

    def test_a_kernel_panic_fails_the_run(self):
        status, _, err = self.run_fake("panic")
        self.assertIsNone(status)
        self.assertIn("panicked", err)

    def test_qemu_exiting_before_the_login_fails_the_run(self):
        status, _, err = self.run_fake("eof")
        self.assertIsNone(status)
        self.assertIn("while waiting for the login", err)

    def test_an_empty_command_is_refused_before_qemu_starts(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            rc = optee_run.main(["--mount", "/mnt/host", "--cmd", " ", "false"])
        self.assertEqual(rc, optee_run.RUN_FAILED)
        self.assertIn("names nothing to run", err.getvalue())


if __name__ == "__main__":
    unittest.main()
