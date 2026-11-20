#!/usr/bin/env python3
"""Every hardware crypto tier a board profile declares is built in CI.

A board's profile.mk names its own tiers in ARCH_CRYPTO_TIERS (the
STM32H573I-DK's stm32h5, the PKA and HASH provider of #544). Only the
CRYPTO=soft image was built in CI, so a missing include in the stm32h5
provider broke `make firmware` on the bench and not in CI (#566). This test
holds .github/workflows/ci.yml to a `make firmware BOARD=<board>
CRYPTO=<tier>` step for each declared tier.
"""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TIERS_RE = re.compile(r"^ARCH_CRYPTO_TIERS\s*:?=\s*(.*)$", re.MULTILINE)


def declared_tiers():
    """(board, tier) for every tier a board profile declares."""
    rows = []
    boards = os.path.join(ROOT, "boards")
    for board in sorted(os.listdir(boards)):
        path = os.path.join(boards, board, "profile.mk")
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            for m in TIERS_RE.finditer(f.read()):
                rows.extend((board, tier) for tier in m.group(1).split())
    return rows


def ci_firmware_builds(text):
    """(board, tier) for every `make firmware BOARD= CRYPTO=` in ci.yml."""
    return set(re.findall(r"make firmware BOARD=(\S+) CRYPTO=(\S+)", text))


class CiCryptoTiersTest(unittest.TestCase):
    def setUp(self):
        with open(os.path.join(ROOT, ".github", "workflows", "ci.yml"),
                  encoding="utf-8") as f:
            self.builds = ci_firmware_builds(f.read())

    def test_the_dk_declares_its_stm32h5_tier(self):
        self.assertIn(("stm32h573i-dk", "stm32h5"), declared_tiers())

    def test_ci_builds_every_declared_tier(self):
        missing = [r for r in declared_tiers() if r not in self.builds]
        self.assertEqual(missing, [], "tiers no ci.yml step builds")

    def test_the_scan_sees_a_missing_step(self):
        builds = ci_firmware_builds(
            "make firmware BOARD=stm32h573i-dk CRYPTO=soft\n")
        self.assertNotIn(("stm32h573i-dk", "stm32h5"), builds)


if __name__ == "__main__":
    unittest.main()
