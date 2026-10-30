# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for which swap part a tfw-boot store row links (make scripts-test).

Owner queue 176, option (a): the ATmega328P's store row keeps the signature
check and the activation and drops the restore from PREV, so it links the
stub (boot/boot_swap_none.c); the 32U4 and the single-slot MPS2 store rows keep
boot/boot_prev.c, and the 2560's two-slot store row keeps boot/boot_swap.c. These
tests read the object list make decides (BOOT_SRCS in the printed database),
without building, so they need no AVR or Arm toolchain.
"""

import os
import re
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

PREV = "boot/boot_prev.c"
SWAP = "boot/boot_swap.c"
NONE = "boot/boot_swap_none.c"
STAGE = "boot/boot_stage.c"
COSE = "boot/boot_cose.c"


def boot_srcs(board, store, tier="atecc608a", sig="atecc608a"):
    """BOOT_SRCS for a row of board (the element tier by default), as make resolves it."""
    p = subprocess.run(
        ["make", "-pn", "-s", "--no-print-directory", "BOARD=" + board,
         "BOOT_TIER=" + tier, "BOOT_MEASURE=" + tier, "BOOT_SIG=" + sig,
         "BOOT_OTA=0", "BOOT_STORE=" + store, "boot-size-check"],
        cwd=REPO, capture_output=True, text=True, check=False)
    m = re.search(r"^BOOT_SRCS :?= (.*)$", p.stdout, re.MULTILINE)
    if m is None:
        raise AssertionError("no BOOT_SRCS in make's database: " + p.stderr[-400:])
    return m.group(1).split()


class BootPrevRow(unittest.TestCase):
    def test_the_328p_store_row_has_no_prev_restore(self):
        srcs = boot_srcs("atmega328p", "1")
        self.assertNotIn(PREV, srcs)
        self.assertNotIn(SWAP, srcs)
        self.assertIn(NONE, srcs)

    def test_the_328p_store_row_keeps_the_copy_and_the_signature_check(self):
        srcs = boot_srcs("atmega328p", "1")
        self.assertIn(STAGE, srcs)
        self.assertIn(COSE, srcs)

    def test_the_328p_base_row_is_unchanged(self):
        srcs = boot_srcs("atmega328p", "0")
        self.assertIn(NONE, srcs)
        self.assertNotIn(PREV, srcs)

    def test_the_32u4_store_row_keeps_the_restore(self):
        srcs = boot_srcs("atmega32u4", "1")
        self.assertIn(PREV, srcs)
        self.assertNotIn(NONE, srcs)

    def test_the_2560_store_row_keeps_its_swap(self):
        srcs = boot_srcs("atmega2560", "1")
        self.assertIn(SWAP, srcs)
        self.assertNotIn(PREV, srcs)

    def test_the_single_slot_mps2_store_row_keeps_the_restore(self):
        srcs = boot_srcs("mps2-an505", "1", tier="soft", sig="p256-soft")
        self.assertIn(PREV, srcs)


if __name__ == "__main__":
    unittest.main()
