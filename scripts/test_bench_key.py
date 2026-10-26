"""Tests for mk/boot-bench.mk: the bench Mega's signing key (#7; rulings 188, 189).

DEVELOPMENT ONLY, as the key is. Each test runs the real make targets. Every
key here is a throwaway, generated in a temporary directory that the test
removes; HOME points into that directory too, so the default BENCH_KEY is a
throwaway as well, and no test ever reads a real bench key.

The refusals run without avr-gcc: they are parse-time, or stop before the
link (`make -o`). The bench image is built, so that class needs avr-gcc and
fails without it; it never skips.
"""

import hashlib
import os
import pathlib
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
BOARD = "atmega2560"
ROW = ["BOOT_TIER=soft", "BOOT_MEASURE=soft", "BOOT_SIG=p256-soft",
       "BOOT_OTA=0", "BOOT_STORE=0"]
KEY_TAIL = pathlib.Path(".config/trusted-firmware/bench-p256.pem")

# A P-256 SubjectPublicKeyInfo's DER, up to the point's X || Y: RFC 5480 §2.
SPKI_P256 = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d03010703420004")


def make(args, env=None):
    return subprocess.run(["make", "--no-print-directory", *args], cwd=ROOT,
                          env=env, capture_output=True, text=True)


def make_var(expr, args):
    """What make expands `expr` to, given these variables."""
    out = subprocess.run(
        ["make", "-s", "--no-print-directory", *args,
         "--eval", f"tfw-print: ; @echo {expr}", "tfw-print"],
        cwd=ROOT, capture_output=True, text=True, check=True)
    return out.stdout.strip().splitlines()[-1]


def require_avr_gcc():
    if shutil.which("avr-gcc") is None:
        raise AssertionError("avr-gcc is not on PATH: this test builds tfw-boot, so it "
                             "runs in the toolchain image (scripts/in-toolchain-image.sh)")


def throwaway_key(path, curve="P-256"):
    """A key no bench ever sees, mode 600."""
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
                    f"ec_paramgen_curve:{curve}", "-out", str(path)],
                   check=True, capture_output=True)
    path.chmod(0o600)
    return path


def public_der(key):
    return subprocess.run(["openssl", "pkey", "-in", str(key), "-pubout", "-outform", "DER"],
                          check=True, capture_output=True).stdout


def ihex(path):
    """An Intel HEX file's bytes, address to byte."""
    mem, base = {}, 0
    for line in path.read_text().split():
        rec = bytes.fromhex(line[1:])
        n, addr, kind, data = rec[0], rec[1] << 8 | rec[2], rec[3], rec[4:4 + rec[0]]
        if kind == 0:
            for i in range(n):
                mem[base + addr + i] = data[i]
        elif kind == 2:
            base = (data[0] << 8 | data[1]) << 4
        elif kind == 4:
            base = (data[0] << 8 | data[1]) << 16
        elif kind == 1:
            break
    return mem


def symbol(nm, elf, name):
    """(address, size) of `name` in `elf`, from the ELF itself."""
    out = subprocess.run([nm, "-S", str(elf)], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) == 4 and f[3] == name:
            return int(f[0], 16), int(f[1], 16)
    raise AssertionError(f"{elf} has no {name}")


def der_signature(rs):
    """r || s, 32 bytes each, as the DER ECDSA-Sig-Value openssl verifies."""
    def integer(b):
        b = b.lstrip(b"\0") or b"\0"
        if b[0] & 0x80:
            b = b"\0" + b
        return bytes([0x02, len(b)]) + b
    body = integer(rs[:32]) + integer(rs[32:])
    return bytes([0x30, len(body)]) + body


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class TempHome(unittest.TestCase):
    """HOME, and with it the default BENCH_KEY, inside a temporary directory."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="tfw-bench-")
        self.dir = pathlib.Path(self.tmp.name)
        self.home = self.dir / "home"
        self.home.mkdir()
        self.build = self.dir / "build"
        self.env = dict(os.environ, HOME=str(self.home))

    def tearDown(self):
        self.tmp.cleanup()

    def run_make(self, *args):
        return make([f"BOARD={BOARD}", f"BUILD_DIR={self.build}", *ROW, *args], self.env)


class Refusals(TempHome):

    def assert_refused_inside(self, key):
        for goal in ("bench-key", "boot-bench", "bench-sign"):
            with self.subTest(goal=goal, key=str(key)):
                r = self.run_make(goal, f"BENCH_KEY={key}")
                self.assertNotEqual(r.returncode, 0, r.stdout)
                self.assertIn("REFUSED", r.stderr)
                self.assertIn("inside the repository tree", r.stderr)
                self.assertFalse((ROOT / "build" / "tfw-test-bench.pem").exists())

    def test_a_key_inside_the_tree_is_refused(self):
        self.assert_refused_inside(ROOT / "build" / "tfw-test-bench.pem")

    def test_a_relative_key_path_is_inside_the_tree(self):
        self.assert_refused_inside("tfw-test-bench.pem")

    def test_the_tree_itself_is_refused(self):
        self.assert_refused_inside(ROOT)

    def test_a_link_into_the_tree_is_refused(self):
        link = self.dir / "checkout"
        link.symlink_to(ROOT)
        self.assert_refused_inside(link / "build" / "tfw-test-bench.pem")

    def test_a_path_with_a_space_is_refused(self):
        r = self.run_make("bench-key", f"BENCH_KEY={self.dir}/a b.pem")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("REFUSED", r.stderr)
        self.assertEqual(list(self.dir.glob("a*")), [])

    def test_a_tree_named_like_the_checkout_is_not_inside_it(self):
        # $(BENCH_TREE)/% is the test, not a string prefix: a sibling whose
        # name starts with the checkout's is outside it.
        key = pathlib.Path(str(ROOT) + "-tfw-test-sibling") / "k.pem"
        r = self.run_make("bench-sign", f"BENCH_KEY={key}")
        self.assertNotIn("REFUSED", r.stderr)
        self.assertIn("there is no bench key", r.stderr)
        self.assertFalse(key.parent.exists())


class OnlyWhenAsked(TempHome):

    def default_key(self):
        return self.home / KEY_TAIL

    def test_signing_without_a_key_makes_none(self):
        r = self.run_make("bench-sign")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn(f"there is no bench key at {self.default_key()}", r.stderr)
        self.assertIn("make bench-key creates one", r.stderr)
        self.assertEqual(list(self.home.iterdir()), [])

    def test_the_bench_image_without_a_key_makes_none(self):
        # The link is marked as made: the key is what stops it.
        elf = make_var("$(BOOT_TARGET).elf", [f"BOARD={BOARD}", f"BUILD_DIR={self.build}", *ROW])
        r = self.run_make("-o", elf, "boot-bench")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("make bench-key creates one", r.stderr)
        self.assertEqual(list(self.home.iterdir()), [])
        self.assertFalse(pathlib.Path(elf.replace("tfw-boot.elf", "tfw-boot-bench.elf")).exists())

    def test_bench_key_makes_a_p256_key_mode_600_at_the_default(self):
        r = self.run_make("bench-key")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        key = self.default_key()
        self.assertEqual(stat.S_IMODE(key.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(key.parent.stat().st_mode), 0o700)
        self.assertEqual(public_der(key)[:len(SPKI_P256)], SPKI_P256)
        self.assertEqual(len(public_der(key)), len(SPKI_P256) + 64)
        self.assertIn("development only", r.stdout)
        self.assertIn(hashlib.sha256(public_der(key)).hexdigest(), r.stdout)
        self.assertNotIn("PRIVATE", r.stdout + r.stderr)
        self.assertEqual(sorted(p.name for p in key.parent.iterdir()), [key.name])

    def test_bench_key_keeps_a_key_already_there(self):
        self.assertEqual(self.run_make("bench-key").returncode, 0)
        before = sha256(self.default_key())
        r = self.run_make("bench-key")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("kept", r.stdout)
        self.assertEqual(sha256(self.default_key()), before)

    def test_bench_key_refuses_a_key_others_can_read(self):
        key = throwaway_key(self.dir / "keys" / "k.pem")
        key.chmod(0o644)
        r = self.run_make("bench-key", f"BENCH_KEY={key}")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("REFUSED", r.stderr)
        self.assertIn("mode 644", r.stderr)

    def test_a_key_not_on_p256_is_refused(self):
        key = throwaway_key(self.dir / "keys" / "k384.pem", curve="P-384")
        elf = make_var("$(BOOT_TARGET).elf", [f"BOARD={BOARD}", f"BUILD_DIR={self.build}", *ROW])
        r = self.run_make("-o", elf, "boot-bench", f"BENCH_KEY={key}")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("is not a P-256 key", r.stderr)
        self.assertFalse(pathlib.Path(elf.replace("tfw-boot.elf", "tfw-boot-bench.elf")).exists())

    def test_the_bench_image_is_the_soft_tiers_only(self):
        r = make([f"BOARD={BOARD}", f"BUILD_DIR={self.build}", "BOOT_TIER=atecc608a",
                  "BOOT_MEASURE=atecc608a", "BOOT_SIG=atecc608a", "BOOT_OTA=1", "BOOT_STORE=0",
                  "boot-bench"], self.env)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("has no key slot in flash", r.stderr)

    def test_signing_needs_the_span_to_sign(self):
        key = throwaway_key(self.dir / "keys" / "k.pem")
        r = self.run_make("bench-sign", f"BENCH_KEY={key}")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("make image-tbs first", r.stderr)


class BenchImage(unittest.TestCase):
    """The bench image, built once for the class, and an image signed for it."""

    @classmethod
    def setUpClass(cls):
        require_avr_gcc()
        cls.tmp = tempfile.TemporaryDirectory(prefix="tfw-bench-")
        cls.dir = pathlib.Path(cls.tmp.name)
        cls.env = dict(os.environ, HOME=str(cls.dir / "home"))
        cls.key = throwaway_key(cls.dir / "keys" / "k.pem")
        cls.vars = [f"BOARD={BOARD}", f"BUILD_DIR={cls.dir / 'build'}", *ROW,
                    f"BENCH_KEY={cls.key}"]
        r = make([*cls.vars, "bootloader"], cls.env)
        if r.returncode != 0:
            cls.tmp.cleanup()
            raise AssertionError(r.stdout[-2000:] + r.stderr[-2000:])
        cls.target = pathlib.Path(make_var("$(BOOT_TARGET)", cls.vars))
        cls.plain = {s: sha256(cls.target.with_suffix(s)) for s in (".elf", ".hex")}
        cls.bench = make([*cls.vars, "boot-bench"], cls.env)
        cls.nm = make_var("$(ARCH_NM)", cls.vars)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def bench_path(self, suffix):
        return self.target.parent / f"tfw-boot-bench{suffix}"

    def test_the_bench_image_is_built(self):
        self.assertEqual(self.bench.returncode, 0, self.bench.stdout + self.bench.stderr)
        self.assertIn("development only", self.bench.stdout)
        self.assertNotIn("PRIVATE", self.bench.stdout + self.bench.stderr)
        self.assertEqual(sorted(p.name for p in self.target.parent.glob("tfw-boot-bench*")),
                         ["tfw-boot-bench.elf", "tfw-boot-bench.hex"])

    def test_only_the_key_slot_differs_and_holds_the_public_half(self):
        at, size = symbol(self.nm, self.target.with_suffix(".elf"), "tfw_boot_p256_key")
        self.assertEqual(size, 64)
        plain = ihex(self.target.with_suffix(".hex"))
        bench = ihex(self.bench_path(".hex"))
        self.assertEqual(plain.keys(), bench.keys())
        differ = {a for a in plain if plain[a] != bench[a]}
        self.assertTrue(differ)
        self.assertLessEqual(differ, set(range(at, at + 64)))
        self.assertEqual(bytes(plain[a] for a in range(at, at + 64)), b"\xff" * 64)
        self.assertEqual(bytes(bench[a] for a in range(at, at + 64)),
                         public_der(self.key)[len(SPKI_P256):])

    def test_the_bench_elf_carries_the_same_slot(self):
        at, size = symbol(self.nm, self.bench_path(".elf"), "tfw_boot_p256_key")
        self.assertEqual((at, size),
                         symbol(self.nm, self.target.with_suffix(".elf"), "tfw_boot_p256_key"))

    def test_the_unpatched_image_is_unchanged(self):
        self.assertEqual({s: sha256(self.target.with_suffix(s)) for s in (".elf", ".hex")},
                         self.plain)

    def test_a_patched_slot_is_never_patched_again(self):
        bytes_file = self.dir / "slot.bin"
        bytes_file.write_bytes(b"\0" * 64)
        out = self.dir / "again.elf"
        r = make([*self.vars, "--eval",
                  f"tfw-again: ; @$(call BOOT_SLOT_PATCH,{self.bench_path('.elf')},"
                  f"tfw_boot_p256_key,{bytes_file},{out})", "tfw-again"], self.env)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("is not the erased placeholder; nothing patched", r.stderr)
        self.assertFalse(out.exists())
        self.assertEqual(list(self.dir.glob("again*")), [])

    def test_a_slot_of_the_wrong_size_is_refused(self):
        bytes_file = self.dir / "short.bin"
        bytes_file.write_bytes(b"\0" * 63)
        out = self.dir / "short.elf"
        r = make([*self.vars, "--eval",
                  f"tfw-short: ; @$(call BOOT_SLOT_PATCH,{self.target.with_suffix('.elf')},"
                  f"tfw_boot_p256_key,{bytes_file},{out})", "tfw-short"], self.env)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("is 64 bytes", r.stderr)
        self.assertFalse(out.exists())

    def test_an_image_signed_with_the_key_verifies_against_the_image(self):
        sign_vars = [f"BOARD={BOARD}", "CRYPTO=soft", f"BUILD_DIR={self.dir / 'build'}",
                     f"BENCH_KEY={self.key}", "IMAGE_VERSION=1"]
        for goal in ("image-tbs", "bench-sign"):
            r = make([*sign_vars, goal], self.env)
            self.assertEqual(r.returncode, 0, goal + r.stdout[-2000:] + r.stderr[-2000:])
            self.assertNotIn("PRIVATE", r.stdout + r.stderr)
        out = pathlib.Path(make_var("$(SUIT_OUT)", sign_vars))
        sig = pathlib.Path(f"{out}.sig")
        self.assertEqual(sig.stat().st_size, 64)
        self.assertFalse(pathlib.Path(f"{out}.sig.der").exists())
        r = make([*sign_vars, "image-seal", f"IMAGE_SIG={sig}"], self.env)
        self.assertEqual(r.returncode, 0, r.stdout[-2000:] + r.stderr[-2000:])
        self.assertIn(sig.read_bytes(), pathlib.Path(f"{out}.region").read_bytes())

        # The public half as the image carries it, not as the key file holds it.
        at, _ = symbol(self.nm, self.target.with_suffix(".elf"), "tfw_boot_p256_key")
        bench = ihex(self.bench_path(".hex"))
        pub = self.dir / "image-pub.der"
        pub.write_bytes(SPKI_P256 + bytes(bench[a] for a in range(at, at + 64)))
        der = self.dir / "sig.der"
        der.write_bytes(der_signature(sig.read_bytes()))
        tbs = pathlib.Path(f"{out}.tbs")
        self.assertEqual(tbs.stat().st_size, 48)

        def verify(span):
            return subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(pub), "-keyform",
                                   "DER", "-signature", str(der), str(span)],
                                  capture_output=True, text=True)
        r = verify(tbs)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("Verified OK", r.stdout)
        tampered = self.dir / "tampered.tbs"
        tampered.write_bytes(bytes([tbs.read_bytes()[0] ^ 1]) + tbs.read_bytes()[1:])
        self.assertNotEqual(verify(tampered).returncode, 0)


if __name__ == "__main__":
    unittest.main()
