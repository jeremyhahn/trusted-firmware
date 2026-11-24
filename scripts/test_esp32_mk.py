# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests for mk/arch/esp32-targets.mk's make logic (make scripts-test).

Through a dry run (make -n) of the target itself, so they need no ESP-IDF and
no QEMU: esp32-qemu-test runs its esp32c3 QEMU session to the end before it
starts the esp32 one, neither backgrounded, so neither starves the other on a
shared runner (#319), and the esp32 session still runs when esp32c3 fails."""

import os
import re
import shutil
import subprocess
import tempfile
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


MAIN_CMAKE = "mk/arch/esp32/main/CMakeLists.txt"
STATIC_IP_IF = "if(CONFIG_TFW_ETH_STATIC_IP)"


def static_address_check():
    """The block of MAIN_CMAKE from STATIC_IP_IF to its matching endif()."""
    with open(os.path.join(REPO, MAIN_CMAKE), encoding="utf-8") as f:
        lines = f.read().splitlines()
    starts = [i for i, line in enumerate(lines) if line.strip() == STATIC_IP_IF]
    if len(starts) != 1:
        raise AssertionError(f"want one {STATIC_IP_IF} in {MAIN_CMAKE}, "
                             f"got {len(starts)}")
    depth = 0
    for i in range(starts[0], len(lines)):
        stripped = lines[i].strip()
        if re.match(r"if\s*\(", stripped):
            depth += 1
        elif re.match(r"endif\s*\(", stripped):
            depth -= 1
            if depth == 0:
                return "\n".join(lines[starts[0]:i + 1]) + "\n"
    raise AssertionError(f"{STATIC_IP_IF} in {MAIN_CMAKE} has no endif()")


def run_static_address_check(ip="192.0.2.10", netmask="255.255.255.0",
                             gateway="192.0.2.1"):
    """Run the check in cmake -P with these knobs; return (rc, output)."""
    with tempfile.TemporaryDirectory() as tmp:
        script = os.path.join(tmp, "check.cmake")
        with open(script, "w", encoding="utf-8") as f:
            f.write(static_address_check())
        p = subprocess.run(
            ["cmake", "-DCONFIG_TFW_ETH_STATIC_IP=1",
             f"-DCONFIG_TFW_ETH_IP_ADDR={ip}",
             f"-DCONFIG_TFW_ETH_NETMASK={netmask}",
             f"-DCONFIG_TFW_ETH_GATEWAY={gateway}", "-P", script],
            cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, check=False)
    return p.returncode, p.stdout


@unittest.skipUnless(shutil.which("cmake"), "cmake is not on PATH")
class Esp32StaticAddressIsChecked(unittest.TestCase):
    """The configure-time check of the static address knobs: src/hal/esp32/
    hal_eth.c relies on it to refuse anything lwIP would not read as the
    address the operator wrote."""

    def assert_refused(self, knob, **knobs):
        rc, out = run_static_address_check(**knobs)
        self.assertNotEqual(rc, 0, out)
        self.assertIn(f"CONFIG_{knob}", out)

    def test_dotted_quads_are_accepted(self):
        for ip in ("192.0.2.10", "0.0.0.0", "255.255.255.255"):
            with self.subTest(ip=ip):
                rc, out = run_static_address_check(ip=ip)
                self.assertEqual(rc, 0, out)

    def test_a_value_that_is_not_a_dotted_quad_is_refused(self):
        for ip in ("192.0.2.256", "1.2.3", "192.0.2.x"):
            with self.subTest(ip=ip):
                self.assert_refused("TFW_ETH_IP_ADDR", ip=ip)

    def test_a_leading_zero_in_the_address_is_refused(self):
        # lwIP's ip4addr_aton reads 010 as octal 8 and refuses 08.
        for ip in ("192.0.2.010", "192.0.2.08", "010.0.2.1"):
            with self.subTest(ip=ip):
                self.assert_refused("TFW_ETH_IP_ADDR", ip=ip)

    def test_a_leading_zero_in_the_netmask_is_refused(self):
        self.assert_refused("TFW_ETH_NETMASK", netmask="255.255.255.000")

    def test_a_leading_zero_in_the_gateway_is_refused(self):
        self.assert_refused("TFW_ETH_GATEWAY", gateway="192.0.2.01")


if __name__ == "__main__":
    unittest.main()
