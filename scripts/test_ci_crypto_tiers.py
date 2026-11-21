#!/usr/bin/env python3
"""Every hardware crypto tier a board profile declares is built in CI.

A board's profile.mk names its own tiers in ARCH_CRYPTO_TIERS (the
STM32H573I-DK's stm32h5, the PKA and HASH provider of #544). Only the
CRYPTO=soft image was built in CI, so a missing include in the stm32h5
provider broke `make firmware` on the bench and not in CI (#566). This test
holds .github/workflows/ci.yml to a `make firmware BOARD=<board>
CRYPTO=<tier>` step for each declared tier.

The scan reads boards/*/profile.mk only, not mk/arch/*.mk: the one arch
tier there, ESP32's hmac-p256 (mk/arch/esp32.mk), belongs to boards that set
IMAGE_TARGET and so sit outside firmware-matrix (mk/firmware.mk); they build
through esp32-build, not `make firmware`, and scanning the arch files would
demand a ci.yml step no ESP32 board can run.
"""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CI_YML = os.path.join(ROOT, ".github", "workflows", "ci.yml")
TIERS_RE = re.compile(r"^ARCH_CRYPTO_TIERS\s*:?=\s*(.*)$", re.MULTILINE)
RUN_RE = re.compile(r"^(\s*(?:-\s+)?)run:(.*)$")


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


def run_commands(text):
    """The executable lines of every `run:` in the ci.yml text.

    A `run: <cmd>` line gives its command; a `run: |` (or `>`) block gives
    the lines indented deeper than its key. Step names and `#` comments
    are not commands, so they never count as a build. The YAML is read as
    text: PyYAML is not in the image.
    """
    commands = []
    block_indent = None
    for line in text.splitlines():
        stripped = line.lstrip()
        indent = len(line) - len(stripped)
        if block_indent is not None:
            if not stripped or indent > block_indent:
                if stripped and not stripped.startswith("#"):
                    commands.append(stripped)
                continue
            block_indent = None
        m = RUN_RE.match(line)
        if not m:
            continue
        value = m.group(2).strip()
        if value[:1] in ("|", ">"):
            block_indent = len(m.group(1))
        elif value and not value.startswith("#"):
            commands.append(value)
    return commands


def ci_firmware_builds(text):
    """(board, tier) for every `make firmware BOARD= CRYPTO=` ci.yml runs."""
    return set(re.findall(r"make firmware BOARD=(\S+) CRYPTO=(\S+)",
                          "\n".join(run_commands(text))))


def missing_builds(text):
    """Declared (board, tier) rows no step in the ci.yml text builds."""
    builds = ci_firmware_builds(text)
    return [r for r in declared_tiers() if r not in builds]


class CiCryptoTiersTest(unittest.TestCase):
    def setUp(self):
        with open(CI_YML, encoding="utf-8") as f:
            self.ci = f.read()

    def test_the_dk_declares_its_stm32h5_tier(self):
        self.assertIn(("stm32h573i-dk", "stm32h5"), declared_tiers())

    def test_ci_builds_every_declared_tier(self):
        self.assertEqual(missing_builds(self.ci), [],
                         "tiers no ci.yml step builds")

    def test_the_scan_sees_a_missing_step(self):
        builds = ci_firmware_builds(
            "make firmware BOARD=stm32h573i-dk CRYPTO=soft\n")
        self.assertNotIn(("stm32h573i-dk", "stm32h5"), builds)

    def test_dropping_the_stm32h5_step_is_reported(self):
        lines = self.ci.splitlines(keepends=True)
        kept = [l for l in lines if "CRYPTO=stm32h5" not in l]
        self.assertLess(len(kept), len(lines), "ci.yml has no stm32h5 step")
        self.assertIn(("stm32h573i-dk", "stm32h5"),
                      missing_builds("".join(kept)))

    def _stm32h5_run_lines(self, lines):
        hits = [i for i, l in enumerate(lines)
                if l.lstrip().startswith("run:") and "CRYPTO=stm32h5" in l]
        self.assertTrue(hits, "ci.yml has no stm32h5 run command")
        return hits

    def test_dropping_only_the_run_command_is_reported(self):
        lines = self.ci.splitlines(keepends=True)
        hits = self._stm32h5_run_lines(lines)
        kept = [l for i, l in enumerate(lines) if i not in hits]
        self.assertIn("CRYPTO=stm32h5", "".join(kept), "step name gone too")
        self.assertIn(("stm32h573i-dk", "stm32h5"),
                      missing_builds("".join(kept)))

    def test_a_run_command_without_crypto_is_reported(self):
        lines = self.ci.splitlines(keepends=True)
        for i in self._stm32h5_run_lines(lines):
            indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
            lines[i] = indent + "run: make firmware BOARD=stm32h573i-dk\n"
        self.assertIn(("stm32h573i-dk", "stm32h5"),
                      missing_builds("".join(lines)))

    def test_a_comment_naming_the_build_does_not_count(self):
        text = ("    steps:\n"
                "      # make firmware BOARD=stm32h573i-dk CRYPTO=stm32h5\n"
                "      - name: make firmware BOARD=stm32h573i-dk CRYPTO=stm32h5\n"
                "        run: |\n"
                "          # make firmware BOARD=stm32h573i-dk CRYPTO=stm32h5\n"
                "          make firmware BOARD=stm32h573i-dk CRYPTO=soft\n")
        self.assertEqual(ci_firmware_builds(text),
                         {("stm32h573i-dk", "soft")})


if __name__ == "__main__":
    unittest.main()
