# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Every make flag that changes an image's objects names its build directory
(#161), and every default image keeps the path it had (make scripts-test).

Make does not track a flag: two configurations in one directory link each
other's objects, and the matrix passes on an image it never built. These
tests ask make for the directory (firmware-dir, boot-dir), so they need no
compiler. The default paths are literals on purpose: CI, reproducible-check,
the docs and the scripts name them."""

import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

# The flags under test, and the make state a parent make passes down: none of
# them may reach the child from the caller's environment.
SCRUB = ("MAKEFLAGS", "MAKELEVEL", "MFLAGS", "MAKEOVERRIDES", "BOARD", "CRYPTO", "ETH", "DHCP",
         "PROVISION", "OTA_STAGE", "MEASURE_SOFT_CSR", "SOFT_ID", "UECC_OPT", "BOOT_OPT_EXTRA",
         "BOOT_TIER", "BOOT_SIG", "BOOT_MEASURE", "BOOT_OTA", "BOOT_STORE", "APP_DIR", "BOOT_AB",
         "I2C", "SPI", "ETH_CHIP", "IMAGE_FLAGS", "HOST_BOARD", "HOST_EXTRA_DEFS")


def make(target, *flags):
    env = {k: v for k, v in os.environ.items() if k not in SCRUB}
    out = subprocess.run(["make", "-s", "--no-print-directory", target, *flags], cwd=REPO, env=env,
                         capture_output=True, text=True, check=False)
    if out.returncode != 0:
        raise AssertionError("make %s %s: rc=%d\n%s" % (target, " ".join(flags), out.returncode, out.stderr))
    return out.stdout.strip()


def fw(*flags):
    return make("firmware-dir", *flags)


def boot(*flags):
    return make("boot-dir", *flags)


class TestDefaultsUnchanged(unittest.TestCase):
    def test_firmware_defaults(self):
        for flags, want in (
                ((), "build/atmega328p/atecc608a"),
                (("CRYPTO=atecc608a",), "build/atmega328p/atecc608a"),
                (("BOARD=atmega2560",), "build/atmega2560/auto-eth-w5100"),
                (("BOARD=atmega2560", "CRYPTO=soft", "ETH=0"), "build/atmega2560/soft"),
                (("BOARD=atmega32u4",), "build/atmega32u4/soft"),
                (("BOARD=mps2-an505",), "build/mps2-an505/soft")):
            with self.subTest(flags=flags):
                self.assertEqual(want, fw(*flags))

    def test_boot_defaults(self):
        self.assertEqual("build/atmega328p/boot/atecc608a-atecc608a-atecc608a-ota0-store0", boot())
        self.assertEqual("build/atmega2560/boot/soft-soft-p256-soft-ota0-store0", boot("BOARD=atmega2560"))

    def test_a_default_spelled_out_keeps_the_path(self):
        # The board's own value, given on the command line, is the same image.
        self.assertEqual(fw(), fw("SOFT_ID=hmac", "UECC_OPT=0"))
        self.assertEqual(fw("BOARD=atmega2560"), fw("BOARD=atmega2560", "SOFT_ID=p256"))
        self.assertEqual(boot("BOARD=atmega2560"), boot("BOARD=atmega2560", "UECC_OPT=0", "BOOT_OPT_EXTRA="))


class TestFirmwareFlags(unittest.TestCase):
    def test_soft_id(self):
        # The 32U4's soft root defaults to hmac, the 2560's to p256.
        self.assertEqual("build/atmega32u4/soft-p256", fw("BOARD=atmega32u4", "SOFT_ID=p256"))
        self.assertEqual("build/atmega2560/soft-hmac", fw("BOARD=atmega2560", "CRYPTO=soft", "ETH=0", "SOFT_ID=hmac"))
        self.assertEqual("build/atmega2560/auto-eth-w5100-hmac", fw("BOARD=atmega2560", "SOFT_ID=hmac"))

    def test_the_328p_refuses_the_soft_tier(self):
        # The 328P is element-only (m522).
        with self.assertRaisesRegex(AssertionError, "CRYPTO=soft is refused on atmega328p"):
            fw("CRYPTO=soft")

    def test_soft_id_where_the_tier_does_not_link_it(self):
        self.assertEqual(fw("CRYPTO=atecc608a"), fw("CRYPTO=atecc608a", "SOFT_ID=p256"))

    def test_uecc_opt(self):
        base = ("BOARD=atmega2560", "CRYPTO=soft", "ETH=0")
        self.assertEqual("build/atmega2560/soft-uecc1", fw(*base, "UECC_OPT=1"))
        self.assertNotEqual(fw(*base, "UECC_OPT=1"), fw(*base, "UECC_OPT=2"))

    def test_uecc_opt_where_the_image_links_no_micro_ecc(self):
        # The 328P's element tier carries no micro-ecc.
        self.assertEqual(fw(), fw("UECC_OPT=1"))
        self.assertEqual(fw("CRYPTO=atecc608a"), fw("CRYPTO=atecc608a", "UECC_OPT=1"))

    def test_spi(self):
        self.assertEqual("build/mps2-an505/soft-spi", fw("BOARD=mps2-an505", "CRYPTO=soft", "SPI=1"))

    def test_row_buses_and_ab(self):
        # The MPS2 boards turn I2C, SPI and two slots on per row (profile.mk).
        self.assertEqual("build/mps2-an505/soft-i2c", fw("BOARD=mps2-an505", "I2C=1"))
        self.assertEqual("build/mps2-an500/soft-spi", fw("BOARD=mps2-an500", "SPI=1"))
        self.assertEqual("build/mps2-an505/soft-ab", fw("BOARD=mps2-an505", "BOOT_AB=1"))
        # The 2560 always has two slots and keeps its path.
        self.assertEqual(fw("BOARD=atmega2560"), fw("BOARD=atmega2560", "BOOT_AB=1"))

    def test_every_matrix_row_has_its_own_directory(self):
        rows = make("firmware-matrix-list").splitlines()
        self.assertTrue(rows)
        seen = {}
        for row in rows:
            board, tier, *flags = row.split()
            d = fw("BOARD=" + board, "CRYPTO=" + tier, *flags)
            with self.subTest(row=row):
                self.assertNotIn(d, seen, "%r and %r share %s" % (row, seen.get(d), d))
            seen[d] = row


class TestBootFlags(unittest.TestCase):
    def test_uecc_opt(self):
        self.assertEqual("build/atmega2560/boot/soft-soft-p256-soft-ota0-store0-uecc2",
                         boot("BOARD=atmega2560", "UECC_OPT=2"))

    def test_uecc_opt_where_the_boot_links_no_micro_ecc(self):
        self.assertEqual(boot(), boot("UECC_OPT=2"))

    def test_boot_opt_extra(self):
        dirs = {boot("BOARD=atmega2560"),
                boot("BOARD=atmega2560", "BOOT_OPT_EXTRA=-mcall-prologues"),
                boot("BOARD=atmega2560", "BOOT_OPT_EXTRA=-mcall-prologues -fno-jump-tables"),
                boot("BOARD=atmega2560", "BOOT_OPT_EXTRA=-Wl,--defsym=x='1'")}
        self.assertEqual(4, len(dirs))
        for d in dirs:
            self.assertTrue(d.startswith("build/atmega2560/boot/soft-soft-p256-soft-ota0-store0"), d)
            self.assertNotIn(" ", d)

    def test_boot_opt_extra_is_stable(self):
        flag = "BOOT_OPT_EXTRA=-mcall-prologues -fno-jump-tables"
        self.assertEqual(boot("BOARD=atmega2560", flag), boot("BOARD=atmega2560", flag))


# The other directories that compile firmware or harness sources (#161), each
# asked of the target that prints it.
OTHER_DIRS = ("bench-dir", "platform-bake-dir", "wiznet-probe-dir", "integration-dir",
              "boot-integration-dir", "host-dir")


class TestOtherDefaultsUnchanged(unittest.TestCase):
    def test_defaults(self):
        for target, flags, want in (
                ("bench-dir", (), "build/bench/atmega328p/atecc608a"),
                # bench-check's rows: the serial image on every board.
                ("bench-dir", ("ETH=0",), "build/bench/atmega328p/atecc608a"),
                ("bench-dir", ("BOARD=atmega2560", "ETH=0"), "build/bench/atmega2560/auto"),
                ("platform-bake-dir", (), "build/platform-bake/atmega328p"),
                ("platform-bake-dir", ("BOARD=atmega2560",), "build/platform-bake/atmega2560"),
                ("wiznet-probe-dir", ("BOARD=atmega2560",), "build/wiznet-probe/atmega2560"),
                ("integration-dir", (), "build/integration"),
                ("boot-integration-dir", ("BOARD=atmega2560",), "build/integration/atmega2560"),
                ("host-dir", (), "build/host")):
            with self.subTest(target=target, flags=flags):
                self.assertEqual(want, make(target, *flags))

    def test_a_default_spelled_out_keeps_the_path(self):
        self.assertEqual("build/integration", make("integration-dir", "BOARD=atmega328p"))
        self.assertEqual("build/integration/atmega2560",
                         make("boot-integration-dir", "BOARD=atmega2560", "UECC_OPT=0", "IMAGE_FLAGS=0x00"))
        self.assertEqual("build/host", make("host-dir", "HOST_BOARD=atmega328p",
                                            "HOST_EXTRA_DEFS=-DTFW_FEATURE_ATTEST=1 -DTFW_FEATURE_ALIAS_CERT=1"))


class TestOtherFlags(unittest.TestCase):
    def two(self, target, base, flag, a, b):
        dirs = {make(target, *base), make(target, *base, "%s=%s" % (flag, a)),
                make(target, *base, "%s=%s" % (flag, b))}
        self.assertEqual(3, len(dirs), "%s %s: %s" % (target, flag, sorted(dirs)))

    def test_bench_takes_the_firmware_row(self):
        base = ("BOARD=atmega2560", "ETH=0")
        self.assertEqual("build/bench/atmega2560/auto-eth-w5100", make("bench-dir", "BOARD=atmega2560"))
        self.assertEqual("build/bench/atmega2560/auto-hmac", make("bench-dir", *base, "SOFT_ID=hmac"))
        self.two("bench-dir", base, "UECC_OPT", "1", "2")
        self.two("bench-dir", base, "CRYPTO", "soft", "atecc608a")
        for flags in ((), ("ETH=0",), ("ETH=0", "PROVISION=1"), ("CRYPTO=soft", "ETH=0")):
            with self.subTest(flags=flags):
                self.assertEqual(fw("BOARD=atmega2560", *flags).split("/")[-1],
                                 make("bench-dir", "BOARD=atmega2560", *flags).split("/")[-1])

    def test_bake_and_probe_name_a_given_row(self):
        for target in ("platform-bake-dir", "wiznet-probe-dir"):
            with self.subTest(target=target):
                base = ("BOARD=atmega2560",)
                self.two(target, base, "CRYPTO", "soft", "atecc608a")
                self.two(target, base, "ETH", "0", "1")
                self.assertEqual("build/%s/atmega2560-soft-prov" % target[:-len("-dir")],
                                 make(target, *base, "CRYPTO=soft", "ETH=0", "PROVISION=1"))

    def test_integration_names_the_board(self):
        self.assertEqual("build/integration/atmega2560", make("integration-dir", "BOARD=atmega2560"))
        self.assertNotEqual(make("integration-dir", "BOARD=atmega2560"), make("integration-dir", "BOARD=atmega32u4"))

    def test_boot_integration(self):
        base = ("BOARD=atmega2560",)
        self.assertEqual("build/integration/atmega2560-uecc2", make("boot-integration-dir", *base, "UECC_OPT=2"))
        self.assertEqual("build/integration/atmega2560-flags0x01",
                         make("boot-integration-dir", *base, "IMAGE_FLAGS=0x01"))
        self.two("boot-integration-dir", base, "UECC_OPT", "1", "2")

    def test_host(self):
        self.assertEqual("build/host-atmega2560", make("host-dir", "HOST_BOARD=atmega2560"))
        self.two("host-dir", (), "HOST_BOARD", "atmega2560", "atmega32u4")
        self.two("host-dir", (), "HOST_EXTRA_DEFS", "-DTFW_FEATURE_ATTEST=1", "-DTFW_FEATURE_ATTEST=0")
        flag = "HOST_EXTRA_DEFS=-DTFW_FEATURE_ATTEST=0"
        self.assertEqual(make("host-dir", flag), make("host-dir", flag))
        self.assertTrue(make("host-dir", flag).startswith("build/host-x"))

    def test_every_other_dir_is_one_word(self):
        for target in OTHER_DIRS:
            with self.subTest(target=target):
                d = make(target, "BOARD=atmega2560", "HOST_EXTRA_DEFS=-DA=1 -DB='2'", "IMAGE_FLAGS=0x01")
                self.assertTrue(d.startswith("build/"), d)
                self.assertNotIn(" ", d)


if __name__ == "__main__":
    unittest.main()
