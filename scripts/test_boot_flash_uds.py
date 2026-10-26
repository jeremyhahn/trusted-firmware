"""Tests for `make boot-flash-uds`: the soft tier's UDS, placed at flash time (#7).

DEVELOPMENT ONLY. The recipe is in mk/boot-bench.mk. It runs here end to end
through make, with a stand-in `avrdude` first on PATH that touches no
programmer: it answers the EESAVE read from a JSON file, copies the image it is
told to write, and fails the write when asked. TMPDIR is a directory of the
test's own, so whatever the recipe leaves there is seen. The key is a
throwaway in that directory, the bench image is built once for the class, and
the test removes all of it. Without avr-gcc it fails; it never skips.
"""

import hashlib
import json
import os
import pathlib
import stat
import subprocess
import tempfile
import unittest

from test_bench_key import (BOARD, ROW, ihex, make, make_var, require_avr_gcc,
                            sha256, symbol, throwaway_key)

# The stand-in. $FAKE_AVRDUDE_STATE holds the fuses; a flash write copies its
# file to $FAKE_AVRDUDE_FLASHED/<n>.hex, unless $FAKE_AVRDUDE_FAIL is set, and
# logs whether the file was there to $FAKE_AVRDUDE_LOG.
FAKE_AVRDUDE = r'''#!/usr/bin/env python3
import json, os, shutil, sys
args = sys.argv[1:]
with open(os.environ["FAKE_AVRDUDE_STATE"]) as f:
    state = json.load(f)
ops = [args[i + 1] if a == "-U" else a[2:] for i, a in enumerate(args)
       if a == "-U" or a.startswith("-U")]
for op in ops:
    mem, rw, path, fmt = op.split(":", 3)
    if rw == "r" and path == "-":
        print(state[mem])
    elif rw == "w" and mem == "flash":
        with open(os.environ["FAKE_AVRDUDE_LOG"], "a") as f:
            f.write(json.dumps({"path": path, "there": os.path.isfile(path)}) + "\n")
        if os.environ.get("FAKE_AVRDUDE_FAIL"):
            print("avrdude: stand-in: flash write failed", file=sys.stderr)
            sys.exit(1)
        out = os.environ["FAKE_AVRDUDE_FLASHED"]
        shutil.copy(path, os.path.join(out, f"{len(os.listdir(out))}.hex"))
    else:
        sys.exit(f"stand-in avrdude: no {op}")
'''

# The ATmega2560's high fuse with EESAVE (bit 3) programmed, and without.
HFUSE_EESAVE = "0xd0"
HFUSE_NO_EESAVE = "0xd8"


class BootFlashUds(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        require_avr_gcc()
        cls.tmp = tempfile.TemporaryDirectory(prefix="tfw-uds-test-")
        cls.dir = pathlib.Path(cls.tmp.name)
        cls.key = throwaway_key(cls.dir / "keys" / "k.pem")
        cls.vars = [f"BOARD={BOARD}", f"BUILD_DIR={cls.dir / 'build'}", *ROW,
                    f"BENCH_KEY={cls.key}"]
        r = make([*cls.vars, "bootloader", "boot-bench"], dict(os.environ, HOME=str(cls.dir)))
        if r.returncode != 0:
            cls.tmp.cleanup()
            raise AssertionError(r.stdout[-2000:] + r.stderr[-2000:])
        cls.target = pathlib.Path(make_var("$(BOOT_TARGET)", cls.vars))
        cls.bench = cls.target.parent / "tfw-boot-bench.hex"
        cls.record = pathlib.Path(make_var("$(BOOT_UDS_RECORD)", cls.vars))
        cls.slot = symbol(make_var("$(ARCH_NM)", cls.vars),
                          cls.target.with_suffix(".elf"), "uds_slot")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.run_dir = pathlib.Path(tempfile.mkdtemp(dir=self.dir, prefix="run-"))
        bindir = self.run_dir / "bin"
        bindir.mkdir()
        fake = bindir / "avrdude"
        fake.write_text(FAKE_AVRDUDE)
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        self.tmpdir = self.run_dir / "tmp"
        self.tmpdir.mkdir()
        self.flashed = self.run_dir / "flashed"
        self.flashed.mkdir()
        self.state = self.run_dir / "part.json"
        self.state.write_text(json.dumps({"hfuse": HFUSE_EESAVE}))
        self.log = self.run_dir / "avrdude.log"
        self.log.write_text("")
        self.env = dict(os.environ, HOME=str(self.run_dir), TMPDIR=str(self.tmpdir),
                        PATH=f"{bindir}{os.pathsep}{os.environ['PATH']}",
                        FAKE_AVRDUDE_STATE=str(self.state), FAKE_AVRDUDE_LOG=str(self.log),
                        FAKE_AVRDUDE_FLASHED=str(self.flashed))
        self.record.unlink(missing_ok=True)

    def flash(self, *extra, env=None):
        return make([*self.vars, *extra, "boot-flash-uds"], env or self.env)

    def writes(self):
        return [json.loads(l) for l in self.log.read_text().splitlines()]

    def flashed_images(self):
        return sorted(self.flashed.iterdir(), key=lambda p: int(p.stem))

    def uds_of(self, image):
        at, size = self.slot
        mem = ihex(image)
        return bytes(mem[a] for a in range(at, at + size))

    def test_the_flashed_image_differs_from_the_bench_image_only_in_the_uds(self):
        r = self.flash()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        (image,) = self.flashed_images()
        at, size = self.slot
        self.assertEqual(size, 32)
        bench, flashed = ihex(self.bench), ihex(image)
        self.assertEqual(bench.keys(), flashed.keys())
        differ = {a for a in bench if bench[a] != flashed[a]}
        self.assertTrue(differ)
        self.assertLessEqual(differ, set(range(at, at + 32)))
        self.assertEqual(self.uds_of(self.bench), b"\xff" * 32)

    def test_two_runs_flash_two_uds(self):
        for _ in range(2):
            r = self.flash()
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        first, second = (self.uds_of(p) for p in self.flashed_images())
        self.assertNotEqual(first, second)
        self.assertNotIn(b"\xff" * 32, (first, second))

    def test_the_temporary_image_is_gone_after_a_flash(self):
        r = self.flash()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        (write,) = self.writes()
        self.assertTrue(write["there"])
        self.assertTrue(write["path"].startswith(f"{self.tmpdir}/tfw-uds."), write)
        self.assertEqual(list(self.tmpdir.iterdir()), [])

    def test_the_temporary_image_is_gone_after_a_failed_flash(self):
        r = self.flash(env=dict(self.env, FAKE_AVRDUDE_FAIL="1"))
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("FAILED", r.stderr)
        (write,) = self.writes()
        self.assertTrue(write["there"])
        self.assertEqual(list(self.tmpdir.iterdir()), [])
        self.assertFalse(self.record.exists())

    def test_a_refused_erase_flashes_nothing_and_leaves_nothing(self):
        # boot-flash's own EESAVE check refuses, inside boot-flash-uds.
        self.state.write_text(json.dumps({"hfuse": HFUSE_NO_EESAVE}))
        r = self.flash()
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("REFUSED", r.stderr)
        self.assertEqual(self.writes(), [])
        self.assertEqual(list(self.tmpdir.iterdir()), [])
        self.assertFalse(self.record.exists())

    def test_only_the_image_hash_is_printed_and_kept(self):
        r = self.flash()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        (image,) = self.flashed_images()
        digest = sha256(image)
        self.assertIn(f"image SHA-256 {digest}", r.stdout)
        record = self.record.read_text()
        self.assertEqual(len(record.splitlines()), 1)
        self.assertIn(digest, record)
        self.assertIn("UDS not kept", record)
        uds = self.uds_of(image)
        for text in (r.stdout, r.stderr, record):
            self.assertNotIn(uds.hex(), text.lower())
            self.assertNotIn(hashlib.sha256(uds).hexdigest(), text)
        outputs = sorted(p.name for p in self.target.parent.iterdir() if p.is_file())
        self.assertEqual(outputs, ["link.err", "tfw-boot-bench.elf", "tfw-boot-bench.hex",
                                   "tfw-boot.elf", "tfw-boot.hex", "uds-flashed.sha256"])

    def test_the_unpatched_images_are_unchanged(self):
        paths = [self.target.with_suffix(".elf"), self.target.with_suffix(".hex"),
                 self.bench, self.bench.with_suffix(".elf")]
        before = [sha256(p) for p in paths]
        r = self.flash()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual([sha256(p) for p in paths], before)
        self.assertEqual(self.uds_of(self.target.with_suffix(".hex")), b"\xff" * 32)

    def test_a_dry_run_does_nothing(self):
        r = self.flash("-n")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.writes(), [])
        self.assertEqual(list(self.tmpdir.iterdir()), [])
        self.assertFalse(self.record.exists())

    def test_a_row_without_a_uds_slot_is_refused(self):
        r = make([f"BOARD={BOARD}", f"BUILD_DIR={self.run_dir / 'build'}",
                  "BOOT_TIER=atecc608a", "BOOT_MEASURE=atecc608a", "BOOT_SIG=atecc608a",
                  "BOOT_OTA=1", "BOOT_STORE=0", f"BENCH_KEY={self.key}", "boot-flash-uds"],
                 self.env)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("boot-flash-uds: BOOT_TIER=atecc608a", r.stderr)
        self.assertEqual(self.writes(), [])

    def test_a_key_inside_the_tree_is_refused(self):
        inside = pathlib.Path(__file__).resolve().parent / "tfw-test-bench.pem"
        r = make([*self.vars, f"BENCH_KEY={inside}", "boot-flash-uds"], self.env)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("inside the repository tree", r.stderr)
        self.assertEqual(self.writes(), [])
        self.assertFalse(inside.exists())


if __name__ == "__main__":
    unittest.main()
