# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for test/integration/cortex-m/qemu_test.sh's rows (make scripts-test).

`make integration-test-cortex-m` runs the script on QEMU's mps2-an505. Here
QEMU is a stub that records its argv, starts a `sleep` as the "guest" and
writes its pid to the -pidfile path, and wirecheck is a stub that records its
argv and fails only on the call STUB_FAIL_AT names. So each case pins what the
script starts, in what order, with which loaders, and how it ends: the usage
refusal, the rows genuine, tampered, otherkey and unprovisioned (and the hsm-*
rows only with `hsm`), no platform block on the unprovisioned row, a failing
row named, and a QEMU that is gone taken for a crash. No QEMU.

QEMU_TEST_SCRIPT names another copy of the script to run instead (a scratch
mutation, to see a case fail); unset, the real one runs.
"""

import os
import signal
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("QEMU_TEST_SCRIPT") or os.path.join(
    REPO, "test", "integration", "cortex-m", "qemu_test.sh")
USAGE = "usage: qemu_test.sh <dir> <machine> <board-id> <tier> [hsm]\n"
SEP = "\x1f"

BASE_ROWS = ["genuine", "tampered", "otherkey", "unprovisioned"]
HSM_ROWS = ["noset", "forged", "replay", "silence", "wipe", "noverdict"]
APPS = {"genuine": "app.bin", "tampered": "app-tampered.bin",
        "otherkey": "app-otherkey.bin", "unprovisioned": "app.bin"}

# argv, one record per line, fields split by SEP; the "guest" is a sleep
# with no stdio of ours, so the run's pipes close when the script ends.
QEMU_STUB = """#!/bin/sh
for a; do printf '%s\\037' "$a"; done >> "$STUB_DIR/qemu.rec"
echo >> "$STUB_DIR/qemu.rec"
for a; do pidfile=$a; done
if [ -n "${STUB_QEMU_DEAD-}" ]; then
    sh -c 'exit 0' </dev/null >/dev/null 2>&1 &
    p=$!
    wait "$p" || true
else
    sleep 30 </dev/null >/dev/null 2>&1 &
    p=$!
    echo "$p" >> "$STUB_DIR/pids"
fi
echo "$p" > "$pidfile"
"""

WIRECHECK_STUB = """#!/bin/sh
for a; do printf '%s\\037' "$a"; done >> "$STUB_DIR/wirecheck.rec"
echo >> "$STUB_DIR/wirecheck.rec"
n=$(wc -l < "$STUB_DIR/wirecheck.rec")
[ "$n" -ne "${STUB_FAIL_AT:-0}" ]
"""


def records(path):
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return [line.rstrip("\n").split(SEP)[:-1] for line in f]


def after(argv, flag):
    return argv[argv.index(flag) + 1]


class TestQemuTestRows(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        with open(os.path.join(self.dir, "addrs.env"), "w") as f:
            f.write("BOOT_AT=0x10000000\nAPP_AT=0x10010000\n"
                    "PLATFORM_AT=0x10080000\nDEVICEID=00\n")
        for name in ("boot.bin", "platform.bin", "app.bin",
                     "app-tampered.bin", "app-otherkey.bin"):
            open(os.path.join(self.dir, name), "w").close()
        for name, body in (("qemu", QEMU_STUB), ("wirecheck", WIRECHECK_STUB)):
            path = os.path.join(self.dir, name)
            with open(path, "w") as f:
                f.write(body)
            os.chmod(path, 0o755)

    def tearDown(self):
        pids = os.path.join(self.dir, "pids")
        if os.path.exists(pids):
            with open(pids) as f:
                for line in f:
                    try:
                        os.kill(int(line), signal.SIGKILL)
                    except (ProcessLookupError, ValueError):
                        pass
        self.tmp.cleanup()

    def run_script(self, *args, **extra):
        env = dict(os.environ, QEMU=os.path.join(self.dir, "qemu"),
                   STUB_DIR=self.dir, CM_SILENT="1s", CM_WAIT="1s", **extra)
        return subprocess.run(["sh", SCRIPT, *args], env=env,
                              capture_output=True, text=True, timeout=60)

    def run_rows(self, *extra_args, **extra):
        return self.run_script(self.dir, "mps2-an505", "mps2-an505", "dev",
                               *extra_args, **extra)

    def qemu_calls(self):
        return records(os.path.join(self.dir, "qemu.rec"))

    def wirecheck_calls(self):
        return records(os.path.join(self.dir, "wirecheck.rec"))

    def rows_started(self):
        return [os.path.basename(after(argv, "-pidfile"))[:-len(".pid")]
                for argv in self.qemu_calls()]

    def loaders(self, argv):
        return [argv[i + 1] for i, a in enumerate(argv) if a == "-device"
                and argv[i + 1].startswith("loader,")]

    def test_a_wrong_argument_list_gets_the_usage_line_and_no_qemu(self):
        for args in ((self.dir, "m", "b"),
                     (self.dir, "m", "b", "t", "hsm", "x"),
                     (self.dir, "m", "b", "t", "nohsm")):
            with self.subTest(args=args):
                got = self.run_script(*args)
                self.assertEqual(got.returncode, 2, got.stderr)
                self.assertEqual(got.stderr, USAGE)
                self.assertEqual(self.qemu_calls(), [])

    def test_four_arguments_run_the_four_rows_in_order(self):
        got = self.run_rows()
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
        self.assertEqual(got.stdout.splitlines()[-1], "qemu_test: green")
        self.assertEqual(self.rows_started(), BASE_ROWS)
        self.assertEqual(len(self.wirecheck_calls()), 2 + 1 + 1 + 1)
        self.assertFalse(any("-hsm" in argv for argv in self.wirecheck_calls()))

    def test_each_row_loads_boot_and_its_application_and_only_provisioned_rows_a_platform(self):
        got = self.run_rows()
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
        calls = self.qemu_calls()
        self.assertEqual(len(calls), len(BASE_ROWS))
        for row, argv in zip(BASE_ROWS, calls):
            with self.subTest(row=row):
                self.assertEqual(after(argv, "-M"), "mps2-an505")
                want = [f"loader,file={self.dir}/boot.bin,addr=0x10000000,force-raw=on",
                        f"loader,file={self.dir}/{APPS[row]},addr=0x10010000,force-raw=on"]
                if row != "unprovisioned":
                    want.append(f"loader,file={self.dir}/platform.bin,"
                                "addr=0x10080000,force-raw=on")
                self.assertEqual(self.loaders(argv), want)

    def test_wirecheck_is_asked_the_board_tier_deviceid_reset_and_refusal(self):
        got = self.run_rows()
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
        calls = self.wirecheck_calls()
        for argv in calls[:2]:
            self.assertEqual(after(argv, "-board"), "mps2-an505")
            self.assertEqual(after(argv, "-tier"), "dev")
            self.assertEqual(after(argv, "-deviceid"), "00")
        self.assertIn("-reset", calls[0])
        self.assertNotIn("-reset", calls[1])
        for argv in calls[2:4]:
            self.assertIn("-silent", argv)
            self.assertNotIn("-refusal", argv)
        self.assertEqual(after(calls[4], "-refusal"), "15")

    def test_hsm_runs_the_six_hsm_rows_after_the_four(self):
        got = self.run_rows("hsm")
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
        self.assertEqual(got.stdout.splitlines()[-1], "qemu_test: green")
        self.assertEqual(self.rows_started(),
                         BASE_ROWS + [f"hsm-{r}" for r in HSM_ROWS])
        hsm_calls = self.wirecheck_calls()[5:]
        self.assertEqual([after(argv, "-hsm") for argv in hsm_calls], HSM_ROWS)
        for argv in hsm_calls:
            self.assertEqual(after(argv, "-hsm-dir"), self.dir)

    def test_a_failing_row_is_named_and_no_later_row_starts(self):
        got = self.run_rows(STUB_FAIL_AT="3")
        self.assertEqual(got.returncode, 1, got.stdout + got.stderr)
        self.assertIn("qemu_test: tampered: ", got.stdout)
        self.assertNotIn("qemu_test: green", got.stdout)
        self.assertEqual(self.rows_started(), ["genuine", "tampered"])

    def test_a_qemu_that_is_gone_is_taken_for_a_crash(self):
        got = self.run_rows(STUB_QEMU_DEAD="1")
        self.assertEqual(got.returncode, 1, got.stdout + got.stderr)
        self.assertIn("qemu_test: genuine: QEMU exited", got.stdout)
        self.assertEqual(self.rows_started(), ["genuine"])


if __name__ == "__main__":
    unittest.main()
