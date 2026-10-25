# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The ATmega328P's rows, as mk/firmware.mk reads them (make scripts-test). No network.

Owner ruling 2026-10-17 (m522, item 79): the ATmega328P is supported only with a
secure element. These ask the build itself -- board-tiers, firmware-matrix-list
and a refused CRYPTO=soft -- so a profile that brings a soft 328P row back fails
here, without the AVR toolchain, before firmware-matrix ever builds it.
"""
import os
import subprocess
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def make(*args):
    # Not the parent make's flags: a CRYPTO= on `make scripts-test` would
    # otherwise reach these as a command-line variable.
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
    return subprocess.run(["make", "-s", "--no-print-directory", "-C", ROOT] + list(args),
                          capture_output=True, text=True, check=False, env=env)


class NanoRowsTest(unittest.TestCase):
    def test_the_nanos_one_tier_is_the_element(self):
        got = make("board-tiers", "BOARD=atmega328p")
        self.assertEqual(0, got.returncode, got.stderr)
        self.assertEqual("atecc608a", got.stdout.strip())

    def test_the_matrix_builds_no_soft_nano(self):
        got = make("firmware-matrix-list")
        self.assertEqual(0, got.returncode, got.stderr)
        nano = [line.split() for line in got.stdout.splitlines() if line.startswith("atmega328p ")]
        self.assertEqual([["atmega328p", "atecc608a"]], nano)

    def test_crypto_soft_on_the_nano_is_refused(self):
        got = make("board-tiers", "BOARD=atmega328p", "CRYPTO=soft")
        self.assertNotEqual(0, got.returncode)
        self.assertIn("CRYPTO=soft is refused on atmega328p", got.stderr)

    def test_the_mega_keeps_its_soft_and_auto_tiers(self):
        got = make("board-tiers", "BOARD=atmega2560", "CRYPTO=soft")
        self.assertEqual(0, got.returncode, got.stderr)
        self.assertEqual(["soft", "atecc608a", "auto"], got.stdout.split())


class MeasureSoftCSRTest(unittest.TestCase):
    # MEASURE_SOFT_CSR=1 (issue #53 T3) measures the one soft AVR row left.
    def test_it_measures_the_megas_soft_row(self):
        got = make("board-tiers", "BOARD=atmega2560", "CRYPTO=soft", "MEASURE_SOFT_CSR=1")
        self.assertEqual(0, got.returncode, got.stderr)

    def test_it_is_refused_on_the_nano(self):
        got = make("board-tiers", "BOARD=atmega328p", "MEASURE_SOFT_CSR=1")
        self.assertNotEqual(0, got.returncode)
        self.assertIn("MEASURE_SOFT_CSR=1 is refused on atmega328p", got.stderr)

    def test_it_is_refused_on_the_megas_other_tiers(self):
        got = make("board-tiers", "BOARD=atmega2560", "CRYPTO=atecc608a", "MEASURE_SOFT_CSR=1")
        self.assertNotEqual(0, got.returncode)
        self.assertIn("MEASURE_SOFT_CSR=1 measures CRYPTO=soft, not CRYPTO=atecc608a", got.stderr)


if __name__ == "__main__":
    unittest.main()
