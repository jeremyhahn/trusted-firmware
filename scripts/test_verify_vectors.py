# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# scripts/verify-vectors.sh is the only thing that keeps the C and Go mirrors
# of the wire vectors equal, and each of its blocks is a sed pattern. A
# pattern that stops matching makes both sides empty and the block "agree",
# so this pins the counts the real pair has and makes each kind of drift a
# failure. Each case copies the real pair into a temp dir under the paths the
# script reads, edits the copy and runs the real script there (#629).
#
# VERIFY_VECTORS_SCRIPT points the suite at a scratch copy of the script; it
# exists only to prove the suite bites a mutated script and is never set by
# the Makefile.

import os
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("VERIFY_VECTORS_SCRIPT") or os.path.join(HERE, "verify-vectors.sh")
C = os.path.join("testdata", "vectors.h")
G = os.path.join("sdk", "go", "internal", "vectors", "vectors.go")

# What the real pair carries today. A new vector moves a count here; a sed
# pattern that stops matching moves it to 0 and turns test_real_pair red.
AGREE = [
    "wire vectors agree: 7 CRC values and 37 wire frames, both languages",
    "SYS_INFO reply vectors agree: 6 values, both languages",
    "CAPS reply vectors agree: 2 values, both languages",
    "APP_INFO reply vectors agree: 3 values, both languages",
    "APP_CMD vectors agree: 8 values, both languages",
    "application evidence vectors agree: 3 values, both languages",
    "image trailer vectors agree: 10 values, both languages",
]


class TestVerifyVectors(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        for rel in (C, G):
            dst = os.path.join(self.tmp.name, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(REPO, rel), dst)

    def read(self, rel):
        with open(os.path.join(self.tmp.name, rel), encoding="utf-8") as f:
            return f.read().split("\n")

    def write(self, rel, lines):
        with open(os.path.join(self.tmp.name, rel), "w", encoding="utf-8") as f:
            f.write("\n".join(lines))

    def edit(self, rel, pattern, fn, count=1):
        """Apply fn to the first `count` lines matching pattern; return the
        (old, new) pairs. fn returning None deletes the line."""
        lines, out, done = self.read(rel), [], []
        rx = re.compile(pattern)
        for line in lines:
            if len(done) < count and rx.search(line):
                new = fn(line)
                done.append((line, new))
                if new is not None:
                    out.append(new)
            else:
                out.append(line)
        self.assertEqual(len(done), count, f"{pattern!r} matched too few lines of {rel}")
        self.write(rel, out)
        return done

    def run_script(self):
        return subprocess.run(["bash", SCRIPT], cwd=self.tmp.name,
                              capture_output=True, text=True, timeout=60)

    def assertFails(self, r, *needles):
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        for n in needles:
            self.assertIn(n, r.stdout)

    # (1) The unedited pair agrees, block by block, at today's counts.
    def test_real_pair(self):
        r = self.run_script()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(r.stdout.splitlines(), AGREE)

    # (2) A CRC result one side has and the other does not.
    def test_crc_value_drift(self):
        with self.subTest("C value changed"):
            self.edit(C, r"^#define TFWV_CRC_CHECK_RESULT ",
                      lambda l: l.replace("0x29B1u", "0x29B2u"))
            r = self.run_script()
            self.assertFails(r, f"value(s) in {C} with no counterpart in {G}:",
                             "  0x29b2", "  0x29b1")
        with self.subTest("C-only value"):
            self.setUp()
            self.edit(C, r"^#define TFWV_CRC_CHECK_RESULT ",
                      lambda l: l + "\n#define TFWV_CRC_EXTRA_RESULT  0xBEEFu")
            r = self.run_script()
            self.assertFails(r, f"value(s) in {C} with no counterpart in {G}:", "  0xbeef")
            self.assertNotIn("with no counterpart in " + C, r.stdout)

    # (3) A wire frame the Go side no longer pins.
    def test_frame_missing_in_go(self):
        [(old, _)] = self.edit(G, r'^\tFrame[A-Za-z0-9]* *= *"', lambda l: None)
        frame = re.search(r'"(.*)"', old).group(1)
        r = self.run_script()
        self.assertFails(r, f"wire frame(s) in {C} with no counterpart in {G}:", frame)

    # (4) A SYS_INFO reply string that differs.
    def test_sysinfo_string_drift(self):
        self.edit(G, r'^\tSysInfo[A-Za-z]* *= *"', lambda l: l.replace('"\\x78', '"\\x79', 1))
        r = self.run_script()
        self.assertFails(r, f"string vector(s) in {G} with no counterpart in {C}:", "\\x79")

    # (5) Two CAPS replies that swap names: same value set, different pairing.
    def test_caps_swapped(self):
        lines = self.read(C)
        i = next(n for n, l in enumerate(lines) if l.startswith("#define TFWV_CAPS_PROTO_REPLY "))
        j = next(n for n, l in enumerate(lines) if l.startswith("#define TFWV_CAPS_PIN13_REPLY "))
        val = lambda l: l[l.index('"'):]
        lines[i], lines[j] = (lines[i][:lines[i].index('"')] + val(lines[j]),
                              lines[j][:lines[j].index('"')] + val(lines[i]))
        self.write(C, lines)
        r = self.run_script()
        self.assertFails(r, "CAPS reply vectors differ:")

    # (6) An image trailer span that differs.
    def test_trailer_span_drift(self):
        self.edit(C, r'^#define TFWV_TRAILER_ATTINY85_SPAN "',
                  lambda l: l.replace('"\\x54', '"\\x55', 1))
        r = self.run_script()
        self.assertFails(r, "image trailer vectors differ:")

    # (7) No CAPS vectors at all is a failure, not an empty agreement.
    def test_caps_missing(self):
        lines = [l for l in self.read(C) if "TFWV_CAPS_" not in l]
        self.write(C, lines)
        r = self.run_script()
        self.assertFails(r, f"no CAPS reply vectors found in {C}")

    # (8) No CRC result, wire frame or SYS_INFO vector on either side is a failure, not an empty agreement.
    def test_empty_blocks_fail(self):
        for name, c_key, g_rx, msg in (
                ("CRC", "TFWV_CRC_", r"^\tCRC[A-Za-z]* *= *0x", "CRC result"),
                ("frame", "TFWV_FRAME_", r'^\tFrame[A-Za-z0-9]* *= *"', "wire frame"),
                ("SYS_INFO", "TFWV_SYSINFO_", r'^\tSysInfo[A-Za-z]* *= *"', "SYS_INFO reply")):
            with self.subTest(name):
                self.setUp()
                self.write(C, [l for l in self.read(C) if c_key not in l])
                self.write(G, [l for l in self.read(G) if not re.search(g_rx, l)])
                r = self.run_script()
                self.assertFails(r, f"no {msg} vectors found in {C}")


if __name__ == "__main__":
    unittest.main()
