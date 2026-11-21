# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for mk/ccdeps.sh, HOST_CC in mk/host.mk (make scripts-test).

Every host binary, tfwsim among them, is compiled and linked by
`sh mk/ccdeps.sh $(CC) ... -o OUT ...`, which then writes OUT.d naming every
header the sources include. Each case here names a stub cc the test writes: it
records its argv, one JSON list per call, prints a dependency line on the -MM
call and exits with STUB_RC (STUB_MM_RC on the -MM call). The last case uses
the real cc on PATH, if there is one.

CCDEPS_SCRIPT names another copy of the script to run; it is set only to prove
a mutated copy fails these tests.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("CCDEPS_SCRIPT") or os.path.join(REPO, "mk", "ccdeps.sh")
MM_LINE = "OUT: a.c inc.h\n"

STUB = f"""#!{sys.executable}
import json, os, sys
args = sys.argv[1:]
with open(os.environ["STUB_RECORD"], "a") as f:
    f.write(json.dumps(args) + "\\n")
if "-MM" in args:
    sys.stdout.write({MM_LINE!r})
    sys.exit(int(os.environ.get("STUB_MM_RC", "0")))
sys.exit(int(os.environ.get("STUB_RC", "0")))
"""


class TestStub(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.cc = os.path.join(self.dir, "stubcc")
        with open(self.cc, "w") as f:
            f.write(STUB)
        os.chmod(self.cc, 0o755)
        self.record = os.path.join(self.dir, "record")
        os.mkdir(os.path.join(self.dir, "out"))

    def tearDown(self):
        self.tmp.cleanup()

    def run_ccdeps(self, *args, **env):
        env = dict(os.environ, STUB_RECORD=self.record, **env)
        return subprocess.run(["sh", SCRIPT, self.cc, *args], cwd=self.dir, env=env,
                              capture_output=True, text=True, timeout=30)

    def calls(self):
        if not os.path.exists(self.record):
            return []
        with open(self.record) as f:
            return [json.loads(line) for line in f]

    def path(self, name):
        return os.path.join(self.dir, name)

    def test_no_o_exits_2_and_never_calls_cc(self):
        got = self.run_ccdeps("a.c", "b.c")
        self.assertEqual(got.returncode, 2, got.stderr)
        self.assertIn("ccdeps.sh: no -o in the command", got.stderr)
        self.assertEqual(self.calls(), [])

    def test_headers_leave_the_compile_and_unity_o_the_mm_run(self):
        got = self.run_ccdeps("a.c", "b.c", "x.h", "y.def", "unity.o", "-Iinc",
                              "-o", "out/app")
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(self.calls(), [
            ["a.c", "b.c", "unity.o", "-Iinc", "-o", "out/app"],
            ["a.c", "b.c", "-Iinc", "-MM", "-MP", "-MT", "out/app"],
        ])
        with open(self.path("out/app.d")) as f:
            self.assertEqual(f.read(), MM_LINE)
        self.assertFalse(os.path.exists(self.path("out/app.d.tmp")))

    def test_include_and_imacros_keep_their_file(self):
        got = self.run_ccdeps("-include", "cfg.h", "a.c", "-imacros", "m.h", "b.c",
                              "-o", "o")
        self.assertEqual(got.returncode, 0, got.stderr)
        calls = self.calls()
        self.assertEqual(len(calls), 2)
        for call in calls:
            with self.subTest(call=call):
                i = call.index("-include")
                self.assertEqual(call[i + 1], "cfg.h")
                i = call.index("-imacros")
                self.assertEqual(call[i + 1], "m.h")
                self.assertEqual(call.count("a.c"), 1)
                self.assertEqual(call.count("b.c"), 1)

    def test_o_given_first(self):
        got = self.run_ccdeps("-o", "o", "a.c")
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(self.calls(), [
            ["a.c", "-o", "o"],
            ["a.c", "-MM", "-MP", "-MT", "o"],
        ])
        with open(self.path("o.d")) as f:
            self.assertEqual(f.read(), MM_LINE)

    def test_a_failed_compile_writes_no_d(self):
        got = self.run_ccdeps("a.c", "-o", "o", STUB_RC="1")
        self.assertNotEqual(got.returncode, 0)
        self.assertEqual(len(self.calls()), 1)
        self.assertFalse(os.path.exists(self.path("o.d")))

    def test_a_failed_mm_run_leaves_the_old_d(self):
        with open(self.path("o.d"), "w") as f:
            f.write("o: old.h\n")
        got = self.run_ccdeps("a.c", "-o", "o", STUB_MM_RC="1")
        self.assertNotEqual(got.returncode, 0)
        self.assertEqual(len(self.calls()), 2)
        with open(self.path("o.d")) as f:
            self.assertEqual(f.read(), "o: old.h\n")


@unittest.skipUnless(shutil.which("cc"), "no cc on PATH")
class TestRealCC(unittest.TestCase):
    def test_the_d_names_the_headers_of_every_source(self):
        with tempfile.TemporaryDirectory() as d:
            files = {
                "one.h": "int one(void);\n",
                "two.h": "int two(void);\n",
                "one.c": '#include "one.h"\nint one(void) { return 1; }\n',
                "two.c": '#include "two.h"\nint one(void);\n'
                         "int main(void) { return one() - 1; }\n",
            }
            for name, text in files.items():
                with open(os.path.join(d, name), "w") as f:
                    f.write(text)
            got = subprocess.run(["sh", SCRIPT, "cc", "one.c", "two.c", "one.h", "-o", "app"],
                                 cwd=d, capture_output=True, text=True, timeout=60)
            self.assertEqual(got.returncode, 0, got.stderr)
            with open(os.path.join(d, "app.d")) as f:
                deps = f.read()
            self.assertIn("one.h", deps)
            self.assertIn("two.h", deps)


if __name__ == "__main__":
    unittest.main()
