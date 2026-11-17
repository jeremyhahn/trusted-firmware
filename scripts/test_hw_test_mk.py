"""hw-test's default board reset names the probe it resets.

The STM32H573I-DK's rows reset it through its ST-LINK (HW_RESET_stm32h573i-dk
in mk/go.mk). PORT names only the VCOM, so the reset command must name the
probe, CM_PROBE_SERIAL, or with two ST-LINKs attached it could reset the other
board. Checked through `make -n hw-test` and through the refusals, which run
before the port is opened, so no board is needed.
"""

import os
import subprocess
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Not a port on any host: every run here stops before a row would open it.
PORT = "absent-port"


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        check=False)
    return p.returncode, p.stdout


class HwTestResetNamesTheProbe(unittest.TestCase):

    def test_dry_run_resets_the_named_probe(self):
        rc, out = make("-n", "hw-test", "BOARD=stm32h573i-dk", f"PORT={PORT}",
                       "CM_PROBE_SERIAL=PROBE-A1")
        self.assertEqual(rc, 0, out)
        self.assertIn("TFW_HW_RESET='cmd:STM32_Programmer_CLI -c port=SWD "
                      "sn=PROBE-A1 mode=HOTPLUG -rst'", out)

    def test_default_reset_without_a_probe_is_refused(self):
        rc, out = make("hw-test", "BOARD=stm32h573i-dk", f"PORT={PORT}")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("REFUSED", out)
        self.assertIn("CM_PROBE_SERIAL=<serial>", out)

    def test_probe_serial_that_is_not_a_word_is_refused(self):
        rc, out = make("hw-test", "BOARD=stm32h573i-dk", f"PORT={PORT}",
                       "CM_PROBE_SERIAL=a;b")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("is not letters, digits, - and _", out)

    def test_named_probe_passes_to_the_port_check(self):
        rc, out = make("hw-test", "BOARD=stm32h573i-dk", f"PORT={PORT}",
                       "CM_PROBE_SERIAL=PROBE-A1")
        self.assertNotEqual(rc, 0, out)
        self.assertNotIn("REFUSED", out)
        self.assertIn(f"{PORT} does not exist", out)

    def test_given_reset_needs_no_probe(self):
        rc, out = make("hw-test", "BOARD=stm32h573i-dk", f"PORT={PORT}",
                       "HW_RESET=cmd:true")
        self.assertNotEqual(rc, 0, out)
        self.assertNotIn("REFUSED", out)
        self.assertIn(f"{PORT} does not exist", out)


if __name__ == "__main__":
    unittest.main()
