# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Pins TFW_APP_STATIC_CHECK (include/tfw/app.h): an application that needs a
capability the image lacks, or more update slots than the board has, is a
compile error with its own message, not a device that boots firmware-only.

Each case compiles a two-line file with the host cc against include/, with
the feature gate or slot count given on the command line (-D), as config.h
allows."""

import os
import shutil
import subprocess
import tempfile
import unittest

_CC = shutil.which("cc")
_INCLUDE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "include")

_LACKS = "lacks a capability"
_SLOTS = "fewer update slots"


def _check(caps, ota, *defs):
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "t.c")
        with open(src, "w") as f:
            f.write('#include "tfw/app.h"\nTFW_APP_STATIC_CHECK(%s, %s);\n' % (caps, ota))
        r = subprocess.run([_CC, "-std=c11", "-fsyntax-only", "-I" + _INCLUDE, *defs, src],
                           capture_output=True, text=True)
    return r.returncode, r.stderr


@unittest.skipIf(_CC is None, "no host cc to compile the check")
class StaticCheck(unittest.TestCase):
    def test_a_capability_the_image_has_compiles(self):
        rc, err = _check("TFW_APP_CAP(TFW_CAPBIT_GPIO)", "TFW_APP_OTA_SINGLE")
        self.assertEqual(rc, 0, err)

    def test_a_capability_gated_off_is_a_compile_error(self):
        rc, err = _check("TFW_APP_CAP(TFW_CAPBIT_I2C)", "TFW_APP_OTA_NONE",
                         "-DTFW_FEATURE_I2C=0")
        self.assertNotEqual(rc, 0)
        self.assertIn(_LACKS, err)

    def test_a_bit_outside_the_bitmap_is_a_compile_error(self):
        rc, err = _check("TFW_APP_CAP(31u)", "TFW_APP_OTA_NONE")
        self.assertNotEqual(rc, 0)
        self.assertIn(_LACKS, err)

    def test_more_slots_than_the_board_has_is_a_compile_error(self):
        # The default board has one slot (config.h); A/B needs two.
        rc, err = _check("TFW_APP_CAP(TFW_CAPBIT_GPIO)", "TFW_APP_OTA_AB")
        self.assertNotEqual(rc, 0)
        self.assertIn(_SLOTS, err)
        self.assertNotIn(_LACKS, err)

    def test_slots_the_board_has_compile(self):
        # Fewer slots than the board has compile too, so the bound is <=.
        for ota in ("TFW_APP_OTA_AB", "TFW_APP_OTA_SINGLE"):
            with self.subTest(ota=ota):
                rc, err = _check("TFW_APP_CAP(TFW_CAPBIT_GPIO)", ota, "-DTFW_OTA_SLOTS=2u")
                self.assertEqual(rc, 0, err)


if __name__ == "__main__":
    unittest.main()
