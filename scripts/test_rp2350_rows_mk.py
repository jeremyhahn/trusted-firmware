#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The Pico 2 W's firmware rows as mk/firmware.mk selects them (row rp2350,
#429): each row's image directory is one word; the default row links the I2C
bus, the converter, PWM, the tone and the RP2350's flash EEPROM with tfw-boot's
flash helper; the PERIPH=0 row links the core hal_eeprom.c, which keeps
ota_ctl.c's and app_runner.c's entry points, and no flash helper. The owner
lifted the AVR gate for rp2350 (ruling 697), so `make flash` refuses no row.
Make variables and recipes only: nothing is compiled, so no cross toolchain is
needed."""

import os
import subprocess
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PRINT = "tfw-print-%: ; @echo $($*)"
BOARD = ("BOARD=rp2350-pico2w", "CRYPTO=soft")

SOC_EEPROM = "src/hal/cortex-m/rp2350/hal_eeprom.c"
CORE_EEPROM = "src/hal/cortex-m/hal_eeprom.c"
FLASH_HELPER = ("boot/hal/cortex-m/rp2350/rp_flash.c",
                "boot/hal/cortex-m/rp2350/rp_flash_rom.c",
                "boot/hal/cortex-m/rp2350/rp_rom_lookup.c")
DEFAULT_PERIPHS = ("src/hal/cortex-m/rp2350/hal_bus.c", "src/caps/bus.c",
                   "src/hal/cortex-m/rp2350/hal_adc.c", "src/caps/analog.c",
                   "src/hal/cortex-m/rp2350/hal_pwm.c", "src/caps/pwm.c",
                   "src/hal/cortex-m/rp2350/hal_tone.c", "src/caps/tone.c",
                   SOC_EEPROM, *FLASH_HELPER)


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
        for row, suffix in (((), "soft-i2c-analog-pwm-tone-eeprom"),
                            (("I2C=0",), "soft-analog-pwm-tone-eeprom"),
                            (("PERIPH=0",), "soft-i2c"),
                            (("I2C=0", "PERIPH=0"), "soft")):
            with self.subTest(row=row):
                fw_dir, err = var("FW_DIR", *row)
                self.assertEqual(fw_dir, "build/rp2350-pico2w/" + suffix)
                self.assertNotIn("overriding recipe", err)


class Rp2350EepromSelection(unittest.TestCase):

    def srcs(self, *row):
        return var("FW_SRCS", *row)[0].split()

    def test_rows_without_the_eeprom_link_the_core_copy_and_no_flash_helper(self):
        for row in (("PERIPH=0",), ("I2C=0", "PERIPH=0")):
            with self.subTest(row=row):
                srcs = self.srcs(*row)
                self.assertIn(CORE_EEPROM, srcs)
                self.assertNotIn(SOC_EEPROM, srcs)
                for helper in FLASH_HELPER:
                    self.assertNotIn(helper, srcs)

    def test_the_eeprom_rows_link_the_flash_eeprom_and_its_helper(self):
        for row in ((), ("PERIPH=1",), ("I2C=0",)):
            with self.subTest(row=row):
                srcs = self.srcs(*row)
                self.assertIn(SOC_EEPROM, srcs)
                self.assertNotIn(CORE_EEPROM, srcs)
                for helper in FLASH_HELPER:
                    self.assertIn(helper, srcs)

    def test_the_default_row_links_every_peripheral(self):
        srcs = self.srcs()
        for src in DEFAULT_PERIPHS:
            self.assertIn(src, srcs)


def flash(*row, picotool=True):
    """`make flash` on a row, with its image taken as built (-o): the recipe
    only, so nothing is compiled. Stand-ins first on PATH, picotool and the
    toolchain's objcopy, log any call and fail; with picotool=False there is no
    picotool stand-in, and PATH is one directory of links to every other tool
    on the host's PATH, so no picotool is found and make, sh and the rest are. Either way the result does not
    depend on whether the host has the real one, and with no record in an
    empty record directory nothing could reach a board. Returns the exit
    code, stderr and the stand-in's call log."""
    fw_dir = var("FW_DIR", *row)[0]
    with tempfile.TemporaryDirectory() as tmp:
        tools = os.path.join(tmp, "bin")
        records = os.path.join(tmp, "records")
        calls = os.path.join(tmp, "picotool.calls")
        os.makedirs(tools)
        os.makedirs(records)
        # The objcopy only satisfies the Makefile's toolchain check: every
        # run stops before the recipe would call it.
        for name in ("picotool", "arm-none-eabi-objcopy")[0 if picotool else 1:]:
            tool = os.path.join(tools, name)
            with open(tool, "w", encoding="utf-8") as f:
                f.write(f'#!/bin/sh\necho {name} "$@" >> "{calls}"\nexit 1\n')
            os.chmod(tool, 0o755)
        # Every other executable the host's PATH finds, linked beside the
        # stand-ins, the first of each name as a lookup would find it: a
        # directory that also holds make or sh is not dropped with picotool.
        for d in os.environ.get("PATH", "").split(os.pathsep):
            if not os.path.isdir(d):
                continue
            for name in os.listdir(d):
                src = os.path.join(d, name)
                dst = os.path.join(tools, name)
                if (name == "picotool" or os.path.lexists(dst) or os.path.isdir(src)
                        or not os.access(src, os.X_OK)):
                    continue
                os.symlink(src, dst)
        env = dict(os.environ, PATH=tools)
        p = subprocess.run(
            ["make", "-s", "--no-print-directory", "-o", fw_dir + "/trusted-firmware.hex",
             "flash", *BOARD, *row, "CM_PROBE_SERIAL=modelprobe", "CM_RECORD_DIR=" + records],
            cwd=REPO, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            check=False)
        log = ""
        if os.path.exists(calls):
            with open(calls, encoding="utf-8") as f:
                log = f.read()
    return p.returncode, p.stderr, log


class Rp2350FlashRows(unittest.TestCase):

    def test_every_row_reaches_the_write_path(self):
        for row in ((), ("I2C=1",), ("PERIPH=1",), ("I2C=0",), ("PERIPH=0",)):
            with self.subTest(row=row, picotool=True):
                rc, err, calls = flash(*row)
                self.assertNotEqual(rc, 0)
                self.assertIn("flash: REFUSED. No whole factory record of rp2350-pico2w", err)
                self.assertNotIn("host-test only", err)
                self.assertNotIn("AVR gate", err)
                self.assertEqual(calls, "")  # the guard stops before any picotool call
            with self.subTest(row=row, picotool=False):
                rc, err, calls = flash(*row, picotool=False)
                self.assertNotEqual(rc, 0)
                self.assertIn("flash: picotool is not on PATH", err)
                self.assertNotIn("host-test only", err)
                self.assertNotIn("AVR gate", err)
                self.assertEqual(calls, "")


if __name__ == "__main__":
    unittest.main()
