# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for test/integration/esp32/qemu_test.py's pure helpers (make scripts-test).

Every verdict `make esp32-qemu-test` gives rests on a few helpers that otherwise
only run inside that QEMU job: the DeviceID point and CDI fingerprint the app
must log, the ESP-IDF partition table, the QEMU eFuse file's key blocks and the
boot log's lines. The harness is loaded with ESP-IDF's and esptool's modules
stubbed, so no ESP-IDF, esptool or QEMU is needed; the class skips without
`cryptography`, which the harness imports itself.

ESP32_QEMU_TEST_SCRIPT names another copy of the harness to load instead, for
a scratch mutation run.
"""

import hashlib
import hmac
import importlib.util
import os
import re
import struct
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("ESP32_QEMU_TEST_SCRIPT") or os.path.join(
    REPO, "test", "integration", "esp32", "qemu_test.py")
SEED_TEST = os.path.join(REPO, "test", "unit", "test_deviceid_p256.c")

try:
    # Imported here, before sys.modules is patched, so the patch's restore
    # does not drop them: the Rust bindings initialise once per process.
    from cryptography.hazmat.primitives import serialization  # noqa: F401
    from cryptography.hazmat.primitives.asymmetric import ec  # noqa: F401
    HAVE_CRYPTOGRAPHY = True
except ImportError:
    HAVE_CRYPTOGRAPHY = False


def load_harness(idf_path):
    """The harness as a module, its ESP-IDF, esptool and l0_seal imports stubbed."""
    qemu_ext = types.ModuleType("idf_py_actions.qemu_ext")
    qemu_ext.QEMU_TARGETS = {}
    bin_image = types.ModuleType("esptool.bin_image")
    bin_image.LoadFirmwareImage = None
    stubs = {
        "idf_py_actions": types.ModuleType("idf_py_actions"),
        "idf_py_actions.qemu_ext": qemu_ext,
        "esptool": types.ModuleType("esptool"),
        "esptool.bin_image": bin_image,
        "l0_seal": types.ModuleType("l0_seal"),
    }
    spec = importlib.util.spec_from_file_location("esp32_qemu_test", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    with mock.patch.dict(os.environ, {"IDF_PATH": idf_path}), \
            mock.patch.dict(sys.modules, stubs), \
            mock.patch.object(sys, "path", list(sys.path)):
        spec.loader.exec_module(mod)
    return mod


def seed_pub():
    """SEED_PUB's 64 bytes, as test/unit/test_deviceid_p256.c spells them."""
    text = Path(SEED_TEST).read_text()
    body = re.search(r"SEED_PUB\[64\] = \{([^}]*)\}", text).group(1)
    return bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", body))


def entry(label, off, size):
    """One 32-byte ESP-IDF partition table entry: an app, factory subtype."""
    return struct.pack("<2sBBII16sI", b"\xaa\x50", 0, 0, off, size, label, 0)


@unittest.skipUnless(HAVE_CRYPTOGRAPHY, "the harness needs cryptography")
class TestEsp32QemuHelpers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.h = load_harness(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def efuse(self, words):
        path = Path(self.tmp.name) / "efuse.bin"
        path.write_bytes(struct.pack("<84I", *words))
        return self.h.efuse_keys(path)

    # (1) the DeviceID point is the C test's literal
    def test_deviceid_point_is_the_c_tests_seed_pub(self):
        want = seed_pub()
        self.assertEqual(len(want), 64)
        self.assertEqual(self.h.UDS, bytes(range(32)))
        self.assertEqual(self.h.DEVICEID_PUB, want)
        self.assertEqual(self.h.deviceid_point(self.h.UDS), want)
        self.assertEqual(self.h.deviceid_fingerprint(want),
                         hashlib.sha256(want).hexdigest()[:32])

    # (2) the CDI fingerprint is HMAC(HMAC(UDS, m), label)
    def test_cdi_fingerprint_is_the_hmac_of_the_cdi(self):
        uds = self.h.UDS
        got = []
        for m in (bytes(32), bytes(range(1, 33))):
            cdi = hmac.new(uds, m, hashlib.sha256).digest()
            want = hmac.new(cdi, b"tfw-cdi-fingerprint-v1", hashlib.sha256).hexdigest()[:32]
            self.assertEqual(self.h.cdi_fingerprint(uds, m), want)
            got.append(want)
        self.assertNotEqual(got[0], got[1])

    # (3) the partition table stops at the first non-entry
    def test_partitions_stop_at_the_padding(self):
        table = (entry(b"factory", 0x10000, 0x100000)
                 + entry(b"ota_0", 0x110000, 0x80000)
                 + b"\xff" * 32
                 + entry(b"late", 0x200000, 0x1000)
                 + b"\xff" * 64)
        self.assertEqual(self.h.partitions(table), {
            "factory": (0x10000, 0x100000),
            "ota_0": (0x110000, 0x80000),
        })

    # (4) eFuse key words, one bit or nibble at a time
    def test_efuse_keys_of_a_blank_file(self):
        for key in self.efuse([0] * 84):
            self.assertEqual(key, (0, False, False, False, bytes(32)))

    def test_efuse_key_purposes_land_on_their_keys(self):
        # KEY_PURPOSE_0/1 are word 2's top nibbles, 2..5 word 3's low ones
        places = [(2, 24), (2, 28), (3, 0), (3, 4), (3, 8), (3, 12)]
        for n, (word, shift) in enumerate(places):
            w = [0] * 84
            w[word] = (n + 9) << shift
            got = [k[0] for k in self.efuse(w)]
            self.assertEqual(got, [n + 9 if i == n else 0 for i in range(6)], f"KEY_PURPOSE_{n}")

    def test_efuse_rd_dis_bit_n_read_protects_key_n_only(self):
        for n in range(6):
            w = [0] * 84
            w[1] = 1 << n
            got = [k[1] for k in self.efuse(w)]
            self.assertEqual(got, [i == n for i in range(6)], f"RD_DIS bit {n}")

    def test_efuse_wr_dis_bits_map_to_key_n(self):
        for n in range(6):
            w = [0] * 84
            w[0] = 1 << (23 + n)
            got = [(k[2], k[3]) for k in self.efuse(w)]
            self.assertEqual(got, [(i == n, False) for i in range(6)], f"WR_DIS bit {23 + n}")
            w[0] = 1 << (8 + n)
            got = [(k[2], k[3]) for k in self.efuse(w)]
            self.assertEqual(got, [(False, i == n) for i in range(6)], f"WR_DIS bit {8 + n}")

    def test_efuse_key_bytes_come_from_words_28_plus_8n(self):
        w = [0] * 84
        for i in range(28, 76):
            w[i] = 0x01010101 * (i - 27)
        for n, key in enumerate(self.efuse(w)):
            self.assertEqual(key[4], struct.pack("<8I", *w[28 + 8 * n:36 + 8 * n]), f"key {n}")
            self.assertEqual(key[4][0], 8 * n + 1)
        self.assertEqual(w[76:], [0] * 8)

    # (5) the boot log's lines, in boot order
    def test_boot_facts_of_two_boots(self):
        m1, m2 = "a" * 64, "b" * 64
        log = "\n".join([
            f"tfw-l0: app at 0x10000 verified, measurement {m1}",
            "tfw-id: cdi hmac-fingerprint=" + "1" * 32,
            "tfw-id: deviceid p256 sha256(x||y)=" + "2" * 32 + " from eFuse BLOCK4",
            "tfw-eth: link up, address 192.0.2.15, wire on tcp/21574",
            "tfw-l0: app at 0x110000 verified, measurement " + "c" * 63,
            f"tfw-l0: app at 0x110000 verified, measurement {m2}",
            "tfw-id: cdi hmac-fingerprint=" + "3" * 32,
            "tfw-id: deviceid p256 sha256(x||y)=" + "4" * 32 + " from eFuse BLOCK5",
            "tfw-eth: link up, address 192.0.2.16, wire on tcp/21575",
        ]) + "\n"
        self.assertEqual(self.h.boot_facts(log), {
            "measurement": [m1, m2],
            "cdi": ["1" * 32, "3" * 32],
            "deviceid": ["2" * 32, "4" * 32],
            "wire": ["21574", "21575"],
        })


if __name__ == "__main__":
    unittest.main()
