# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for the manifest check's refusal on the 4 KB parts (make scripts-test; #52, slice 4).

The manifest check stays off the ATmega328P and ATmega32U4 (owner queue 176,
option (a)). These tests read what make decides, without building: the refusal
of OTA_MANIFEST=1 at parse time, and the stub both parts link by default. The
board names and file names are literals, as the caller types them.
"""

import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


class ManifestOffThe4KBParts(unittest.TestCase):
    def test_the_manifest_check_is_refused_with_the_ruling(self):
        for board in ("atmega328p", "atmega32u4"):
            with self.subTest(board=board):
                rc, out = make("firmware-srcs", "BOARD=" + board, "OTA_MANIFEST=1")
                self.assertNotEqual(rc, 0, out)
                self.assertIn("OTA_MANIFEST=1 is refused on " + board, out)
                self.assertIn("owner queue 176, option (a)", out)
                self.assertTrue(out.startswith("mk/firmware.mk:"), out)

    def test_the_default_links_the_stub_on_both_parts(self):
        for board in ("atmega328p", "atmega32u4"):
            with self.subTest(board=board):
                rc, out = make("firmware-srcs", "BOARD=" + board)
                self.assertEqual(rc, 0, out)
                self.assertNotIn("src/core/suit.c", out.split())
                self.assertNotIn("src/core/ota_manifest.c", out.split())

    def test_the_2560_still_links_the_reader(self):
        rc, out = make("firmware-srcs", "BOARD=atmega2560", "CRYPTO=atecc608a",
                       "OTA_STAGE=1")
        self.assertEqual(rc, 0, out)
        self.assertIn("src/core/suit.c", out.split())
        self.assertIn("src/core/ota_manifest.c", out.split())


if __name__ == "__main__":
    unittest.main()
