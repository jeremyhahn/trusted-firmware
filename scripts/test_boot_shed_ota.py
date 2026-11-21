# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for the ATmega2560 soft tier shedding OTA (make scripts-test; #618).

On the soft tier the 2560's OTA and store rows do not fit, and they shed OTA
and the store, never the verifier (OTA requirement 37, CA m467): the one soft
row is atmega2560:soft:soft:p256-soft:0:0, and a row naming OTA or the store
is refused at parse time. These tests read what make decides, without
building. The rows are literals, as the caller types them.
"""

import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

SOFT = ("BOOT_TIER=soft", "BOOT_MEASURE=soft", "BOOT_SIG=p256-soft")
BASE_ROW = "atmega2560:soft:soft:p256-soft:0:0"


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


def supported_rows(out):
    """The rows the refusal offers for the board, as a list."""
    lead = "For atmega2560 the supported rows, which fit, are: "
    tail = ". See BOOT_SUPPORTED in mk/boot.mk"
    if lead not in out or tail not in out:
        raise AssertionError(out)
    return out.split(lead, 1)[1].split(tail, 1)[0].split()


class BootShedOta(unittest.TestCase):
    def test_ota_and_the_store_are_refused_on_the_soft_tier(self):
        for ota, store in (("1", "0"), ("0", "1")):
            with self.subTest(ota=ota, store=store):
                rc, out = make("bootloader", "BOARD=atmega2560", *SOFT,
                               "BOOT_OTA=" + ota, "BOOT_STORE=" + store, "MAKE=echo")
                self.assertNotEqual(rc, 0, out)
                self.assertIn("bootloader: atmega2560:soft:soft:p256-soft:%s:%s "
                              "(BOARD:BOOT_TIER:BOOT_MEASURE:BOOT_SIG:BOOT_OTA:BOOT_STORE) "
                              "is not a supported configuration" % (ota, store), out)
                # The verifier stays: the base soft row is the one offered.
                rows = supported_rows(out)
                self.assertIn(BASE_ROW, rows)
                self.assertNotIn("atmega2560:soft:soft:p256-soft:1:0", rows)
                self.assertNotIn("atmega2560:soft:soft:p256-soft:0:1", rows)
                # Refused at parse time: the error is all make printed.
                self.assertEqual(len(out.strip().splitlines()), 1, out)
                self.assertTrue(out.startswith("mk/boot.mk:"), out)

    def test_the_default_soft_row_is_the_base_row(self):
        rc, out = make("--eval", "tfw-print-boot-row: ; @echo $(BOOT_ROW)",
                       "tfw-print-boot-row", "BOARD=atmega2560", *SOFT)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out.strip(), BASE_ROW)
        # And the gate takes it: a dry run plans the ota0-store0 build.
        _, out = make("-n", "bootloader", "BOARD=atmega2560", *SOFT)
        self.assertNotIn("is not a supported configuration", out)
        self.assertIn("build/atmega2560/boot/soft-soft-p256-soft-ota0-store0/", out)


if __name__ == "__main__":
    unittest.main()
