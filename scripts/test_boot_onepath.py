# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for one update path per image (make scripts-test; #52, slice 7).

tfw-boot opens a session over the UART (BOOT_OTA=1) or installs what the
application staged in the external store (BOOT_STORE=1), never both, and the
2560's application links the one path its OTA_STAGE names. These tests read
what make decides, without building: the refusal at parse time, and the object
list `firmware` links. The rows and file names are literals, as the caller
types them.
"""

import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

ELEMENT = ("BOOT_TIER=atecc608a", "BOOT_MEASURE=atecc608a", "BOOT_SIG=atecc608a")
SOFT = ("BOOT_TIER=soft", "BOOT_MEASURE=soft", "BOOT_SIG=p256-soft")
CHOICES = ("Choose BOOT_OTA=1 BOOT_STORE=0, OTA in tfw-boot, for an application built OTA_STAGE=0",
           "or BOOT_OTA=0 BOOT_STORE=1, the external store, for an application built OTA_STAGE=1")


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


def srcs(*args):
    """The object list `firmware` links for the 2560 and the flags given."""
    rc, out = make("firmware-srcs", "BOARD=atmega2560", *args)
    if rc != 0:
        raise AssertionError(out)
    return out.split()


class BootOnePath(unittest.TestCase):
    def test_both_paths_are_refused_with_the_two_choices(self):
        for goal in ("bootloader", "boot-flash"):
            for tier in (ELEMENT, SOFT):
                with self.subTest(goal=goal, tier=tier[0]):
                    rc, out = make(goal, "BOARD=atmega2560", *tier,
                                   "BOOT_OTA=1", "BOOT_STORE=1", "MAKE=echo")
                    self.assertNotEqual(rc, 0, out)
                    self.assertIn(goal + ": BOOT_OTA=1 BOOT_STORE=1 is two update paths, "
                                  "and an image takes one.", out)
                    for choice in CHOICES:
                        self.assertIn(choice, out)
                    self.assertNotIn("is not a supported configuration", out)
                    # Refused at parse time: the error is all make printed.
                    self.assertEqual(len(out.strip().splitlines()), 1, out)
                    self.assertTrue(out.startswith("mk/boot.mk:"), out)

    def test_each_path_alone_is_not_refused_as_two(self):
        for ota, store in (("1", "0"), ("0", "1")):
            with self.subTest(ota=ota, store=store):
                rc, out = make("bootloader-candidate",
                               "BOOT_CANDIDATE=atmega2560:atecc608a:atecc608a:atecc608a:"
                               + ota + ":" + store, "MAKE=echo")
                self.assertNotIn("is two update paths", out)
                self.assertIn("is already in BOOT_SUPPORTED", out)

    def test_both_paths_can_still_be_measured(self):
        rc, out = make("bootloader-candidate",
                       "BOOT_CANDIDATE=atmega2560:atecc608a:atecc608a:atecc608a:1:1", "MAKE=echo")
        self.assertEqual(rc, 0, out)
        words = out.split()
        for w in ("boot-size-check", "BOOT_OTA=1", "BOOT_STORE=1"):
            self.assertIn(w, words)
        self.assertNotIn("is two update paths", out)

    def test_a_stage_image_links_the_store_path_alone(self):
        for eth in ("ETH=0", "ETH=1"):
            for tier in ("CRYPTO=atecc608a", "CRYPTO=soft", "CRYPTO=auto"):
                with self.subTest(eth=eth, tier=tier):
                    s = srcs(eth, tier, "OTA_STAGE=1")
                    self.assertIn("src/core/ota_stage.c", s)
                    self.assertIn("src/core/ota_activate.c", s)
                    self.assertIn("src/core/update_request_store.c", s)
                    self.assertNotIn("src/core/update_request.c", s)
                    self.assertNotIn("src/core/update_request_none.c", s)

    def test_an_image_without_the_stager_links_the_boot_session_path_alone(self):
        for eth in ("ETH=0", "ETH=1"):
            for tier in ("CRYPTO=atecc608a", "CRYPTO=soft", "CRYPTO=auto"):
                with self.subTest(eth=eth, tier=tier):
                    s = srcs(eth, tier, "OTA_STAGE=0")
                    self.assertIn("src/core/update_request.c", s)
                    self.assertIn("src/core/ota_stage_none.c", s)
                    for f in ("src/core/update_request_store.c", "src/core/ota_stage.c",
                              "src/core/ota_activate.c", "src/core/ota_ctl.c",
                              "src/store/spi_nor.c"):
                        self.assertNotIn(f, s)

    def test_the_ethernet_default_is_the_store_path(self):
        s = srcs()
        self.assertIn("src/core/update_request_store.c", s)
        self.assertNotIn("src/core/update_request.c", s)


if __name__ == "__main__":
    unittest.main()
