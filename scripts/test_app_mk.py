# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# mk/app.mk's APP_DIR gate, the refusals an out-of-tree application meets
# before anything is compiled. app-image only echoes a path, so make parses the
# Makefile and stops: the test needs no compiler and builds nothing.

import os
import shutil
import subprocess
import tempfile
import unittest

_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_MAKE = shutil.which("make")


@unittest.skipIf(_MAKE is None, "no make")
class AppMkGateTest(unittest.TestCase):
    def _make(self, app_mk, sources=(), write_app_mk=True):
        """Writes app_mk (and each source) into a fresh directory and runs
        make app-image with APP_DIR set to it: (returncode, stdout + stderr)."""
        with tempfile.TemporaryDirectory() as d:
            if write_app_mk:
                with open(os.path.join(d, "app.mk"), "w") as f:
                    f.write(app_mk)
            for src in sources:
                path = os.path.join(d, src)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as f:
                    f.write("void app_main(void) {}\n")
            env = dict(os.environ)
            for name in ("MAKEFLAGS", "MAKELEVEL", "MFLAGS", "APP_DIR"):
                env.pop(name, None)
            p = subprocess.run(
                [_MAKE, "-s", "--no-print-directory", "-C", _REPO,
                 "app-image", "APP_DIR=" + d],
                env=env, capture_output=True, text=True, timeout=60)
            return p.returncode, p.stdout + p.stderr

    def _refused(self, app_mk, message, sources=(), write_app_mk=True):
        rc, out = self._make(app_mk, sources, write_app_mk)
        self.assertNotEqual(rc, 0, out)
        self.assertIn(message, out)

    def test_valid_app_accepted(self):
        rc, out = self._make("APP_NAME := ok\nAPP_SRCS := a.c\n", ["a.c"])
        self.assertEqual(rc, 0, out)
        self.assertIn("app-ok", out)

    def test_fifteen_character_name_accepted(self):
        rc, out = self._make(
            "APP_NAME := abcdefghijklmno\nAPP_SRCS := a.c\n", ["a.c"])
        self.assertEqual(rc, 0, out)
        self.assertIn("app-abcdefghijklmno", out)

    def test_bad_names_refused(self):
        for name in ("abcdefghijklmnop", "Bad", "-ab"):
            with self.subTest(name=name):
                self._refused(
                    "APP_NAME := %s\nAPP_SRCS := a.c\n" % name,
                    "at most 15 characters", ["a.c"])

    def test_no_app_mk_refused(self):
        self._refused("", "has no app.mk", write_app_mk=False)

    def test_empty_srcs_refused(self):
        self._refused("APP_NAME := ok\nAPP_SRCS :=\n", "APP_SRCS is empty")

    def test_escaping_srcs_refused(self):
        for src in ("/a.c", "../a.c"):
            with self.subTest(src=src):
                self._refused(
                    "APP_NAME := ok\nAPP_SRCS := %s\n" % src,
                    "relative paths inside the application directory")

    def test_not_c_refused(self):
        self._refused("APP_NAME := ok\nAPP_SRCS := a.h\n", "not .c", ["a.h"])

    def test_missing_src_refused(self):
        self._refused(
            "APP_NAME := ok\nAPP_SRCS := a.c b.c\n", "do not exist", ["a.c"])


if __name__ == "__main__":
    unittest.main()
