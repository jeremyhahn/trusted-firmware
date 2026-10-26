# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The Cortex-M board writes of mk/arch/cortex-m.mk (make scripts-test).

`make -n` shows each silicon board's command lines, checked here by literal:
the tool, its target, the addresses, the record guard. The refusals and the
record-then-write-then-restore order are run for real against
scripts/fake_probe_tool.py on PATH, which talks to no device. No board, no
probe, no network.
"""

import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
FAKE = os.path.join(HERE, "fake_probe_tool.py")
TOOLS = ["STM32_Programmer_CLI", "picotool", "JLinkExe", "pyocd", "arm-none-eabi-objcopy"]
BOOT_ROW = "soft-soft-p256-soft-ota0-store0"

# Per board: its tool, the flash base, the slot, the flash size, and the
# literal command lines `make -n` must show for a record read, an image write
# at the slot and the read back of it, with CM_PROBE_SERIAL=SN1.
BOARDS = {
    "stm32h573i-dk": {
        "tool": "STM32_Programmer_CLI",
        "base": "0x08000000", "app": "0x08008000", "total": 2097152,
        "record": "STM32_Programmer_CLI -c port=SWD sn=SN1  -u 0x08000000 2097152 $rec.flash.bin",
        "extra": "STM32_Programmer_CLI -c port=SWD sn=SN1  -ob displ > $rec.optionbytes.txt",
        "write": "STM32_Programmer_CLI -c port=SWD sn=SN1  -d {bin} 0x08008000 -v",
        "readback": "STM32_Programmer_CLI -c port=SWD sn=SN1  -u 0x08008000 $n {bin}.readback",
    },
    "rp2350-pico2w": {
        "tool": "picotool",
        "base": "0x10000000", "app": "0x10008000", "total": 4194304,
        "record": "picotool save --ser SN1  -r 0x10000000 $(printf '0x%x' $(( 0x10000000 + 4194304 ))) $rec.flash.bin -t bin",
        "extra": "picotool otp dump --ser SN1  > $rec.otp.txt",
        "write": "picotool load --ser SN1  -v {bin} -t bin -o 0x10008000",
        "readback": "picotool save --ser SN1  -r 0x10008000 $(printf '0x%x' $(( 0x10008000 + $n ))) {bin}.readback -t bin",
    },
    "nrf54l15-dk": {
        "tool": "JLinkExe",
        "base": "0x00000000", "app": "0x00008000", "total": 1560576,
        "record": "printf 'savebin %s,%s,0x%x\\nexit\\n' $rec.flash.bin 0x00000000 1560576 > $rec.flash.bin.jlink && "
                  "JLinkExe -USB SN1 -device NRF54L15_M33 -if SWD -speed 4000 -autoconnect 1 -ExitOnError 1 -NoGui 1  "
                  "-CommandFile $rec.flash.bin.jlink",
        "extra": "printf 'savebin %s,%s,0x%x\\nexit\\n' $rec.uicr.bin 0x00FFD000 4096 > $rec.uicr.bin.jlink",
        "write": "printf 'loadbin %s,%s\\nverifybin %s,%s\\nexit\\n' {bin} 0x00008000 {bin} 0x00008000 > {bin}.jlink",
        "readback": "printf 'savebin %s,%s,0x%x\\nexit\\n' {bin}.readback 0x00008000 $n > {bin}.readback.jlink",
    },
    "frdm-mcxn947": {
        "tool": "pyocd",
        "base": "0x10000000", "app": "0x10008000", "total": 2097152,
        "record": "pyocd cmd -t mcxn947vdf -u SN1  -c \"savemem 0x10000000 2097152 $rec.flash.bin\"",
        "extra": "echo 'no protected-flash read: its place in the MCXN947 map is not cited yet' > $rec.extra.txt",
        "write": "pyocd load -t mcxn947vdf -u SN1  --format bin --base-address 0x10008000 {bin}",
        "readback": "pyocd cmd -t mcxn947vdf -u SN1  -c \"savemem 0x10008000 $n {bin}.readback\"",
    },
}


def hex_of(board, boot=False):
    if boot:
        return f"build/{board}/boot/{BOOT_ROW}/tfw-boot.hex"
    return f"build/{board}/soft/trusted-firmware.hex"


class Bench(unittest.TestCase):
    """A temporary directory with the fake tools on PATH and a record dir."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="cmflash-")
        self.bin = os.path.join(self.tmp, "bin")
        os.mkdir(self.bin)
        for tool in TOOLS:
            os.symlink(FAKE, os.path.join(self.bin, tool))
        self.records = os.path.join(self.tmp, "records")
        self.log = os.path.join(self.tmp, "calls.log")
        self.env = dict(os.environ, PATH=self.bin + os.pathsep + os.environ["PATH"],
                        FAKE_LOG=self.log, FAKE_MEM=os.path.join(self.tmp, "mem.json"))

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def make(self, *args, dry=False, path=None):
        env = dict(self.env)
        if path is not None:
            env["PATH"] = path
        cmd = ["make", "--no-print-directory", "-s"] + (["-n"] if dry else []) + list(args)
        return subprocess.run(cmd, cwd=REPO, env=env, capture_output=True, text=True, check=False)

    def calls(self):
        if not os.path.exists(self.log):
            return []
        with open(self.log, encoding="utf-8") as f:
            return f.read().splitlines()

    def image(self, board):
        """A stand-in .hex under the temporary directory; FW_DIR points there."""
        fw = os.path.join(self.tmp, "fw-" + board)
        os.makedirs(fw, exist_ok=True)
        hexfile = os.path.join(fw, "trusted-firmware.hex")
        with open(hexfile, "wb") as f:
            f.write(bytes(range(256)) * 4)
        return fw, hexfile

    def flash(self, board, *extra):
        fw, hexfile = self.image(board)
        return self.make("flash", f"BOARD={board}", f"FW_DIR={fw}", "-o", hexfile,
                         f"CM_RECORD_DIR={self.records}", *extra)


class TestDryRun(Bench):
    def test_flash_lines(self):
        for board, b in BOARDS.items():
            with self.subTest(board=board):
                r = self.make("flash", f"BOARD={board}", "CM_PROBE_SERIAL=SN1", "-o", hex_of(board), dry=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                out = r.stdout
                binfile = hex_of(board)[:-4] + ".bin"
                self.assertIn(f"command -v {b['tool']} >/dev/null", out)
                self.assertIn(f"for f in build/records/{board}-SN1-*.flash.bin; do", out)
                self.assertIn(f"[ \"$(stat -c %s \"$f\")\" -eq {b['total']} ] && sha256sum -c --status \"$f.sha256\"", out)
                self.assertIn("make board-record BOARD=" + board + " CM_PROBE_SERIAL=SN1", out)
                self.assertIn(f"arm-none-eabi-objcopy -I ihex -O binary {hex_of(board)} {binfile}", out)
                self.assertIn(b["write"].format(bin=binfile), out)
                self.assertIn(b["readback"].format(bin=binfile), out)
                self.assertIn(f"cmp {binfile} {binfile}.readback", out)
                # The guard comes before the write.
                self.assertLess(out.index("REFUSED. No whole factory record"), out.index(b["write"].format(bin=binfile)))

    def test_boot_flash_lines(self):
        for board, b in BOARDS.items():
            with self.subTest(board=board):
                r = self.make("boot-flash", f"BOARD={board}", "CM_PROBE_SERIAL=SN1", "-o", hex_of(board, True), dry=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                binfile = hex_of(board, True)[:-4] + ".bin"
                write = b["write"].format(bin=binfile).replace(b["app"], b["base"])
                self.assertIn(write, r.stdout)
                self.assertIn(f"for f in build/records/{board}-SN1-*.flash.bin; do", r.stdout)

    def test_record_lines(self):
        for board, b in BOARDS.items():
            with self.subTest(board=board):
                r = self.make("board-record", f"BOARD={board}", "CM_PROBE_SERIAL=SN1", dry=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                out = r.stdout
                self.assertIn(f"rec=build/records/{board}-SN1-$(date -u +%Y%m%dT%H%M%SZ);", out)
                self.assertIn(b["record"], out)
                self.assertIn(b["extra"], out)
                self.assertIn(f"if [ \"$n\" -ne {b['total']} ]; then", out)
                self.assertIn("sha256sum $rec.flash.bin > $rec.flash.bin.sha256", out)

    def test_restore_lines(self):
        for board, b in BOARDS.items():
            with self.subTest(board=board):
                img = f"{self.tmp}/{board}-SN1-20261021T000000Z.flash.bin"
                open(img, "wb").close()
                r = self.make("board-restore", f"BOARD={board}", "CM_PROBE_SERIAL=SN1",
                              f"BOARD_RESTORE_IMAGE={img}", dry=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn(f"case \"$(basename {img})\" in {board}-SN1-*.flash.bin) ;;", r.stdout)
                write = b["write"].format(bin=img).replace(b["app"], b["base"])
                self.assertIn(write, r.stdout)

    def test_restore_needs_a_record(self):
        r = self.make("board-restore", "BOARD=stm32h573i-dk", "CM_PROBE_SERIAL=SN1", dry=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("board-restore: set BOARD_RESTORE_IMAGE", r.stderr)

    def test_no_refused_operation_in_any_line(self):
        for board in BOARDS:
            for goal, extra in (("flash", ["-o", hex_of(board)]), ("board-record", [])):
                r = self.make(goal, f"BOARD={board}", "CM_PROBE_SERIAL=SN1", *extra, dry=True)
                for line in r.stdout.split(";"):
                    if "echo" in line or "case" in line or "*erase*" in line:
                        continue
                    for word in ("erase", "-e all", "unlock", "recover", "RDP=", "otp set", "otp load", "fuse", "-lock"):
                        self.assertNotIn(word, line, f"{board} {goal}: {line}")


class TestQemuBoard(Bench):
    def test_flash_and_record_keep_their_refusal(self):
        r = self.make("flash", "BOARD=mps2-an505", "-o", hex_of("mps2-an505"))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("flash: mps2-an505 is an emulated board: QEMU loads the image (make integration-test-cortex-m)", r.stderr)
        r = self.make("board-record", "BOARD=mps2-an505")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("board-record: mps2-an505 is an emulated board: it has no factory state", r.stderr)
        r = self.make("board-restore", "BOARD=mps2-an505")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("board-restore: mps2-an505 is an emulated board: it has no factory state", r.stderr)


class TestRefusals(Bench):
    def test_write_without_record_is_refused(self):
        for board in BOARDS:
            with self.subTest(board=board):
                r = self.flash(board, "CM_PROBE_SERIAL=SN1")
                self.assertNotEqual(r.returncode, 0)
                self.assertIn(f"flash: REFUSED. No whole factory record of {board} with probe SN1", r.stderr)
                self.assertEqual(self.calls(), [])

    def test_write_without_serial_is_refused(self):
        r = self.flash("stm32h573i-dk")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("flash: REFUSED. Name the probe: make flash BOARD=stm32h573i-dk CM_PROBE_SERIAL=<serial>", r.stderr)
        self.assertEqual(self.calls(), [])

    def test_refused_probe_args(self):
        for args in ("-ob RDP=0xBB", "-e all", "--erase=chip", "-lock", "unlock", "otp set"):
            with self.subTest(args=args):
                if os.path.exists(self.log):
                    os.remove(self.log)
                r = self.make("board-record", "BOARD=stm32h573i-dk", "CM_PROBE_SERIAL=SN1",
                              f"CM_RECORD_DIR={self.records}", f"CM_PROBE_ARGS={args}")
                self.assertNotEqual(r.returncode, 0)
                self.assertIn("names an erase, option-byte, protection, lock, OTP or fuse operation", r.stderr)
                self.assertEqual(self.calls(), [])

    def test_missing_tool_names_it(self):
        os.remove(os.path.join(self.bin, "picotool"))
        path = self.bin + os.pathsep + "/usr/bin" + os.pathsep + "/bin"
        r = self.make("board-record", "BOARD=rp2350-pico2w", "CM_PROBE_SERIAL=SN1",
                      f"CM_RECORD_DIR={self.records}", path=path)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("board-record: picotool is not on PATH: rp2350-pico2w is written with it. "
                      "To get it: build and install picotool 2.0.0 or later from https://github.com/raspberrypi/picotool",
                      r.stderr)

    def test_short_read_is_not_a_record(self):
        self.env["FAKE_SHORT"] = "1"
        r = self.make("board-record", "BOARD=stm32h573i-dk", "CM_PROBE_SERIAL=SN1", f"CM_RECORD_DIR={self.records}")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("bytes, not the part's 2097152", r.stderr)
        self.assertTrue(all(n.endswith((".short", ".optionbytes.txt")) for n in os.listdir(self.records)))
        del self.env["FAKE_SHORT"]
        r = self.flash("stm32h573i-dk", "CM_PROBE_SERIAL=SN1")
        self.assertIn("REFUSED. No whole factory record", r.stderr)


class TestRecordWriteRestore(Bench):
    def cycle(self, board):
        b = BOARDS[board]
        r = self.make("board-record", f"BOARD={board}", "CM_PROBE_SERIAL=SN1", f"CM_RECORD_DIR={self.records}")
        self.assertEqual(r.returncode, 0, r.stderr)
        recs = [n for n in os.listdir(self.records) if n.endswith(".flash.bin")]
        self.assertEqual(len(recs), 1)
        rec = os.path.join(self.records, recs[0])
        self.assertTrue(recs[0].startswith(f"{board}-SN1-"))
        self.assertEqual(os.path.getsize(rec), b["total"])
        self.assertTrue(os.path.exists(rec + ".sha256"))

        # Another probe's serial has no record: refused.
        r = self.flash(board, "CM_PROBE_SERIAL=SN2")
        self.assertIn("REFUSED. No whole factory record", r.stderr)

        r = self.flash(board, "CM_PROBE_SERIAL=SN1")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("read back equal", r.stdout)

        # A record that no longer matches its SHA-256 is not one.
        with open(rec, "r+b") as f:
            f.write(b"\x00")
        r = self.flash(board, "CM_PROBE_SERIAL=SN1")
        self.assertIn("REFUSED. No whole factory record", r.stderr)
        r = self.make("board-restore", f"BOARD={board}", "CM_PROBE_SERIAL=SN1", f"BOARD_RESTORE_IMAGE={rec}")
        self.assertIn(f"is not {b['total']} bytes matching", r.stderr)
        with open(rec, "r+b") as f:
            f.write(b"\xff")

        # Another board's record name: refused.
        other = os.path.join(self.tmp, "other-SN1-20261021T000000Z.flash.bin")
        shutil.copy(rec, other)
        r = self.make("board-restore", f"BOARD={board}", "CM_PROBE_SERIAL=SN1", f"BOARD_RESTORE_IMAGE={other}")
        self.assertIn(f"is not a record of {board} with probe SN1", r.stderr)

        r = self.make("board-restore", f"BOARD={board}", "CM_PROBE_SERIAL=SN1", f"BOARD_RESTORE_IMAGE={rec}")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("back over the whole flash, read back equal", r.stdout)
        return self.calls()

    def test_stm32h573i_dk(self):
        calls = self.cycle("stm32h573i-dk")
        self.assertTrue(any(c.endswith("-ob displ") for c in calls))

    def test_rp2350_pico2w(self):
        calls = self.cycle("rp2350-pico2w")
        self.assertIn("picotool otp dump --ser SN1", calls)

    def test_nrf54l15_dk(self):
        self.cycle("nrf54l15-dk")
        uicr = [n for n in os.listdir(self.records) if n.endswith(".uicr.bin")]
        self.assertEqual(len(uicr), 1)
        self.assertEqual(os.path.getsize(os.path.join(self.records, uicr[0])), 4096)

    def test_frdm_mcxn947(self):
        calls = self.cycle("frdm-mcxn947")
        self.assertTrue(all(c.split()[1] in ("cmd", "load") for c in calls if c.startswith("pyocd")))


if __name__ == "__main__":
    unittest.main()
