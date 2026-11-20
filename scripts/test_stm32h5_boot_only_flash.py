# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for the STM32H5's boot-only flash driver (make scripts-test; SPEC row stm32h5).

tfw-boot programs the flash through a boot-only flash-interface driver,
boot/hal/cortex-m/stm32h5/boot_flash.c. The application's EEPROM writes its own
sector through src/hal/cortex-m/stm32h5/ee_flash.c instead, confined to that
sector. These tests read what make links, without building: no STM32H5
application links boot_flash.c, on any tier or row, and the one with the
EEPROM links ee_flash.c. The board names and file names are literals.
"""

import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

BOOT_FLASH = "boot/hal/cortex-m/stm32h5/boot_flash.c"
EE_FLASH = "src/hal/cortex-m/stm32h5/ee_flash.c"
EE_FLASH_PORT = "src/hal/cortex-m/stm32h5/ee_flash_port.c"


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


def rows(board):
    """The board's firmware-matrix rows, as make argument lists."""
    rc, out = make("firmware-matrix-list")
    assert rc == 0, out
    return [line.split()[1:] for line in out.splitlines()
            if line.split() and line.split()[0] == board]


class BootFlashStaysBootOnly(unittest.TestCase):
    def test_no_stm32h5_application_links_boot_flash(self):
        for board in ("stm32h573i-dk", "nucleo-h563zi"):
            found = rows(board)
            self.assertTrue(found, board)
            for row in found:
                with self.subTest(board=board, row=row):
                    rc, out = make("firmware-srcs", "BOARD=" + board,
                                   "CRYPTO=" + row[0], *row[1:])
                    self.assertEqual(rc, 0, out)
                    self.assertNotIn(BOOT_FLASH, out.split())

    def test_the_eeprom_writes_through_the_confined_entry(self):
        rc, out = make("firmware-srcs", "BOARD=stm32h573i-dk")
        self.assertEqual(rc, 0, out)
        self.assertIn(EE_FLASH, out.split())
        self.assertIn(EE_FLASH_PORT, out.split())

    def test_a_board_with_the_eeprom_off_links_no_flash_write(self):
        rc, out = make("firmware-srcs", "BOARD=nucleo-h563zi")
        self.assertEqual(rc, 0, out)
        self.assertNotIn(EE_FLASH, out.split())
        self.assertNotIn(EE_FLASH_PORT, out.split())


if __name__ == "__main__":
    unittest.main()
