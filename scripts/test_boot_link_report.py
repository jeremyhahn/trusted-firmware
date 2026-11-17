"""scripts/boot_link_report.sh names a failed boot link's real cause (#530).

On #497 an LTO-link -Werror=frame-larger-than error was reported as "it does
not fit in the boot section", and a lane sized RAM and flash that were not
the cause. These pin both classes with canned link.err files.
"""

import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "boot_link_report.sh")
NOT_FIT = "it does not fit in the boot section."

OVERFLOW_ERR = """\
/usr/lib/gcc/avr/7.3.0/../../../avr/bin/ld: build/boot/atmega328p/tfw-boot.elf section `.text' will not fit in region `text'
/usr/lib/gcc/avr/7.3.0/../../../avr/bin/ld: region `text' overflowed by 212 bytes
collect2: error: ld returned 1 exit status
"""

FRAME_ERR = """\
boot/boot_main.c: In function 'boot_main':
boot/boot_main.c:118:6: error: the frame size of 1312 bytes is larger than 1024 bytes [-Werror=frame-larger-than=]
lto1: some warnings being treated as errors
lto-wrapper: fatal error: avr-gcc returned 1 exit status
compilation terminated.
/usr/lib/gcc/avr/7.3.0/../../../avr/bin/ld: error: lto-wrapper failed
collect2: error: ld returned 1 exit status
"""


def run(text, board="atmega328p"):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "link.err")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        p = subprocess.run(["sh", SCRIPT, path, board],
                           capture_output=True, text=True, check=False)
    return p.returncode, p.stdout


class BootLinkReportTest(unittest.TestCase):
    def test_overflow_is_a_size_failure(self):
        rc, out = run(OVERFLOW_ERR)
        self.assertEqual(rc, 0, out)
        self.assertIn("tfw-boot does not link for atmega328p: " + NOT_FIT, out)
        self.assertIn("  ld: ", out)
        self.assertIn("region `text' overflowed by 212 bytes", out)
        self.assertIn("will not fit in region `text'", out)

    def test_frame_larger_than_names_the_error(self):
        rc, out = run(FRAME_ERR)
        self.assertEqual(rc, 1, out)
        self.assertNotIn(NOT_FIT, out)
        self.assertNotIn("fit", out)
        first = out.splitlines()[0]
        self.assertEqual(first, "tfw-boot does not link for atmega328p:")
        self.assertIn("[-Werror=frame-larger-than=]", out)
        self.assertIn("lto-wrapper failed", out)
        self.assertNotIn("compilation terminated.", out)

    def test_unmatched_log_is_shown_whole(self):
        rc, out = run("something odd happened\n")
        self.assertEqual(rc, 1, out)
        self.assertNotIn(NOT_FIT, out)
        self.assertIn("  ld: something odd happened", out)

    def test_usage(self):
        p = subprocess.run(["sh", SCRIPT], capture_output=True, text=True,
                           check=False)
        self.assertEqual(p.returncode, 2)


if __name__ == "__main__":
    unittest.main()
