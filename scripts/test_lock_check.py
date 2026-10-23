"""Tests for `make lock-check`: the lock bits over the soft root's UDS.

SPEC row device-provisioning, R6 (docs/device-provisioning/engineering.md):
the operator writes the lock bits, and the provisioning flow reads them back
and refuses to call the part provisioned unless they lock the UDS. The recipe
is ARCH_LOCK_CHECK in mk/arch/avr.mk; the bits are the board's
BOOT_LOCK_PROGRAMMED.

The lock bytes here are literals on purpose, against each board's profile:
a value computed from that profile would move with it. No board and no
avr-gcc: the real make target runs against slice 3's stand-in `avrdude`.
"""

import json
import os
import pathlib
import stat
import subprocess
import tempfile
import unittest

from test_boot_flash_eesave import FAKE_AVRDUDE, ROOT

BOARD = "atmega2560"


class StandIn(unittest.TestCase):
    """`make lock-check` against the stand-in avrdude; no tests of its own."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        bindir = self.dir / "bin"
        bindir.mkdir()
        fake = bindir / "avrdude"
        fake.write_text(FAKE_AVRDUDE)
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        self.state = self.dir / "part.json"
        self.log = self.dir / "avrdude.log"
        self.env = dict(os.environ,
                        PATH=f"{bindir}{os.pathsep}{os.environ['PATH']}",
                        FAKE_AVRDUDE_STATE=str(self.state),
                        FAKE_AVRDUDE_LOG=str(self.log))

    def tearDown(self):
        self.tmp.cleanup()

    def lock_check(self, lock, board=BOARD):
        self.state.write_text(json.dumps(
            {"lfuse": "0xff", "hfuse": "0xff", "efuse": "0xff", "lock": lock}))
        self.log.write_text("")
        return subprocess.run(
            ["make", "--no-print-directory", "lock-check", f"BOARD={board}",
             f"BUILD_DIR={self.dir / 'build'}"],
            cwd=ROOT, env=self.env, capture_output=True, text=True)

    def calls(self):
        return [json.loads(l) for l in self.log.read_text().splitlines()]

    def assert_read_only(self):
        for call in self.calls():
            self.assertFalse(any(":w:" in a for a in call), call)


class LockCheck(StandIn):

    def test_the_operators_value_passes(self):
        # docs/device-provisioning/user.md step 7 writes 0x0C.
        r = self.lock_check("0x0c")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("the UDS is locked", r.stdout)
        self.assertIn("0x33", r.stdout)
        self.assert_read_only()

    def test_every_bit_programmed_passes(self):
        r = self.lock_check("0x00")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_an_unlocked_part_is_refused(self):
        r = self.lock_check("0xff")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("REFUSED", r.stderr)
        for named in ("LB1 (bit 0)", "LB2 (bit 1)", "BLB11 (bit 4)", "BLB12 (bit 5)"):
            self.assertIn(named, r.stderr)
        self.assert_read_only()

    def test_the_factory_lock_byte_is_refused(self):
        # The bench Mega's factory 0xCF: BLB1 mode 3, LB unprogrammed.
        r = self.lock_check("0xcf")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("read 1: LB1 (bit 0) LB2 (bit 1).", r.stderr)
        self.assertNotIn("BLB1", r.stderr.split("read 1:")[1])

    def test_one_bit_short_is_refused_and_named(self):
        # 0x0C with BLB11 (bit 4) left unprogrammed.
        r = self.lock_check("0x1c")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("read 1: BLB11 (bit 4).", r.stderr)
        for other in ("LB1 ", "LB2 ", "BLB12"):
            self.assertNotIn(other, r.stderr.split("read 1:")[1])

    def test_an_unreadable_lock_byte_is_refused(self):
        r = self.lock_check("error")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("could not read the lock byte", r.stderr)

    def test_a_board_with_no_expected_value_is_refused(self):
        # The ATtiny85's profile names no BOOT_LOCK_PROGRAMMED: refused at
        # parse time, before avrdude is ever run.
        r = self.lock_check("0x00", board="attiny85")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("names no BOOT_LOCK_PROGRAMMED", r.stderr)
        self.assertEqual(self.calls(), [])


class LockCheckSmallParts(StandIn):
    """The ATmega328P and ATmega32U4: the same lock byte as the Mega.

    Each data sheet's Lock Bit Byte table (328P DS40002061B §28.1 Table 28-1,
    32U4 Atmel-7766J §28.1 Table 28-1) puts BLB12:BLB11 at bits 5:4, BLB02:BLB01
    at 3:2 and LB2:LB1 at 1:0, so the protecting value is 0x0C and the mask
    0x33. Literals, per board, as above.
    """

    def test_the_protecting_value_passes(self):
        for board in ("atmega328p", "atmega32u4"):
            with self.subTest(board=board):
                r = self.lock_check("0x0c", board=board)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertIn("the UDS is locked", r.stdout)
                self.assertIn("0x33", r.stdout)

    def test_an_unlocked_part_is_refused(self):
        for board in ("atmega328p", "atmega32u4"):
            with self.subTest(board=board):
                r = self.lock_check("0xff", board=board)
                self.assertNotEqual(r.returncode, 0, r.stdout)
                self.assertIn("REFUSED", r.stderr)
                for named in ("LB1 (bit 0)", "LB2 (bit 1)", "BLB11 (bit 4)",
                              "BLB12 (bit 5)"):
                    self.assertIn(named, r.stderr)

    def test_one_bit_short_is_refused_and_named(self):
        # 0x0C with BLB12 (bit 5) left unprogrammed on the 328P, LB1 (bit 0)
        # on the 32U4.
        for board, lock, short, others in (
                ("atmega328p", "0x2c", "BLB12 (bit 5)", ("LB1 ", "LB2 ", "BLB11")),
                ("atmega32u4", "0x0d", "LB1 (bit 0)", ("LB2 ", "BLB11", "BLB12"))):
            with self.subTest(board=board):
                r = self.lock_check(lock, board=board)
                self.assertNotEqual(r.returncode, 0, r.stdout)
                self.assertIn(f"read 1: {short}.", r.stderr)
                for other in others:
                    self.assertNotIn(other, r.stderr.split("read 1:")[1])


if __name__ == "__main__":
    unittest.main()
