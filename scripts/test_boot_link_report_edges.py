"""scripts/boot_link_report.sh's edges (#640).

test_boot_link_report.py pins the two classes with link.err files that end in
a newline. These pin the rest: a last line with no newline still counts, a
"will not fit" line alone is a size failure, the size report keeps only the
overflow, fit, overlap and undefined-reference lines, the error path names vla
and alloca lines, and a missing link.err prints only the header.

BOOT_LINK_REPORT_SCRIPT overrides the script under test (for a mutation run).
"""

import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.environ.get("BOOT_LINK_REPORT_SCRIPT",
                        os.path.join(HERE, "boot_link_report.sh"))
SIZE_HEADER = ("tfw-boot does not link for atmega328p: "
               "it does not fit in the boot section.\n")
ERR_HEADER = "tfw-boot does not link for atmega328p:\n"


def run_path(path, board="atmega328p"):
    p = subprocess.run(["sh", SCRIPT, path, board],
                       capture_output=True, text=True, check=False)
    return p.returncode, p.stdout


def run(text, board="atmega328p"):
    # The text is written as given: no newline is added at the end.
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "link.err")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        return run_path(path, board)


class BootLinkReportEdgesTest(unittest.TestCase):
    def test_overflow_without_trailing_newline_is_a_size_failure(self):
        rc, out = run("ld: region `text' overflowed by 4 bytes")
        self.assertEqual(rc, 0, out)
        self.assertEqual(
            out,
            SIZE_HEADER + "\n" + "  ld: ld: region `text' overflowed by 4 bytes\n")

    def test_will_not_fit_alone_keeps_only_size_lines(self):
        rc, out = run("x.c:1: warning: unused\n"
                      "ld: section .text will not fit in region `text'\n"
                      "ld: undefined reference to `foo'\n"
                      "collect2: error: ld returned 1 exit status\n")
        self.assertEqual(rc, 0, out)
        self.assertEqual(
            out,
            SIZE_HEADER + "\n"
            + "  ld: ld: section .text will not fit in region `text'\n"
            + "  ld: ld: undefined reference to `foo'\n")

    def test_vla_and_alloca_are_named_and_a_note_is_dropped(self):
        vla = "a.c:3:1: error: ISO C90 forbids variable length array 'v' [-Werror=vla]"
        alloca = "b.c:9: warning: alloca is used"
        rc, out = run(vla + "\nnote: here\n" + alloca + "\n")
        self.assertEqual(rc, 1, out)
        self.assertEqual(out.splitlines()[0],
                         "tfw-boot does not link for atmega328p:")
        self.assertIn("  ld: " + vla + "\n", out)
        self.assertIn("  ld: " + alloca + "\n", out)
        self.assertNotIn("note: here", out)

    def test_unmatched_last_line_without_newline_is_shown(self):
        rc, out = run("first\nlast-no-newline")
        self.assertEqual(rc, 1, out)
        self.assertIn("  ld: first\n", out)
        self.assertIn("  ld: last-no-newline\n", out)

    def test_missing_link_err_prints_the_header_only(self):
        with tempfile.TemporaryDirectory() as d:
            rc, out = run_path(os.path.join(d, "link.err"))
        self.assertEqual(rc, 1, out)
        self.assertEqual(out, ERR_HEADER + "\n")


if __name__ == "__main__":
    unittest.main()
