"""make lint-no-delay fails closed: a tree it cannot scan is not a clean tree.

The gate sweeps LINT_GREP_ROOT for blocking delay calls in the firmware C
(SPEC device-capabilities: "There is no delay()"). A root it cannot enter, or a
file grep cannot read, must fail the gate, never pass it as "no hits".
"""

import os
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def lint(root):
    return subprocess.run(
        ["make", "--no-print-directory", "-C", REPO, "lint-no-delay",
         "LINT_GREP_ROOT=" + root],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


class LintNoDelayTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        for d in ("src/core", "boot"):
            os.makedirs(os.path.join(self.root, d))
        self.write("src/core/ok.c", "int ok(void) { return 0; }\n")

    def write(self, rel, text):
        path = os.path.join(self.root, rel)
        with open(path, "w") as f:
            f.write(text)
        return path

    def test_clean_tree_passes(self):
        r = lint(self.root)
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_delay_call_fails(self):
        self.write("src/core/plant.c", "void f(void) { delay(10); }\n")
        r = lint(self.root)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("src/core/plant.c:1: delay-call", r.stdout)

    def test_delay_call_split_before_paren_fails(self):
        self.write("src/core/plant.c", "void f(void) {\n  delay\n(10);\n}\n")
        r = lint(self.root)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("src/core/plant.c:2: delay-call", r.stdout)

    def test_delay_call_split_after_paren_fails(self):
        self.write("boot/plant.c", "void f(void) {\n  delay (\n10);\n}\n")
        r = lint(self.root)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("boot/plant.c:2: delay-call", r.stdout)

    def test_comment_and_bus_timing_pass(self):
        self.write("src/core/prose.c",
                   "// a poll, never delay()\n"
                   " * nor delay\n"
                   "void g(void) { _delay_us\n(5); }\n")
        r = lint(self.root)
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_missing_root_fails(self):
        r = lint(os.path.join(self.root, "no-such-dir"))
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertNotIn("no delay calls in the firmware", r.stdout)

    @unittest.skipIf(os.geteuid() == 0,
                     "root reads a mode-000 file, so it cannot be unreadable")
    def test_unreadable_file_fails(self):
        path = self.write("src/core/plant.c", "void f(void) { delay(10); }\n")
        os.chmod(path, 0)
        self.addCleanup(os.chmod, path, 0o644)
        r = lint(self.root)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertNotIn("no delay calls in the firmware", r.stdout)


if __name__ == "__main__":
    unittest.main()
