# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/tfm-measure.sh's footprint table and stops (make scripts-test).

The script fetches TF-M and tf-m-extras, builds three trees and prints each
image's flash (text + data) and RAM (data + bss) from arm-none-eabi-size. Each
run here gives it a work directory whose checkouts and venv already exist, and
a PATH whose git, cmake, arm-none-eabi-gcc and arm-none-eabi-size are stubs:
git reports the HEAD the case chose, cmake logs its calls and fails for the
build directory the case names, and arm-none-eabi-size prints the sizes the
case wrote. No network, no TF-M, no CMake, no Arm toolchain.

TFM_MEASURE_SCRIPT runs the suite against another copy of the script (a
mutation check); the copy needs its tfm-pins.sh next to it, as the script
sources it from there. It is not needed otherwise.
"""

import os
import re
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("TFM_MEASURE_SCRIPT") or os.path.join(REPO, "scripts", "tfm-measure.sh")
# The pins live in tfm-pins.sh beside the script, which sources it (tfm-build.sh
# shares them).
PINS = os.path.join(os.path.dirname(SCRIPT), "tfm-pins.sh")


def pin(name):
    """The value of NAME=... in the pins file, so the test follows a re-pin."""
    with open(PINS, encoding="utf-8") as f:
        m = re.search(r"^%s=(\S+)$" % name, f.read(), re.M)
    if not m:
        raise AssertionError("%s not found in %s" % (name, PINS))
    return m.group(1)


TFM_COMMIT = pin("TFM_COMMIT")
EXTRAS_COMMIT = pin("EXTRAS_COMMIT")

# The sizes the stub arm-none-eabi-size reports, as text, data, bss.
SIZES = {
    "build-base/bin/bl2.axf": (30000, 1000, 20000),
    "build-base/bin/tfm_s.axf": (90000, 2000, 60000),
    "build-stub/bin/tfm_s.axf": (94500, 2100, 63000),
    "build-ns/bin/tfm_ns.axf": (12000, 300, 4000),
}

HEADER = "| Image | text | data | bss | flash, bytes | RAM, bytes |"
SEPARATOR = "|---|---|---|---|---|---|"

GIT = """#!/bin/bash
# git -C DIR rev-parse HEAD: the HEAD the case chose for DIR.
[ "$1" = -C ] && [ "$3 $4" = "rev-parse HEAD" ] || exit 99
cat "$STUB/head-$(basename "$2")"
"""

CMAKE = """#!/bin/bash
# Logs each call; fails when an argument is the build directory to fail.
echo "$*" >>"$STUB/cmake.log"
for a in "$@"; do
	[ -n "${STUB_FAIL:-}" ] && [ "$a" = "$STUB_FAIL" ] && exit 1
done
exit 0
"""

GCC = """#!/bin/bash
echo "arm-none-eabi-gcc (stub) 14.2.1"
"""

SIZE = """#!/bin/bash
# arm-none-eabi-size -B FILE: a header, then FILE's row from the case's table.
[ "$1" = -B ] || exit 99
echo "   text	   data	    bss	    dec	    hex	filename"
while read -r f t d b; do
	if [ "$f" = "$2" ]; then
		echo "$t $d $b $((t + d + b)) $(printf %x $((t + d + b))) $2"
		exit 0
	fi
done <"$STUB/sizes"
exit 1
"""

OK = "#!/bin/sh\nexit 0\n"


def kib(n):
    return "%d.%d" % (n // 1024, (n % 1024) * 10 // 1024)


def row(label, text, data, bss):
    flash, ram = text + data, data + bss
    return "| %s | %d | %d | %d | %d (%s KiB) | %d (%s KiB) |" % (
        label, text, data, bss, flash, kib(flash), ram, kib(ram))


def table(sizes):
    b = sizes["build-base/bin/bl2.axf"]
    s = sizes["build-base/bin/tfm_s.axf"]
    p = sizes["build-stub/bin/tfm_s.axf"]
    n = sizes["build-ns/bin/tfm_ns.axf"]
    return [
        HEADER,
        SEPARATOR,
        row("BL2 (MCUboot)", *b),
        row("secure image, Profile Medium", *s),
        row("secure image with the stub partition", *p),
        row("the stub partition (difference)", *(x - y for x, y in zip(p, s))),
        row("BL2 + secure image with the stub", *(x + y for x, y in zip(b, p))),
        row("non-secure stub", *n),
    ]


def write(path, body, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(body)
    os.chmod(path, mode)


class TfmMeasureTest(unittest.TestCase):
    def run_script(self, sizes=SIZES, tfm_head=TFM_COMMIT, fail=None):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        work = os.path.join(tmp.name, "work")
        stub = os.path.join(tmp.name, "stub")
        exe = stat.S_IRWXU
        os.makedirs(os.path.join(work, "tf-m", ".git"))
        os.makedirs(os.path.join(work, "tf-m-extras", ".git"))
        write(os.path.join(work, "venv", "bin", "python"), OK, exe)
        write(os.path.join(work, "venv", "bin", "pip"), OK, exe)
        write(os.path.join(stub, "bin", "git"), GIT, exe)
        write(os.path.join(stub, "bin", "cmake"), CMAKE, exe)
        write(os.path.join(stub, "bin", "arm-none-eabi-gcc"), GCC, exe)
        write(os.path.join(stub, "bin", "arm-none-eabi-size"), SIZE, exe)
        write(os.path.join(stub, "head-tf-m"), tfm_head + "\n")
        write(os.path.join(stub, "head-tf-m-extras"), EXTRAS_COMMIT + "\n")
        write(os.path.join(stub, "sizes"), "".join(
            "%s %d %d %d\n" % ((os.path.join(work, f),) + v) for f, v in sizes.items()))
        write(os.path.join(stub, "cmake.log"), "")
        env = dict(os.environ)
        env["PATH"] = os.path.join(stub, "bin") + os.pathsep + env.get("PATH", "")
        env["STUB"] = stub
        env.pop("STUB_FAIL", None)
        if fail:
            env["STUB_FAIL"] = os.path.join(work, fail)
        proc = subprocess.run(["bash", SCRIPT, work], env=env, capture_output=True,
                              text=True, timeout=60)
        with open(os.path.join(stub, "cmake.log"), encoding="utf-8") as f:
            cmake_calls = f.read().splitlines()
        return proc, work, cmake_calls

    def test_table_rows_add_up(self):
        proc, _, _ = self.run_script()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        lines = proc.stdout.splitlines()
        self.assertIn("| BL2 (MCUboot) | 30000 | 1000 | 20000 | 31000 (30.2 KiB) | 21000 (20.5 KiB) |",
                      lines)
        want = table(SIZES)
        start = lines.index(HEADER)
        self.assertEqual(lines[start:], want)
        self.assertFalse([l for l in lines[:start] if l.startswith("|")])

    def test_stub_no_larger_than_base_stops(self):
        sizes = dict(SIZES)
        sizes["build-stub/bin/tfm_s.axf"] = (89900, 2100, 63000)  # text + data = base's
        proc, _, _ = self.run_script(sizes=sizes)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("no larger than base", proc.stderr)
        self.assertNotIn(HEADER, proc.stdout)
        self.assertNotIn("|", proc.stdout)

    def test_wrong_pinned_commit_stops_before_cmake(self):
        other = "1" * 40
        proc, _, cmake_calls = self.run_script(tfm_head=other)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("is %s, pinned %s" % (other, TFM_COMMIT), proc.stderr)
        self.assertEqual(cmake_calls, [])
        self.assertEqual(proc.stdout, "")

    def test_failed_configure_names_its_log(self):
        proc, work, _ = self.run_script(fail="build-stub")
        self.assertEqual(proc.returncode, 1)
        self.assertIn("configure stub failed; see %s/logs/configure-stub.log" % work, proc.stderr)
        self.assertNotIn("|", proc.stdout)


if __name__ == "__main__":
    unittest.main()
