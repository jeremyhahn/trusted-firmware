# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests that docs/boards/esp32-qemu.md cites mk/arch/esp32-targets.mk at the lines it names (make scripts-test).

The page cites the build, the test, its three builds and its description by
line; a comment line added above them moves every one (#760 did, a day after
#753 set them). Each span is found here by its content, never by a line number
written into the test, and each cite on the page must equal one of them. The
page's mk/arch/esp32.mk cites must name the pin the page quotes beside them.
Text only: no make, no ESP-IDF, no QEMU."""

import os
import re
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PAGE = "docs/boards/esp32-qemu.md"
TARGETS_MK = "mk/arch/esp32-targets.mk"
ESP32_MK = "mk/arch/esp32.mk"

# `mk/arch/esp32-targets.mk:61`–`134`, the en dash (U+2013) between; a cite
# of one line has no second number.
TARGETS_CITE = re.compile(
    r"`mk/arch/esp32-targets\.mk:(\d+)`(?:–`(\d+)`)?")
ESP32_CITE = re.compile(r"`mk/arch/esp32\.mk:(\d+)`")
ASSIGN = re.compile(r"^([A-Z0-9_]+)\s*:=\s*(\S+)\s*$")

# The variables of mk/arch/esp32.mk the page must cite, by name.
ESP32_PINS = ("ESP_IDF_VERSION", "ESP_QEMU_VERSION")


def read(path):
    with open(os.path.join(REPO, path), encoding="utf-8") as f:
        return f.read()


def spans(mk_text):
    """The four spans of esp32-targets.mk the page cites, 1-based (first, last).

    build: the rule esp32-build: to the last line of its recipe, the line
    before the next blank one; test: the ## esp32-qemu-test: help line to the
    line that says it is green; builds: the three esp32-build BOARD=esp32-qemu
    lines; description: the help line to the last comment line before the
    rule esp32-qemu-test:."""
    lines = mk_text.split("\n")

    def first(pred, start=0):
        for i in range(start, len(lines)):
            if pred(lines[i]):
                return i
        raise ValueError("%s: no line found" % TARGETS_MK)

    b0 = first(lambda s: s.startswith("esp32-build:"))
    b1 = first(lambda s: not s.strip(), b0) - 1

    t0 = first(lambda s: s.startswith("## esp32-qemu-test:"))
    t1 = first(lambda s: "esp32-qemu-test: green" in s, t0)

    builds = [i for i, s in enumerate(lines)
              if "esp32-build BOARD=esp32-qemu" in s]
    if len(builds) != 3 or builds[-1] - builds[0] != 2:
        raise ValueError("%s: esp32-build BOARD=esp32-qemu on lines %s, not "
                         "three consecutive" % (TARGETS_MK,
                                                [i + 1 for i in builds]))

    rule = first(lambda s: s.startswith("esp32-qemu-test:"), t0)
    d1 = max(i for i in range(t0, rule) if lines[i].startswith("#"))

    return {
        "build": (b0 + 1, b1 + 1),
        "test": (t0 + 1, t1 + 1),
        "builds": (builds[0] + 1, builds[-1] + 1),
        "description": (t0 + 1, d1 + 1),
    }


def targets_cites(page_text):
    """Every esp32-targets.mk cite on the page: (page line, first, last)."""
    out = []
    for n, line in enumerate(page_text.split("\n"), 1):
        for m in TARGETS_CITE.finditer(line):
            a = int(m.group(1))
            out.append((n, a, int(m.group(2) or a)))
    return out


def check_targets_cites(page_text, mk_text):
    """Problems with the page's esp32-targets.mk cites: each equals a span,
    and each span is cited."""
    want = spans(mk_text)
    problems = []
    cited = set()
    for n, a, b in targets_cites(page_text):
        hit = [k for k, v in want.items() if v == (a, b)]
        if hit:
            cited.update(hit)
            continue
        name, (f, l) = min(want.items(),
                           key=lambda kv: abs(kv[1][0] - a) + abs(kv[1][1] - b))
        problems.append("%s:%d cites %s:%d–%d; the %s span is %d–%d"
                        % (PAGE, n, TARGETS_MK, a, b, name, f, l))
    for name in sorted(set(want) - cited):
        f, l = want[name]
        problems.append("%s cites no %s:%d–%d, the %s span"
                        % (PAGE, TARGETS_MK, f, l, name))
    return problems


def check_esp32_cites(page_text, mk_text):
    """Problems with the page's esp32.mk cites: each names an assignment
    whose value the page quotes on the same line, and each pin is cited."""
    mk_lines = mk_text.split("\n")
    problems = []
    cited = set()
    for n, line in enumerate(page_text.split("\n"), 1):
        for m in ESP32_CITE.finditer(line):
            k = int(m.group(1))
            a = ASSIGN.match(mk_lines[k - 1]) if k <= len(mk_lines) else None
            if not a:
                problems.append("%s:%d cites %s:%d, which is no assignment"
                                % (PAGE, n, ESP32_MK, k))
                continue
            cited.add(a.group(1))
            if "`%s`" % a.group(2) not in line:
                problems.append("%s:%d cites %s:%d, %s := %s, and does not "
                                "quote that value beside it"
                                % (PAGE, n, ESP32_MK, k, a.group(1),
                                   a.group(2)))
    for name in ESP32_PINS:
        if name not in cited:
            problems.append("%s cites no %s line of %s"
                            % (PAGE, name, ESP32_MK))
    return problems


class Esp32QemuPageTest(unittest.TestCase):

    maxDiff = None

    def setUp(self):
        self.page = read(PAGE)
        self.targets = read(TARGETS_MK)

    def test_spans_found(self):
        want = spans(self.targets)
        for name, (f, l) in want.items():
            self.assertLessEqual(f, l, name)

    def test_targets_cites(self):
        self.assertEqual(check_targets_cites(self.page, self.targets), [])

    def test_esp32_cites(self):
        self.assertEqual(check_esp32_cites(self.page, read(ESP32_MK)), [])

    def test_line_added_above_moves_every_cite(self):
        problems = check_targets_cites(self.page, "# one more line\n"
                                       + self.targets)
        lines = sorted({n for n, _, _ in targets_cites(self.page)})
        self.assertGreaterEqual(len(lines), 4)
        for n in lines:
            self.assertTrue(
                any(p.startswith("%s:%d " % (PAGE, n)) for p in problems),
                "%s:%d not reported: %s" % (PAGE, n, problems))


if __name__ == "__main__":
    unittest.main()
