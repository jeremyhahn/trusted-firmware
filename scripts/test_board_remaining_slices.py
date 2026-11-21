#!/usr/bin/env python3
"""A board page's open slice whose proving test already exists (make scripts-test). No network.

Issue #621: docs/boards/rp2350-pico2w.md and nrf54l15-dk.md kept slice 1 under
"Remaining slices" after #500 and #529 landed it, with the test the item named
on main. This reads every docs/boards/*.md "### Remaining slices" list and
fails on an item not marked **done** whose proving test is already on disk:

- "new `path`" (and "and `name`" after it, in the same directory): the item
  names a new test file, and that file exists;
- "a new test in `path`" or "a new case in `path`": the item extends a file
  that already exists, so the file alone proves nothing. It counts only when
  the file also names a C identifier the item quotes in backticks (for
  rp2350 slice 1, TFW_BOOT_CONFIRM_ADDR). Without that tightening the
  mcxn947 slice 1 (an owner's call, not built) and the rp2350 slice 6 (an
  OTP write, not built) would be flagged for test files that predate them.
"""
import glob
import os
import re
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HEADING = "### Remaining slices"
ITEM = re.compile(r"^\d+\. ")
NEW_FILE = re.compile(r"\bnew\s+`([^`]+)`((?:\s*(?:,|and)\s*`[^`]+`)*)")
NEW_IN = re.compile(r"\bnew\s+(?:test|case)\s+in\s+`([^`]+)`")
QUOTED = re.compile(r"`([^`]+)`")
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def is_test_file(path):
    base = os.path.basename(path)
    return (base.startswith("test_") and base.endswith((".c", ".py"))) or base.endswith("_test.go")


def remaining_items(text):
    """The items of a page's "Remaining slices" list, each joined to one line."""
    items, cur, inside = [], None, False
    for line in text.splitlines():
        if line.startswith("#"):
            inside = line.strip() == HEADING
            if cur is not None:
                items.append(cur)
                cur = None
            continue
        if not inside:
            continue
        if ITEM.match(line):
            if cur is not None:
                items.append(cur)
            cur = line
        elif cur is not None and line.strip():
            cur += " " + line.strip()
    if cur is not None:
        items.append(cur)
    return items


def proving_tests(item, root=ROOT):
    """The test files under root that already prove the item."""
    found = []
    for m in NEW_FILE.finditer(item):
        first = m.group(1)
        names = [first] + QUOTED.findall(m.group(2))
        for name in names:
            path = name if "/" in name else os.path.join(os.path.dirname(first), name)
            if is_test_file(path) and os.path.isfile(os.path.join(root, path)):
                found.append(path)
    idents = [q for q in QUOTED.findall(item) if IDENT.match(q)]
    for m in NEW_IN.finditer(item):
        path = m.group(1)
        full = os.path.join(root, path)
        if not (is_test_file(path) and os.path.isfile(full) and idents):
            continue
        with open(full, encoding="utf-8") as f:
            body = f.read()
        if any(re.search(r"\b%s\b" % re.escape(i), body) for i in idents):
            found.append(path)
    return found


def stale_items(root=ROOT):
    """(page, item number, tests) for each open item whose test is on disk."""
    out = []
    for page in sorted(glob.glob(os.path.join(root, "docs", "boards", "*.md"))):
        with open(page, encoding="utf-8") as f:
            text = f.read()
        for item in remaining_items(text):
            if "**done**" in item:
                continue
            tests = proving_tests(item, root)
            if tests:
                out.append((os.path.relpath(page, root), item.split(".", 1)[0], tests))
    return out


class RemainingSlicesTest(unittest.TestCase):
    def test_an_open_slice_has_no_proving_test_on_disk(self):
        self.assertEqual([], stale_items(),
                         "mark these items **done**, naming the PR and the test")


class MatcherTest(unittest.TestCase):
    """The matcher over a scratch tree, so a planned test file landing in this
    repository never changes these cases (Codex P2 on #622)."""

    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = scratch.name

    def write(self, path, body=""):
        full = os.path.join(self.root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as f:
            f.write(body)

    def test_the_matcher_reads_a_new_file_and_its_sibling(self):
        self.write("boot/hal/cortex-m/nrf54l/test/test_boot_nrf54l_uart.c")
        self.write("boot/hal/cortex-m/nrf54l/test/test_boot_nrf54l_rram.c")
        item = ("1. **x**: proven by the new `boot/hal/cortex-m/nrf54l/test/test_boot_nrf54l_uart.c`"
                " and `test_boot_nrf54l_rram.c`.")
        self.assertEqual(["boot/hal/cortex-m/nrf54l/test/test_boot_nrf54l_uart.c",
                          "boot/hal/cortex-m/nrf54l/test/test_boot_nrf54l_rram.c"],
                         proving_tests(item, self.root))

    def test_a_new_case_in_an_existing_file_needs_a_quoted_identifier(self):
        item = "1. **x**: a new case in `test/unit/test_rp2350_eeprom.c`. It writes `TFW_BOOT_CONFIRM_ADDR`."
        self.write("test/unit/test_rp2350_eeprom.c", "static void test_eeprom_read(void) {}\n")
        self.assertEqual([], proving_tests(item, self.root))
        self.write("test/unit/test_rp2350_eeprom.c", "static uint32_t a = TFW_BOOT_CONFIRM_ADDR;\n")
        self.assertEqual(["test/unit/test_rp2350_eeprom.c"], proving_tests(item, self.root))

    def test_a_missing_new_file_is_not_flagged(self):
        item = "5. **x**: proven by a new `test/unit/boot/test_boot_rp2350_otp.c` over an OTP model."
        self.assertEqual([], proving_tests(item, self.root))

    def test_stale_items_flags_an_open_item_whose_test_is_in_its_root(self):
        self.write("test/unit/test_x_open.c")
        self.write("test/unit/test_x_done.c")
        self.write("docs/boards/x.md",
                   "# x\n\n### Remaining slices\n\n"
                   "1. **open**: proven by a new `test/unit/test_x_open.c`.\n"
                   "2. **done** (#1): proven by a new `test/unit/test_x_done.c`.\n"
                   "\n## Next\n")
        self.assertEqual([("docs/boards/x.md", "1", ["test/unit/test_x_open.c"])],
                         stale_items(self.root))

if __name__ == "__main__":
    unittest.main()
