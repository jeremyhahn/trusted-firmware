# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for test/integration/cortex-m/qemu_tz_test.sh's run (make scripts-test).

mk/arch/cortex-m-targets.mk's cortex-m-qemu-tz-run calls the script with
<dir> <machine> <s-load> <ns-load> <s-ram>. Each run here names a stub as
QEMU: it records its argv, appends a fixed UART log to the -serial file: path
(appends, so a log the script left in place would show),
starts a `sleep 30` and writes its pid to the -pidfile path, as -daemonize
would. Every log carries a SecureFault line, so the script's wait ends at
once. No QEMU.
"""

import os
import signal
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
# QEMU_TZ_SCRIPT points the suite at a scratch copy of the script, for a
# mutation run only; make scripts-test never sets it.
SCRIPT = os.environ.get("QEMU_TZ_SCRIPT") or os.path.join(
    REPO, "test", "integration", "cortex-m", "qemu_tz_test.sh")

ARGS = ("mps2-an505", "0x10000000", "0x00200000", "0x30000000")
FAULT = "tz: s: SecureFault SFSR=0x00000048 SFAR=0x30000000"
# The lines the script requires, each with the FAIL message it gives when
# the line is missing.
REQUIRED = (
    ("tz: s: sau on, entering ns",
     "the secure image did not reach the non-secure entry"),
    ("tz: ns: up", "the non-secure image did not run"),
    ("tz: ns: tfw_s_version=0x00010000",
     "tfw_s_version through the veneer did not return 0x00010000"),
    ("tz: ns: reading secure SRAM",
     "the non-secure image did not reach its secure SRAM read"),
)

STUB = """#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$STUB_DIR/argv"
while [ $# -gt 0 ]; do
    case $1 in
    -serial) serial=${2#file:}; shift;;
    -pidfile) pidfile=$2; shift;;
    esac
    shift
done
cat "$STUB_UART" >> "$serial"
sleep 30 > /dev/null 2>&1 < /dev/null &
echo $! >> "$STUB_DIR/pids"
echo $! > "$pidfile"
exit 0
"""


def green():
    return [line for line, _ in REQUIRED] + [FAULT]


class TestRun(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        for name in ("tz_s.bin", "tz_ns.bin"):
            open(os.path.join(self.dir, name), "wb").close()
        self.qemu = os.path.join(self.dir, "qemu")
        with open(self.qemu, "w") as f:
            f.write(STUB)
        os.chmod(self.qemu, 0o755)
        self.uart = os.path.join(self.dir, "uart.in")

    def tearDown(self):
        try:
            with open(os.path.join(self.dir, "pids")) as f:
                pids = [int(p) for p in f.read().split()]
        except FileNotFoundError:
            pids = []
        for pid in pids:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        self.tmp.cleanup()

    def run_script(self, lines, args=ARGS, lf=()):
        """Runs the script on a UART log of lines, each CRLF-terminated but
        those whose index is in lf, which end in LF alone."""
        with open(self.uart, "w", newline="") as f:
            for i, line in enumerate(lines):
                f.write(line + ("\n" if i in lf else "\r\n"))
        env = dict(os.environ, QEMU=self.qemu, STUB_DIR=self.dir,
                   STUB_UART=self.uart)
        return subprocess.run(["bash", SCRIPT, self.dir, *args], env=env,
                              capture_output=True, text=True, timeout=30)

    def argv(self):
        with open(os.path.join(self.dir, "argv")) as f:
            return f.read()

    def assertFails(self, got, message):
        self.assertEqual(got.returncode, 1, got.stdout + got.stderr)
        self.assertIn(f"qemu_tz_test: FAIL: {message}", got.stdout)

    def test_an_address_that_is_not_hex_is_refused_before_qemu(self):
        for args in (("mps2-an505", "0x10000000", "0x00200000", "abc"),
                     ("mps2-an505", "0x10000000", "", "0x30000000")):
            with self.subTest(args=args):
                got = self.run_script(green(), args)
                self.assertFails(got, "an address is missing")
                self.assertFalse(os.path.exists(os.path.join(self.dir, "argv")))

    def test_the_green_log_passes_with_both_loaders(self):
        got = self.run_script(green())
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
        self.assertTrue(got.stdout.splitlines()[-1].endswith(
            "SecureFault at 0x30000000)"), got.stdout)
        argv = self.argv()
        self.assertIn("-M mps2-an505 ", argv)
        self.assertIn(f"-serial file:{self.dir}/tz.uart.log ", argv)
        self.assertIn(f"loader,file={self.dir}/tz_s.bin,addr=0x10000000,force-raw=on",
                      argv)
        self.assertIn(f"loader,file={self.dir}/tz_ns.bin,addr=0x00200000,force-raw=on",
                      argv)
        self.assertIn(f"-daemonize -pidfile {self.dir}/tz.pid", argv)

    def test_a_stale_log_from_the_last_run_is_removed(self):
        with open(os.path.join(self.dir, "tz.uart.log"), "w") as f:
            f.write("".join(line + "\r\n" for line in green()))
        missing = [line for line, _ in REQUIRED[1:]] + [FAULT]
        got = self.run_script(missing)
        self.assertFails(got, REQUIRED[0][1])

    def test_each_required_line_missing_fails_with_its_message(self):
        for line, message in REQUIRED:
            with self.subTest(line=line):
                got = self.run_script([l for l in green() if l != line])
                self.assertFails(got, message)

    def test_a_secure_sram_read_that_returned_fails(self):
        lines = green()
        lines.insert(4, "tz: ns: secure SRAM read returned 0x00000000")
        self.assertFails(self.run_script(lines),
                         "the non-secure image read secure SRAM")

    def test_an_unexpected_fault_line_fails(self):
        lines = green() + ["tz: ns: unexpected fault"]
        self.assertFails(self.run_script(lines, lf=(5,)), "an unexpected fault")

    def test_a_report_without_auviol_fails(self):
        lines = green()[:4] + ["tz: s: SecureFault SFSR=0x00000040 SFAR=0x30000000"]
        self.assertFails(self.run_script(lines), "SFSR 0x00000040 has no AUVIOL")

    def test_a_sfar_that_is_not_the_secure_sram_read_fails(self):
        lines = green()[:4] + ["tz: s: SecureFault SFSR=0x00000048 SFAR=0x30000004"]
        self.assertFails(self.run_script(lines),
                         "SFAR 0x30000004 is not the secure SRAM read at 0x30000000")


if __name__ == "__main__":
    unittest.main()
