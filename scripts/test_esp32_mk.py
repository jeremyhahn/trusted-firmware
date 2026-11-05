# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests for mk/arch/esp32-targets.mk's make logic (make scripts-test).

Through a dry run (make -n) of the target itself, so they need no ESP-IDF and
no QEMU: esp32-qemu-test runs its esp32c3 QEMU session to the end before it
starts the esp32 one, neither backgrounded, so neither starves the other on a
shared runner (#319), and the esp32 session still runs when esp32c3 fails."""

import os
import re
import subprocess
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

QEMU_TEST = "test/integration/esp32/qemu_test.py"


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        check=False)
    return p.returncode, p.stdout


def qemu_command():
    """The one shell command of the dry run that runs the QEMU sessions."""
    rc, out = make("-n", "esp32-qemu-test")
    if rc != 0:
        raise AssertionError(f"make -n esp32-qemu-test: {out}")
    commands = [c for c in out.replace("\\\n", " ").splitlines()
                if QEMU_TEST in c]
    if len(commands) != 1:
        raise AssertionError(
            f"want both {QEMU_TEST} runs in one shell command, "
            f"got {len(commands)}: {commands}")
    return commands[0]


class Esp32QemuTestRunsOneSessionAtATime(unittest.TestCase):

    def test_esp32c3_runs_before_esp32(self):
        runs = re.findall(re.escape(QEMU_TEST) + r" \S+ (\S+)", qemu_command())
        self.assertEqual(runs, ["esp32c3", "esp32"])

    def test_neither_session_is_backgrounded(self):
        command = qemu_command()
        self.assertNotRegex(command, r"(?<![&|>])&(?![&>])")
        self.assertNotRegex(command, r"\bwait\b")

    def test_each_failure_is_kept_and_esp32_still_runs(self):
        command = qemu_command()
        self.assertNotIn("&&", command)
        for session in command.split(QEMU_TEST)[1:]:
            self.assertRegex(session, r"\|\| rc=1;")
        self.assertRegex(command, r"exit \$rc$")


if __name__ == "__main__":
    unittest.main()
