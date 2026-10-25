#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# `make provenance-one`'s fwhash_sha256 is the device's measurement: SHA-256
# over [base, base + image_len) of the board's application region (base 0 on
# AVR, the slot's base on mps2-an505), a gap read as erased flash (0xFF).
#
# The manifest is built by the real recipe in mk/release.mk, run by make over a
# fixture HEX. Only the toolchain is stood in for (`-o` keeps make from
# rebuilding the image, ARCH_SIZE/ARCH_CC/ARCH_RAM_USED answer for the ELF),
# because the field under test comes from the HEX alone and this runs where
# there is no avr-gcc or arm-none-eabi-gcc.

import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


fwhash = _load("fwhash")
# The fixture with a gap and its literal, the same pair test_boot_measure.c's
# test_the_fixed_image_measures_to_the_literal_fwhash_pins measures through
# tfw_boot_measure itself.
_pinned = _load("test_fwhash")
GAP_HEX = _pinned.FIXED_HEX
GAP_MEASUREMENT = _pinned.FIXED_MEASUREMENT
# What the manifest said for GAP_HEX before: objcopy -O binary's 0x00-filled file.
GAP_OLD_MANIFEST = _pinned.ZERO_GAP_MEASUREMENT

# 0x0000-0x001F: 00..1F, no gap. image_len 0x20.
NO_GAP_HEX = """\
:10000000000102030405060708090A0B0C0D0E0F78
:10001000101112131415161718191A1B1C1D1E1F68
:00000001FF
"""
NO_GAP_MEASUREMENT = "630dcd2966c4336691125448bbb25b4ff412a49c732db2c8abc1b8581bd710dd"

FAKE_SIZE = """#!/bin/sh
printf '   text\\t   data\\t    bss\\t    dec\\t    hex\\tfilename\\n'
printf '    100\\t      2\\t      0\\t    102\\t     66\\t%s\\n' "$3"
"""
FAKE_CC = """#!/bin/sh
echo 0.0.0-fake
"""


# The tier each measured board ships, which names the manifest. The 328P is
# element-only and refuses CRYPTO=soft (owner ruling 2026-10-17, m522).
TIERS = {"atmega328p": "atecc608a", "mps2-an505": "soft",
         "frdm-imx95-m7": "soft"}


def build_manifest(hex_text, board="atmega328p"):
    """Run mk/release.mk's provenance-one over hex_text, with board's real
    profile.mk at the tier it ships: (manifest, release .bin)."""
    tier = TIERS[board]
    with tempfile.TemporaryDirectory() as tmp:
        target = os.path.join(tmp, "fw")
        with open(target + ".hex", "w", encoding="ascii") as f:
            f.write(hex_text)
        with open(target + ".elf", "wb") as f:
            f.write(b"\x7fELF not an image")
        tools = {}
        for name, body in (("size", FAKE_SIZE), ("cc", FAKE_CC)):
            tools[name] = os.path.join(tmp, name)
            with open(tools[name], "w", encoding="ascii") as f:
                f.write(body)
            os.chmod(tools[name], 0o755)
        rel = os.path.join(tmp, "release")
        run = subprocess.run(
            ["make", "-s", "--no-print-directory", "-C", _ROOT,
             "-o", target + ".elf", "-o", target + ".hex",
             "provenance-one", f"BOARD={board}", f"CRYPTO={tier}",
             f"TARGET={target}", f"REL_DIR={rel}", "SOURCE_COMMIT=0000000",
             f"ARCH_SIZE={tools['size']}", f"ARCH_CC={tools['cc']}",
             "ARCH_RAM_USED=echo 7"],
            capture_output=True, text=True, check=False)
        if run.returncode != 0:
            raise AssertionError(f"make provenance-one: rc={run.returncode}\n"
                                 f"{run.stdout}{run.stderr}")
        with open(os.path.join(rel, f"{board}-{tier}.json"), encoding="utf-8") as f:
            manifest = json.load(f)
        with open(os.path.join(rel, f"{board}-{tier}.bin"), "rb") as f:
            return manifest, f.read()


class ProvenanceMeasurement(unittest.TestCase):
    def test_a_gap_is_measured_as_the_device_measures_it(self):
        m, image = build_manifest(GAP_HEX)
        self.assertEqual(m["fwhash_sha256"], GAP_MEASUREMENT)
        # The release .bin, which make suit-tbs reads, is the measured image.
        self.assertEqual(hashlib.sha256(image).hexdigest(), GAP_MEASUREMENT)
        self.assertEqual(image[0x10:0x100], b"\xff" * 0xF0)
        self.assertEqual(m["fwhash_sha256"], fwhash.measure(GAP_HEX)[0].hex())
        self.assertNotEqual(m["fwhash_sha256"], GAP_OLD_MANIFEST)
        self.assertEqual(m["image_len"], 0x120)

    def test_an_image_without_a_gap_keeps_its_manifest_hash(self):
        m, _ = build_manifest(NO_GAP_HEX)
        # objcopy -O binary's file for this image is these bytes, so the
        # manifest of an image without a gap does not move.
        self.assertEqual(hashlib.sha256(bytes(range(0x20))).hexdigest(), NO_GAP_MEASUREMENT)
        self.assertEqual(m["fwhash_sha256"], NO_GAP_MEASUREMENT)
        self.assertEqual(m["image_len"], 0x20)

    def test_the_other_fields_are_unchanged(self):
        m, _ = build_manifest(NO_GAP_HEX)
        self.assertEqual(list(m), [
            "board", "arch", "crypto_tier", "mcu", "f_cpu_hz", "version",
            "source_commit", "compiler", "toolchain", "flash_bytes", "ram_bytes",
            "elf_sha256", "hex_sha256", "reproducible", "image_len",
            "fwhash_sha256", "note"])
        self.assertEqual(m["hex_sha256"], hashlib.sha256(NO_GAP_HEX.encode()).hexdigest())
        self.assertEqual((m["flash_bytes"], m["ram_bytes"]), (102, 7))

    def test_the_note_names_the_measured_image(self):
        m, _ = build_manifest(NO_GAP_HEX)
        self.assertIn("the measurement tfw-boot takes", m["note"])
        self.assertIn("[base, base + image_len)", m["note"])
        self.assertIn("(0 on this board)", m["note"])
        self.assertIn("read as 0xFF", m["note"])
        self.assertNotIn("application binary", m["note"])

    def test_a_malformed_hex_writes_no_manifest(self):
        with self.assertRaises(AssertionError):
            build_manifest(GAP_HEX.replace(":00000001FF\n", ""))


class SlotBoard(unittest.TestCase):
    """mps2-an505: the application slot at 0x38000000, nothing at flash 0. The
    region comes from its profile.mk (FWHASH_BASE, FWHASH_SIZE), as the boot
    HAL reads it: tfw_boot_measure_from over TFW_APP_REGION_BASE."""

    def test_a_slot_image_measures_from_the_slot_base(self):
        m, image = build_manifest(_pinned.SLOT_HEX, "mps2-an505")
        self.assertEqual(m["fwhash_sha256"], GAP_MEASUREMENT)
        self.assertEqual(m["image_len"], 0x120)
        self.assertEqual(hashlib.sha256(image).hexdigest(), GAP_MEASUREMENT)
        self.assertIn("(0x38000000 on this board)", m["note"])
        self.assertEqual((m["arch"], m["f_cpu_hz"]), ("cortex-m", 20000000))

    def test_a_record_outside_the_slot_writes_no_manifest(self):
        for text in (_pinned.BOOT_REGION_HEX, _pinned.PAST_SLOT_HEX, GAP_HEX):
            with self.subTest(text=text), self.assertRaises(AssertionError):
                build_manifest(text, "mps2-an505")

    def test_an_avr_board_refuses_a_slot_image(self):
        with self.assertRaises(AssertionError):
            build_manifest(_pinned.SLOT_HEX)


# The fixture at frdm-imx95-m7's application slot, 0x00008000: GAP_HEX with each
# record's address moved up by 0x8000, so the same bytes from the slot's base.
ITCM_SLOT_HEX = """\
:10800000000102030405060708090A0B0C0D0E0FF8
:108100005A5B58595E5F5C5D5253505156575455F7
:108110004A4B48494E4F4C4D4243404146474445E7
:00000001FF
"""
# tfw-boot's region at the ITCM's base, below the slot.
ITCM_BOOT_REGION_HEX = ":01000000AA55\n:00000001FF\n"
# The first byte past the 192 KiB slot, 0x00038000 (the platform block's side).
ITCM_PAST_SLOT_HEX = ":020000040003F7\n:01800000AAD5\n:00000001FF\n"


class ItcmSlotBoard(unittest.TestCase):
    """frdm-imx95-m7: the application slot at 0x00008000 in the M7's ITCM,
    tfw-boot's region below it. Its profile.mk sets FWHASH_BASE, FWHASH_SIZE
    and F_CPU as board_profile.h's TFW_APP_REGION_BASE, TFW_APP_REGION_SIZE
    and TFW_CM_CPU_HZ (#49)."""

    def test_a_slot_image_measures_from_the_slot_base(self):
        m, image = build_manifest(ITCM_SLOT_HEX, "frdm-imx95-m7")
        self.assertEqual(m["fwhash_sha256"], GAP_MEASUREMENT)
        self.assertEqual(m["image_len"], 0x120)
        self.assertEqual(hashlib.sha256(image).hexdigest(), GAP_MEASUREMENT)
        self.assertIn("(0x00008000 on this board)", m["note"])
        self.assertEqual((m["arch"], m["f_cpu_hz"]), ("cortex-m", 800000000))

    def test_a_record_outside_the_slot_writes_no_manifest(self):
        for text in (ITCM_BOOT_REGION_HEX, ITCM_PAST_SLOT_HEX, GAP_HEX):
            with self.subTest(text=text), self.assertRaises(AssertionError):
                build_manifest(text, "frdm-imx95-m7")


if __name__ == "__main__":
    unittest.main()
