# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The build is reproducible, so the expected firmware hash is computable from
# source (docs/SPEC.md, row secure-boot). The application and tfw-boot of the
# ATmega2560 soft row are each built twice, from two clean copies of this tree
# in two different directories, with SOURCE_DATE_EPOCH set and the time zone
# changed between them, and the images must be byte-identical: the HEX a
# programmer writes and the measurement scripts/fwhash.py takes of it.
#
# A clean copy is the tree without build/ and .git: nothing a previous build
# left behind can be reused, so each image is compiled and linked here. `make
# reproducible-check` and `make bootloader-reproducible` build from `git archive
# HEAD` (scripts/repro_build.py, tested by scripts/test_repro_build.py).
#
# Needs the AVR toolchain, as `make build` does: run it in the toolchain image
# (scripts/in-toolchain-image.sh). Without avr-gcc it fails; it never skips.

import importlib.util
import os
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

_spec = importlib.util.spec_from_file_location("fwhash", os.path.join(HERE, "fwhash.py"))
fwhash = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fwhash)

SOURCE_DATE_EPOCH = "1700000000"
BOARD = "atmega2560"
# The soft base row of mk/boot.mk's BOOT_SUPPORTED: P-256 verify, as on every row.
APP_ARGS = ["firmware", f"BOARD={BOARD}", "CRYPTO=soft"]
BOOT_ARGS = ["bootloader", f"BOARD={BOARD}", "BOOT_TIER=soft", "BOOT_MEASURE=soft",
             "BOOT_SIG=p256-soft", "BOOT_OTA=0", "BOOT_STORE=0"]
BUILT = re.compile(r"^built (\S+\.hex)$", re.MULTILINE)


def clean_copy(dst):
    def ignore(path, names):
        if os.path.abspath(path) == ROOT:
            return [n for n in names if n in (".git", "build", "coverage")]
        return [n for n in names if n == "__pycache__"]
    shutil.copytree(ROOT, dst, symlinks=True, ignore=ignore)


def build(tree, args, tz):
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
    env.update(SOURCE_DATE_EPOCH=SOURCE_DATE_EPOCH, TZ=tz, LC_ALL="C")
    run = subprocess.run(["make", "-C", tree, "--no-print-directory", f"-j{os.cpu_count() or 1}"] + args,
                         env=env, capture_output=True, text=True, timeout=900, check=False)
    if run.returncode != 0:
        raise AssertionError(f"make {' '.join(args)} in {tree} exited {run.returncode}:\n"
                             + "\n".join((run.stdout + run.stderr).splitlines()[-30:]))
    found = BUILT.findall(run.stdout)
    if len(found) != 1:
        raise AssertionError(f"make {' '.join(args)} did not say which image it built:\n{run.stdout[-2000:]}")
    with open(os.path.join(tree, found[0]), encoding="ascii") as f:
        return found[0], f.read()


class Reproducible(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if shutil.which("avr-gcc") is None:
            raise AssertionError("avr-gcc is not on PATH: this test builds the AVR images, so it "
                                 "runs in the toolchain image (scripts/in-toolchain-image.sh)")
        cls.tmp = tempfile.TemporaryDirectory(prefix="tfw-repro-")
        cls.a = os.path.join(cls.tmp.name, "a")
        cls.b = os.path.join(cls.tmp.name, "a-longer", "second", "b")
        clean_copy(cls.a)
        clean_copy(cls.b)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def assert_reproducible(self, args):
        path_a, hex_a = build(self.a, args, "UTC")
        path_b, hex_b = build(self.b, args, "Pacific/Kiritimati")
        self.assertEqual(path_a, path_b)
        ma, na = fwhash.measure(hex_a)
        mb, nb = fwhash.measure(hex_b)
        self.assertEqual((ma.hex(), na), (mb.hex(), nb),
                         f"{path_a}: two clean builds measure differently")
        self.assertEqual(hex_a, hex_b, f"{path_a}: two clean builds are not byte-identical")

    def test_the_application_builds_byte_identical_twice(self):
        self.assert_reproducible(APP_ARGS)

    def test_tfw_boot_builds_byte_identical_twice(self):
        self.assert_reproducible(BOOT_ARGS)


if __name__ == "__main__":
    unittest.main()
