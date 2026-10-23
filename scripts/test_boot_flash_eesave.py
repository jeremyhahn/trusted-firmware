"""Tests for `make boot-flash`'s EESAVE guard and the first boot flash.

SPEC row device-provisioning, R7 and R8 (docs/device-provisioning/
engineering.md): EESAVE is programmed at the first boot flash, and the
factory record is written together with it. The recipe is ARCH_BOOT_FLASH
and ARCH_BOOT_FIRST_FLASH in mk/arch/avr.mk.

No board and no avr-gcc: each test runs the real make target with a stand-in
`avrdude` first on PATH, which holds a part's fuse bytes in a JSON file and
logs every call, and with the boot image marked as made (`make -o`). The
EESAVE fuse, its bit and the part come from each board's profile.mk, asked of
make; none is a literal here.
"""

import json
import os
import pathlib
import stat
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent

# The stand-in. A part is lfuse/hfuse/efuse/lock in $FAKE_AVRDUDE_STATE, plus
# "stuck": a fuse write that does not hold. Each call appends its argv to
# $FAKE_AVRDUDE_LOG.
FAKE_AVRDUDE = r'''#!/usr/bin/env python3
import json, os, sys
state_path = os.environ["FAKE_AVRDUDE_STATE"]
with open(state_path) as f:
    state = json.load(f)
with open(os.environ["FAKE_AVRDUDE_LOG"], "a") as f:
    f.write(json.dumps(sys.argv[1:]) + "\n")
args = sys.argv[1:]
ops = []
for i, a in enumerate(args):
    if a == "-U":
        ops.append(args[i + 1])
    elif a.startswith("-U"):
        ops.append(a[2:])
for op in ops:
    mem, rw, path, fmt = op.split(":", 3)
    if rw == "r" and path == "-":
        print(state[mem])
    elif rw == "r":
        size = {"eeprom": 64, "flash": 256}[mem]
        with open(path, "wb") as f:
            f.write(bytes([0x5A if mem == "eeprom" else 0xA5]) * size)
    elif rw == "w" and fmt == "m" and mem in ("lfuse", "hfuse", "efuse", "lock"):
        if not state.get("stuck"):
            state[mem] = path
with open(state_path, "w") as f:
    json.dump(state, f)
'''


def make_var(board, build_dir, expr):
    """What make expands `expr` to for this board, the way boot-flash sees it."""
    out = subprocess.run(
        ["make", "-s", "--no-print-directory", f"BOARD={board}",
         f"BUILD_DIR={build_dir}", "--eval", f"tfw-print: ; @echo {expr}",
         "tfw-print"],
        cwd=ROOT, capture_output=True, text=True, check=True)
    return out.stdout.strip().splitlines()[-1]


def eesave_boards():
    """Every board whose profile names an EESAVE fuse: each one boot-flash serves."""
    boards = []
    for d in sorted((ROOT / "boards").iterdir()):
        if not (d / "profile.mk").is_file():
            continue
        if make_var(d.name, "build", "$(ARCH):$(BOOT_EESAVE_FUSE)").split(":")[1:] != [""]:
            boards.append(d.name)
    return boards


class BootFlashEesave(unittest.TestCase):

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
        self.build = self.dir / "build"
        self.env = dict(os.environ,
                        PATH=f"{bindir}{os.pathsep}{os.environ['PATH']}",
                        FAKE_AVRDUDE_STATE=str(self.state),
                        FAKE_AVRDUDE_LOG=str(self.log))

    def tearDown(self):
        self.tmp.cleanup()

    def profile(self, board):
        fuse, bit, part = make_var(
            board, self.build,
            "$(BOOT_EESAVE_FUSE) $(BOOT_EESAVE_BIT) $(AVRDUDE_PART)").split()
        return fuse, int(bit), part

    def part(self, **fuses):
        """A part with every fuse byte at 0xff unless given."""
        state = {"lfuse": "0xff", "hfuse": "0xff", "efuse": "0xff", "lock": "0xff"}
        state.update(fuses)
        self.state.write_text(json.dumps(state))
        self.log.write_text("")

    def fuses(self):
        return json.loads(self.state.read_text())

    def calls(self):
        return [json.loads(l) for l in self.log.read_text().splitlines()]

    def boot_flash(self, board, *extra):
        hexfile = make_var(board, self.build, "$(BOOT_TARGET).hex")
        return subprocess.run(
            ["make", "--no-print-directory", "-o", hexfile, "boot-flash",
             f"BOARD={board}", f"BUILD_DIR={self.build}", *extra],
            cwd=ROOT, env=self.env, capture_output=True, text=True)

    @staticmethod
    def writes(calls):
        """The calls that write the part: a fuse, the EEPROM or the flash."""
        return [c for c in calls
                if any(":w:" in a for a in c)]

    def test_there_is_a_board_to_test(self):
        self.assertTrue(eesave_boards(), "no board names BOOT_EESAVE_FUSE")

    def test_unprogrammed_eesave_is_refused_and_nothing_written(self):
        for board in eesave_boards():
            with self.subTest(board=board):
                fuse, bit, _ = self.profile(board)
                self.part(**{fuse: "0xff"})
                r = self.boot_flash(board)
                self.assertNotEqual(r.returncode, 0, r.stdout)
                self.assertIn("REFUSED. EESAVE is not programmed", r.stderr)
                self.assertIn("BOOT_FIRST_FLASH=1", r.stderr)
                self.assertEqual(self.writes(self.calls()), [])
                self.assertEqual(self.fuses()[fuse], "0xff")
                self.assertFalse(list(self.build.glob("factory-*")))

    def test_first_flash_writes_the_record_then_eesave_then_flashes(self):
        for board in eesave_boards():
            with self.subTest(board=board):
                fuse, bit, part = self.profile(board)
                # Every byte distinct, so the record names which is which.
                before = {"lfuse": "0xf1", "hfuse": "0xf2", "efuse": "0xf3", "lock": "0xf4"}
                before[fuse] = "0xff"
                self.part(**before)
                r = self.boot_flash(board, "BOOT_FIRST_FLASH=1")
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

                want = 0xff & ~(1 << bit)
                self.assertEqual(int(self.fuses()[fuse], 16), want)
                for other in ("lfuse", "hfuse", "efuse", "lock"):
                    if other != fuse:
                        self.assertEqual(self.fuses()[other], before[other])

                records = list(self.build.glob(f"factory-{board}-*.mk"))
                self.assertEqual(len(records), 1, r.stdout)
                text = records[0].read_text()
                for name, value in before.items():
                    self.assertIn(f"FACTORY_{name.upper():<5} := {value}\n", text)
                self.assertIn(f"({part})", text)
                stem = str(records[0])[:-len(".mk")]
                self.assertTrue(pathlib.Path(stem + ".eeprom.bin").is_file())
                self.assertTrue(pathlib.Path(stem + ".flash.bin").is_file())

                # The order: every read before the one fuse write, the flash
                # after the fuse reads back programmed.
                calls = self.calls()
                writes = self.writes(calls)
                self.assertEqual(len(writes), 2, writes)
                self.assertIn(f"{fuse}:w:0x{want:02x}:m", writes[0])
                self.assertTrue(any(a.startswith("-Uflash:w:") for a in writes[1]))
                first_write = calls.index(writes[0])
                reads = [c for c in calls[:first_write] if any(":r:" in a for a in c)]
                read_mems = {a.split(":")[0].removeprefix("-U")
                             for c in reads for a in c if ":r:" in a}
                self.assertEqual(read_mems,
                                 {"lfuse", "hfuse", "efuse", "lock", "eeprom", "flash"})
                self.assertIn(f"{fuse}:r:-:h", calls[first_write + 1])
                for f in self.build.glob("factory-*"):
                    f.unlink()

    def test_first_flash_refuses_when_eesave_does_not_hold(self):
        for board in eesave_boards():
            with self.subTest(board=board):
                fuse, bit, _ = self.profile(board)
                self.part(**{fuse: "0xff"})
                state = self.fuses()
                state["stuck"] = True
                self.state.write_text(json.dumps(state))
                r = self.boot_flash(board, "BOOT_FIRST_FLASH=1")
                self.assertNotEqual(r.returncode, 0, r.stdout)
                self.assertIn("EESAVE did not hold", r.stderr)
                flashes = [c for c in self.calls()
                           if any(a.startswith("-Uflash:w:") for a in c)]
                self.assertEqual(flashes, [])
                # The record stays: it was written before the fuse.
                self.assertEqual(len(list(self.build.glob(f"factory-{board}-*.mk"))), 1)
                for f in self.build.glob("factory-*"):
                    f.unlink()

    def test_first_flash_on_a_programmed_part_writes_no_fuse_and_no_record(self):
        for board in eesave_boards():
            with self.subTest(board=board):
                fuse, bit, _ = self.profile(board)
                self.part(**{fuse: f"0x{0xff & ~(1 << bit):02x}"})
                r = self.boot_flash(board, "BOOT_FIRST_FLASH=1")
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                writes = self.writes(self.calls())
                self.assertEqual(len(writes), 1, writes)
                self.assertTrue(any(a.startswith("-Uflash:w:") for a in writes[0]))
                self.assertFalse(list(self.build.glob("factory-*")))

    def test_first_flash_and_eeprom_restore_together_are_refused(self):
        board = eesave_boards()[0]
        fuse, _, _ = self.profile(board)
        self.part(**{fuse: "0xff"})
        r = self.boot_flash(board, "BOOT_FIRST_FLASH=1", "BOOT_EEPROM_RESTORE=1")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("pick one", r.stderr)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
