#!/usr/bin/env python3
"""Tests for prov-absent-check.py: the rules on literal object lists, the plan
of make variable sets, and the sweep of this tree's own object lists -- the
gate itself (row device-provisioning). Needs make, no toolchain, no network."""

import importlib.util
import os
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("prov_absent_check", os.path.join(HERE, "prov-absent-check.py"))
pac = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pac)

SRCS = ["src/main.c", "src/core/dispatch.c", "src/crypto/soft/provider.c", "src/proto/frame.c"]
JIG = SRCS + ["src/caps/provision.c", "src/caps/provision_atecc.c"]
DEFS = ["-DTFW_FEATURE_ALIAS_CERT=1"]
JIG_DEFS = ["-DTFW_FEATURE_PROVISION=1"] + DEFS
BOOT = ["boot/boot_main.c", "boot/boot_image.c", "src/proto/crc16.c"]


def prod(srcs=SRCS, defs=DEFS, boot=BOOT):
    return ("BOARD=b CRYPTO=soft PROVISION=0", 0, list(srcs), list(defs), list(boot))


def jig(srcs=JIG, defs=JIG_DEFS, boot=BOOT):
    return ("BOARD=b CRYPTO=soft PROVISION=1", 1, list(srcs), list(defs), list(boot))


class Rules(unittest.TestCase):
    def test_a_clean_production_and_jig_pair_passes(self):
        self.assertEqual(pac.check([prod(), jig()]), [])

    def test_a_production_image_linking_the_wire_fails(self):
        fails = pac.check([prod(srcs=SRCS + ["src/caps/provision.c"]), jig()])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=0: a production image links src/caps/provision.c"])

    def test_a_new_backend_is_caught_by_pattern(self):
        fails = pac.check([prod(srcs=SRCS + ["src/caps/provision_se05x.c"]), jig()])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=0: a production image links src/caps/provision_se05x.c"])

    def test_an_architecture_half_is_caught(self):
        fails = pac.check([prod(srcs=SRCS + ["src/hal/esp32/prov_uds.c"]), jig()])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=0: a production image links src/hal/esp32/prov_uds.c"])

    def test_a_crypto_provider_is_not_provisioning(self):
        self.assertEqual(pac.prov_objects(["src/crypto/auto/provider.c", "src/crypto/atecc608a/provider.c"]), [])

    def test_a_production_image_defining_the_feature_fails(self):
        fails = pac.check([prod(defs=DEFS + ["-DTFW_FEATURE_PROVISION=1"]), jig()])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=0: a production image compiles with -DTFW_FEATURE_PROVISION=1"])

    def test_a_jig_without_the_wire_is_vacuous(self):
        fails = pac.check([prod(), jig(srcs=SRCS)])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=1: the jig image lacks src/caps/provision.c -- the check would be vacuous"])

    def test_a_jig_without_the_define_is_vacuous(self):
        fails = pac.check([prod(), jig(defs=DEFS)])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=1: the jig image lacks -DTFW_FEATURE_PROVISION=1 -- the check would be vacuous"])

    def test_a_boot_image_linking_provisioning_fails_jig_or_not(self):
        bad = BOOT + ["src/caps/provision.c"]
        fails = pac.check([prod(boot=bad), jig(boot=bad)])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=0: the boot image links src/caps/provision.c",
                                 "BOARD=b CRYPTO=soft PROVISION=1: the boot image links src/caps/provision.c"])

    def test_a_boot_list_without_boot_main_means_the_probe_read_nothing(self):
        fails = pac.check([prod(boot=[]), jig()])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=0: the boot list lacks boot/boot_main.c -- the probe read nothing"])

    def test_an_empty_object_list_fails(self):
        fails = pac.check([prod(srcs=[]), jig()])
        self.assertEqual(fails, ["BOARD=b CRYPTO=soft PROVISION=0: the object list is empty"])

    def test_no_production_or_no_jig_image_fails(self):
        self.assertEqual(pac.check([jig()]), ["no production image was listed"])
        self.assertEqual(pac.check([prod()]), ["no jig image was listed"])
        self.assertEqual(pac.check([]), ["no production image was listed", "no jig image was listed"])


BAKE = ["tools/platform-bake/main.c", "tools/platform-bake/bake.c", "boot/boot_platform.c",
        "src/proto/crc16.c", "src/hal/avr/hal_eeprom.c", "src/hal/avr/hal_uart.c"]


class BakeRules(unittest.TestCase):
    def test_a_clean_bake_list_passes(self):
        self.assertEqual(pac.check_bake([("bake b", BAKE)]), [])

    def test_each_element_or_provisioning_unit_fails(self):
        for s in ("src/caps/provision.c", "src/caps/provision_atecc.c", "src/hal/esp32/prov_uds.c",
                  "src/crypto/atecc608a/atecc.c", "src/crypto/soft/uds_flash.c", "src/crypto/auto/provider.c",
                  "src/crypto/deviceid_p256.c", "src/hal/esp32/crypto_hmac_p256.c",
                  "src/core/ota_floor_atecc.c", "src/hal/avr/hal_bus.c"):
            with self.subTest(s=s):
                self.assertEqual(pac.check_bake([("bake b", BAKE + [s])]), [f"bake b: the bake image links {s}"])

    def test_a_list_without_bake_c_means_the_probe_read_nothing(self):
        fails = pac.check_bake([("bake b", ["src/main.c"])])
        self.assertEqual(fails, ["bake b: the bake image's list lacks tools/platform-bake/bake.c -- the probe read nothing"])

    def test_no_bake_image_fails(self):
        self.assertEqual(pac.check_bake([]), ["no bake image was listed"])


class Plan(unittest.TestCase):
    def test_a_board_with_a_controller_is_asked_with_and_without_ethernet(self):
        got = pac.plan({"m": (["soft"], "w5100")}, [])
        bases = sorted({" ".join(v[:-1]) for v in got})
        self.assertEqual(bases, ["BOARD=m CRYPTO=soft", "BOARD=m CRYPTO=soft APP_DIR=test/app/example",
                                 "BOARD=m CRYPTO=soft ETH=0", "BOARD=m CRYPTO=soft ETH=1"])

    def test_a_board_without_a_controller_is_asked_as_it_defaults(self):
        got = pac.plan({"t": (["none"], "")}, [])
        self.assertEqual(got, [["BOARD=t", "CRYPTO=none", "PROVISION=0"], ["BOARD=t", "CRYPTO=none", "PROVISION=1"],
                               ["BOARD=t", "CRYPTO=none", "APP_DIR=test/app/example", "PROVISION=0"],
                               ["BOARD=t", "CRYPTO=none", "APP_DIR=test/app/example", "PROVISION=1"]])

    def test_every_tier_is_asked_both_ways(self):
        got = pac.plan({"m": (["soft", "atecc608a"], "")}, [])
        for tier in ("soft", "atecc608a"):
            for p in (0, 1):
                self.assertIn(["BOARD=m", f"CRYPTO={tier}", f"PROVISION={p}"], got)

    def test_a_matrix_row_keeps_its_variables_and_a_plain_one_adds_nothing(self):
        got = pac.plan({}, [["n", "soft", "ETH=1", "OTA_STAGE=0"], ["n", "soft"]])
        self.assertEqual(got, [["BOARD=n", "CRYPTO=soft", "ETH=1", "OTA_STAGE=0", "PROVISION=0"],
                               ["BOARD=n", "CRYPTO=soft", "ETH=1", "OTA_STAGE=0", "PROVISION=1"]])

    def test_a_tier_with_no_provisioning_backend_is_asked_as_production_only(self):
        got = pac.plan({"m": (["soft", "optiga"], "")}, [["m", "optiga", "ETH=1"]],
                       frozenset({("m", "optiga")}))
        self.assertEqual(got, [["BOARD=m", "CRYPTO=soft", "PROVISION=0"],
                               ["BOARD=m", "CRYPTO=soft", "PROVISION=1"],
                               ["BOARD=m", "CRYPTO=optiga", "PROVISION=0"],
                               ["BOARD=m", "CRYPTO=soft", "APP_DIR=test/app/example", "PROVISION=0"],
                               ["BOARD=m", "CRYPTO=soft", "APP_DIR=test/app/example", "PROVISION=1"],
                               ["BOARD=m", "CRYPTO=optiga", "ETH=1", "PROVISION=0"]])


class Tree(unittest.TestCase):
    """The gate: this tree's object lists, asked of its own make files."""

    def test_no_production_image_links_a_provisioning_unit(self):
        images = pac.sweep()
        self.assertEqual(pac.check(images), [])
        boards = {label.split()[0] for label, *_ in images}
        self.assertIn("BOARD=atmega2560", boards)
        self.assertIn("BOARD=esp32-qemu", boards)

    def test_the_sweep_reads_what_make_links(self):
        # A production image forced to link the wire and define the feature, on
        # the command line, must fail: the sweep reads make, not a copy of it.
        image = pac.ask(["BOARD=atmega2560", "CRYPTO=soft", "PROVISION=0",
                         "PROV_SRCS=src/caps/provision.c", "PROV_DEFS=-DTFW_FEATURE_PROVISION=1"])
        label = image[0]
        self.assertEqual(pac.check([image, jig()]), [f"{label}: a production image links src/caps/provision.c",
                                                     f"{label}: a production image compiles with -DTFW_FEATURE_PROVISION=1"])

    def test_a_tier_make_refuses_to_provision_is_planned_production_only(self):
        # CRYPTO=optiga's PROVISION=1 is an $(error) in mk/firmware.mk; make's
        # provision-backend says so, the sweep asks no jig of it, and its
        # production image is still held to the rule.
        none = pac.unprovisioned_pairs([("atmega2560", "optiga"), ("atmega2560", "soft")])
        self.assertEqual(none, frozenset({("atmega2560", "optiga")}))
        labels = [image[0] for image in pac.sweep()]
        optiga = [lb for lb in labels if "CRYPTO=optiga" in lb.split()]
        self.assertTrue(optiga)
        self.assertEqual([lb for lb in optiga if "PROVISION=0" not in lb.split()], [])
        image = pac.ask(["BOARD=atmega2560", "CRYPTO=optiga", "PROVISION=0",
                         "PROV_SRCS=src/caps/provision.c"])
        self.assertEqual(pac.check([image, jig()]), [f"{image[0]}: a production image links src/caps/provision.c"])

    def test_the_boot_probe_reads_the_boot_list(self):
        image = pac.ask(["BOARD=atmega2560", "CRYPTO=soft", "PROVISION=0"])
        self.assertIn("boot/boot_main.c", image[4])
        self.assertNotIn("boot/boot_main.c", image[2])

    def test_no_bake_image_links_an_element_or_provisioning_unit(self):
        bakes = pac.sweep_bake()
        self.assertEqual(pac.check_bake(bakes), [])
        self.assertIn("platform-bake BOARD=atmega2560", [label for label, _ in bakes])

    def test_the_bake_probe_reads_what_make_links(self):
        # The bake list forced to link the element's driver, on the command
        # line, must fail: the probe reads make, not a copy of it.
        bake = pac.ask_bake(["BOARD=atmega2560",
                             "PLATBAKE_SRCS=tools/platform-bake/bake.c src/crypto/atecc608a/atecc.c"])
        self.assertEqual(pac.check_bake([bake]),
                         ["platform-bake BOARD=atmega2560 PLATBAKE_SRCS=tools/platform-bake/bake.c "
                          "src/crypto/atecc608a/atecc.c: the bake image links src/crypto/atecc608a/atecc.c"])

    def test_a_make_failure_is_an_error_not_a_pass(self):
        with self.assertRaises(RuntimeError):
            pac.make(["no-such-target-tfw"])

    def test_a_parent_make_s_flags_are_not_inherited(self):
        with mock.patch.dict(os.environ, {"MAKEFLAGS": "-j8 BOARD=attiny85", "MAKELEVEL": "1"}):
            self.assertNotIn("MAKEFLAGS", pac.make_env())
            self.assertNotIn("MAKELEVEL", pac.make_env())
            image = pac.ask(["BOARD=atmega2560", "CRYPTO=soft", "PROVISION=0"])
        self.assertIn("src/crypto/soft/provider.c", image[2])


if __name__ == "__main__":
    unittest.main()
