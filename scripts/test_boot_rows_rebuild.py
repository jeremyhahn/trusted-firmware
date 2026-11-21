# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for test_boot_rows_2560 following BOOT_SUPPORTED (make scripts-test; #640).

test_boot_rows_2560 compiles mk/boot.mk's BOOT_SUPPORTED into its binary, so a
binary built from one matrix must not be run against another: an edited or
overridden matrix rebuilds it, and the default matrix rebuilds it back. Each
build is in a scratch BUILD_DIR, so the repository's build/ is untouched.
"""

import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

MARKER = "atmega2560:marker-row"


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


def make_print(expr, *args):
    """The value make expands expr to, with args on the command line."""
    rc, out = make("--eval", "tfw-print: ; @echo %s" % expr, "tfw-print", *args)
    if rc != 0:
        raise AssertionError(out)
    return out.strip()


class BootRowsRebuild(unittest.TestCase):
    def setUp(self):
        self.build = tempfile.mkdtemp(prefix="tfw-boot-rows-")
        self.addCleanup(shutil.rmtree, self.build, ignore_errors=True)
        self.build_arg = "BUILD_DIR=" + self.build
        self.default = make_print("$(BOOT_SUPPORTED)")
        host_dir = make_print("$(HOST_DIR)", self.build_arg)
        self.target = os.path.join(host_dir, "boot", "test_boot_rows_2560")
        self.binary = self.target if os.path.isabs(self.target) else \
            os.path.join(REPO, self.target)

    def build_rows(self, *args):
        rc, out = make(self.target, self.build_arg, *args)
        self.assertEqual(rc, 0, out)
        with open(self.binary, "rb") as f:
            return f.read()

    def test_the_binary_follows_the_matrix(self):
        marker = MARKER.encode()
        self.assertFalse(marker in self.build_rows(), "the default matrix names the marker")
        edited = self.build_rows("BOOT_SUPPORTED=%s %s" % (self.default, MARKER))
        self.assertTrue(marker in edited, "an overridden matrix ran the old binary")
        self.assertFalse(marker in self.build_rows(),
                         "the default matrix ran the overridden binary")

    def test_an_unchanged_matrix_rebuilds_nothing(self):
        self.build_rows()
        rc, out = make("-q", self.target, self.build_arg)
        self.assertEqual(rc, 0, "a second make would rebuild the binary: " + out)


if __name__ == "__main__":
    unittest.main()
