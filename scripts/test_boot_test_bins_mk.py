#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Every HAL-local boot suite is in BOOT_TEST_BINS (row #621). mk/boot.mk
fills BOOT_TEST_BINS from the test/unit/boot/test_*.c wildcard; a suite whose
source sits beside its SoC's sources (boot/hal/, src/hal/, or a
$(..._BOOT_TEST_DIR) directory) is outside that wildcard and needs its own
`BOOT_TEST_BINS += $(BOOT_TEST_DIR)/<name>` line, or `make boot-test` stops
building and running it. The makefile text only: no make run."""

import os
import re
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOT_MK = os.path.join(REPO, "mk", "boot.mk")

# HAL-local suites known to be missing; none today.
KNOWN_MISSING = ()

RULE = re.compile(r"^\$\(BOOT_TEST_DIR\)/(test_[\w-]+)\s*:\s*(\S+)")
VAR = re.compile(r"^(\w+)\s*:=\s*(\S+)\s*$")
BINS_ADD = re.compile(r"^BOOT_TEST_BINS\s*\+=(.*)$")
VAR_REF = re.compile(r"^\$\((\w+)\)/")


def logical_lines(text):
    """The makefile's lines with backslash continuations joined."""
    out, cur = [], ""
    for line in text.splitlines():
        if line.endswith("\\"):
            cur += line[:-1] + " "
            continue
        out.append(cur + line)
        cur = ""
    if cur:
        out.append(cur)
    return out


def missing_hal_suites(text):
    """Names of HAL-local boot suites with a rule and no BOOT_TEST_BINS += line."""
    lines = logical_lines(text)
    variables, registered, suites = {}, set(), []
    for line in lines:
        m = VAR.match(line)
        if m:
            variables[m.group(1)] = m.group(2)
        m = BINS_ADD.match(line)
        if m:
            registered.update(m.group(1).split())
    for line in lines:
        m = RULE.match(line)
        if not m:
            continue
        name, src = m.groups()
        ref = VAR_REF.match(src)
        if ref and ref.group(1) in variables:
            src = variables[ref.group(1)] + src[ref.end() - 1:]
        if not (os.path.basename(src).startswith("test_") and src.endswith(".c")):
            continue
        if src.startswith("test/unit/boot/"):
            continue
        hal_local = (src.startswith("boot/hal/") or src.startswith("src/hal/")
                     or (ref is not None and ref.group(1).endswith("_BOOT_TEST_DIR")))
        if hal_local and "$(BOOT_TEST_DIR)/" + name not in registered:
            suites.append(name)
    return suites


class BootTestBins(unittest.TestCase):

    def test_every_hal_local_boot_suite_is_in_boot_test_bins(self):
        with open(BOOT_MK, encoding="utf-8") as f:
            missing = [n for n in missing_hal_suites(f.read()) if n not in KNOWN_MISSING]
        self.assertEqual(missing, [],
                         "HAL-local boot suites missing from BOOT_TEST_BINS: " + ", ".join(missing))

    def test_the_checker_names_an_unregistered_suite(self):
        text = ("X_BOOT_TEST_DIR := boot/hal/x/test\n"
                "BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_kept\n"
                "$(BOOT_TEST_DIR)/test_kept: src/hal/x/test/test_kept.c \\\n"
                "        src/hal/x/kept.c | $(BOOT_TEST_DIR)\n"
                "$(BOOT_TEST_DIR)/test_lost: boot/hal/x/test/test_lost.c | $(BOOT_TEST_DIR)\n"
                "$(BOOT_TEST_DIR)/test_lost_var: $(X_BOOT_TEST_DIR)/test_lost_var.c\n"
                "$(BOOT_TEST_DIR)/test_wild: test/unit/boot/test_wild.c\n")
        self.assertEqual(missing_hal_suites(text), ["test_lost", "test_lost_var"])


if __name__ == "__main__":
    unittest.main()
