#!/usr/bin/env python3
"""prov-absent-check.py's prov_defs and bake_objects filters, on literal lists."""

import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
# The script under test; PROV_ABSENT_CHECK points the suite at a scratch copy.
SCRIPT = os.environ.get("PROV_ABSENT_CHECK", os.path.join(HERE, "prov-absent-check.py"))
spec = importlib.util.spec_from_file_location("prov_absent_check", SCRIPT)
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


class ProvDefs(unittest.TestCase):
    def test_every_define_naming_the_feature_is_caught_whatever_its_value(self):
        defs = ["-DTFW_FEATURE_PROVISION=1", "-DX=1", "-DTFW_FEATURE_PROVISION",
                "-DTFW_FEATURE_PROVISION=0", "-DTFW_FEATURE_ALIAS_CERT=1"]
        self.assertEqual(pac.prov_defs(defs), ["-DTFW_FEATURE_PROVISION", "-DTFW_FEATURE_PROVISION=0",
                                               "-DTFW_FEATURE_PROVISION=1"])

    def test_other_defines_are_not_caught(self):
        self.assertEqual(pac.prov_defs([]), [])
        self.assertEqual(pac.prov_defs(["-DTFW_FEATURE_ALIAS_CERT=1", "-DX=1"]), [])

    def test_a_production_image_with_a_bare_define_fails(self):
        image = prod(defs=["-DTFW_FEATURE_PROVISION"])
        fails = pac.check([image, jig()])
        self.assertEqual(len(fails), 1, fails)
        self.assertTrue(fails[0].endswith("a production image compiles with -DTFW_FEATURE_PROVISION"), fails)


class BakeObjects(unittest.TestCase):
    def test_hits_are_sorted_duplicates_kept_case_sensitive_architecture_needed(self):
        srcs = ["tools/platform-bake/bake.c", "src/hal/esp32/hal_bus.c", "src/hal/hal_bus.c",
                "src/crypto/soft/x.c", "src/crypto/top.c", "src/crypto/deviceid_soft.c",
                "src/core/atecc608.c", "src/caps/Provision.c", "src/caps/provision.c",
                "src/caps/provision.c", "src/hal/avr/crypto_hw.c", "src/hal/avr/prov_uds.c"]
        self.assertEqual(pac.bake_objects(srcs), ["src/caps/provision.c", "src/caps/provision.c",
                                                  "src/core/atecc608.c", "src/crypto/deviceid_soft.c",
                                                  "src/crypto/soft/x.c", "src/hal/avr/crypto_hw.c",
                                                  "src/hal/avr/prov_uds.c", "src/hal/esp32/hal_bus.c"])


if __name__ == "__main__":
    unittest.main()
