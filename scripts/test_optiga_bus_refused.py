# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for CRYPTO=optiga's refusal with the I2C bus off (make scripts-test; #510).

The OPTIGA Trust M answers on I2C and nowhere else, so mk/firmware.mk stops a
CRYPTO=optiga build on a board whose I2C is off. mps2-an505 has it off by
default and turns it on per row with I2C=1. These tests read what make
decides, without building. The board and file names are literals, as the
caller types them.
"""

import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


class OptigaNeedsTheI2CBus(unittest.TestCase):
    def test_optiga_with_the_bus_off_is_refused(self):
        rc, out = make("firmware-srcs", "BOARD=mps2-an505", "CRYPTO=optiga")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("CRYPTO=optiga needs the I2C bus -- mps2-an505 has it off", out)
        self.assertIn("takes I2C=1", out)
        self.assertTrue(out.startswith("mk/firmware.mk:"), out)

    def test_optiga_with_i2c_on_links_the_transport(self):
        rc, out = make("firmware-srcs", "BOARD=mps2-an505", "CRYPTO=optiga", "I2C=1")
        self.assertEqual(rc, 0, out)
        self.assertIn("src/crypto/optiga/optiga_i2c.c", out.split())


if __name__ == "__main__":
    unittest.main()
