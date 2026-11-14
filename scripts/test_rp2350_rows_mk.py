#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The Pico 2 W's firmware rows as mk/firmware.mk selects them (row rp2350,
#429): each row's image directory is one word, and the RP2350's flash EEPROM
with tfw-boot's flash helper is linked only in the row that has the EEPROM
feature; the default and I2C=1 rows link the core hal_eeprom.c, which keeps
ota_ctl.c's and app_runner.c's entry points. Make variables only: nothing is
compiled, so no cross toolchain is needed."""

import os
import subprocess
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PRINT = "tfw-print-%: ; @echo $($*)"
BOARD = ("BOARD=rp2350-pico2w", "CRYPTO=soft")

SOC_EEPROM = "src/hal/cortex-m/rp2350/hal_eeprom.c"
CORE_EEPROM = "src/hal/cortex-m/hal_eeprom.c"
FLASH_HELPER = ("boot/hal/cortex-m/rp2350/rp_flash.c",
                "boot/hal/cortex-m/rp2350/rp_flash_rom.c",
                "boot/hal/cortex-m/rp2350/rp_rom_lookup.c")


def var(name, *args):
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", "--eval", PRINT, "tfw-print-" + name,
         *BOARD, *args],
        cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if p.returncode != 0:
        raise AssertionError(f"make print {name}: {p.stderr}")
    return p.stdout.strip(), p.stderr


class Rp2350RowDirectory(unittest.TestCase):

    def test_each_row_is_one_directory_word(self):
        for row, suffix in (((), "soft"), (("I2C=1",), "soft-i2c"),
                            (("PERIPH=1",), "soft-analog-pwm-tone-eeprom")):
            with self.subTest(row=row):
                fw_dir, err = var("FW_DIR", *row)
                self.assertEqual(fw_dir, "build/rp2350-pico2w/" + suffix)
                self.assertNotIn("overriding recipe", err)


class Rp2350EepromSelection(unittest.TestCase):

    def srcs(self, *row):
        return var("FW_SRCS", *row)[0].split()

    def test_rows_without_the_eeprom_link_the_core_copy_and_no_flash_helper(self):
        for row in ((), ("I2C=1",)):
            with self.subTest(row=row):
                srcs = self.srcs(*row)
                self.assertIn(CORE_EEPROM, srcs)
                self.assertNotIn(SOC_EEPROM, srcs)
                for helper in FLASH_HELPER:
                    self.assertNotIn(helper, srcs)

    def test_the_peripheral_row_links_the_flash_eeprom_and_its_helper(self):
        srcs = self.srcs("PERIPH=1")
        self.assertIn(SOC_EEPROM, srcs)
        self.assertNotIn(CORE_EEPROM, srcs)
        for helper in FLASH_HELPER:
            self.assertIn(helper, srcs)


if __name__ == "__main__":
    unittest.main()
