# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for test/integration/cortex-m/qemu_wire_test.sh's arguments (make scripts-test).

mk/arch/cortex-m-targets.mk's cortex-m-qemu-wire-run passes the row as the
fourth argument: none for the base row, or store, ab, spi or i2c. Each run here
names `false` as QEMU, so a row the script accepts gets as far as its first
boot and fails there, naming that boot; a row it refuses ends at the usage
line with exit status 2. No QEMU.
"""

import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.path.join(REPO, "test", "integration", "cortex-m", "qemu_wire_test.sh")
USAGE = "usage: qemu_wire_test.sh <dir> <machine> <board> [store|ab|spi|i2c|pwm]\n"

# The row the recipe passes, and the first boot the script starts for it.
ROWS = (
    ((), "provisioned"),
    (("store",), "staging"),
    (("ab",), "ab-Info"),
    (("spi",), "spi"),
    (("i2c",), "i2c"),
)


class TestRows(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        with open(os.path.join(self.dir, "addrs.env"), "w") as f:
            f.write("BOOT_AT=0x10000000\nAPP_AT=0x10010000\n"
                    "PLATFORM_AT=0x10080000\nDEVICEID=00\n")

    def tearDown(self):
        for name in os.listdir(self.dir):
            os.remove(os.path.join(self.dir, name))
        os.rmdir(self.dir)

    def run_script(self, *args):
        env = dict(os.environ, QEMU="false", MAKE="false")
        return subprocess.run([SCRIPT, *args], env=env, capture_output=True,
                              text=True, timeout=30)

    def test_every_row_the_recipe_passes_reaches_its_first_boot(self):
        for row, first in ROWS:
            with self.subTest(row=row):
                got = self.run_script(self.dir, "mps2-an505", "mps2-an505", *row)
                self.assertEqual(got.returncode, 1, got.stderr)
                self.assertNotIn("usage:", got.stderr)
                self.assertIn(f"qemu_wire_test: mps2-an505 {first}: false -M mps2-an505 "
                              "did not start", got.stderr)

    def test_a_row_it_does_not_know_is_refused_with_the_usage_line(self):
        for args in (("bogus",), ("base",), ("spi", "i2c"), ()):
            with self.subTest(args=args):
                lead = (self.dir, "mps2-an505") + (("mps2-an505",) if args else ())
                got = self.run_script(*lead, *args)
                self.assertEqual(got.returncode, 2, got.stderr)
                self.assertEqual(got.stderr, USAGE)


if __name__ == "__main__":
    unittest.main()
