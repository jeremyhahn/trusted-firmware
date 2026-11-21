# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for boot/hal/esp32/l0_seal.py's image helpers (make scripts-test).

mk/arch/esp32-targets.mk's ESP_L0_SEAL puts trailer v1 and the SUIT envelope
on the ESP32 app image, inside the ESP-IDF build only. These tests pin the
three helpers that need nothing but the standard library: layout walks the
ESP-IDF image to its last segment and its appended SHA-256, unsigned_image
grows the zero pad segment until the SHA-256 ends 16 bytes past a 4 KB
boundary, and board_id reads TFW_BOARD_ID from the board profile. The script
imports cryptography, which the toolchain image does not carry, so it is
loaded with stub modules for those imports; the stubs leave sys.modules when
the load ends. TFW_L0_SEAL_SCRIPT names another copy of the script, for a
mutation run only. No ESP-IDF, no cryptography, no network.
"""

import hashlib
import importlib.util
import os
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("TFW_L0_SEAL_SCRIPT",
                        os.path.join(REPO, "boot", "hal", "esp32", "l0_seal.py"))


def _stubs():
    m = {n: types.ModuleType(n) for n in (
        "cryptography", "cryptography.hazmat", "cryptography.hazmat.primitives",
        "cryptography.hazmat.primitives.hashes",
        "cryptography.hazmat.primitives.serialization",
        "cryptography.hazmat.primitives.asymmetric",
        "cryptography.hazmat.primitives.asymmetric.ec",
        "cryptography.hazmat.primitives.asymmetric.utils")}
    p = "cryptography.hazmat.primitives"
    m[p].hashes = m[p + ".hashes"]
    m[p].serialization = m[p + ".serialization"]
    m[p + ".asymmetric"].ec = m[p + ".asymmetric.ec"]
    m[p + ".asymmetric.utils"].decode_dss_signature = lambda sig: (0, 0)
    return m


with mock.patch.dict(sys.modules, _stubs()):
    _spec = importlib.util.spec_from_file_location("l0_seal", SCRIPT)
    l0_seal = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(l0_seal)

CODE = 0x40000000


def image(*segments, count=None, magic=0xE9, hashed=1):
    """An ESP-IDF image: the 24-byte header, each (addr, data) segment, zeros
    to the 16-byte boundary the checksum byte ends, then the SHA-256 of all
    of it. Returns (image, offset of the last segment header, digest offset)."""
    head = bytearray(24)
    head[0] = magic
    head[1] = len(segments) if count is None else count
    head[23] = hashed
    img, last = bytearray(head), None
    for addr, data in segments:
        last = len(img)
        img += struct.pack("<II", addr, len(data)) + data
    digest = (len(img) + 16) & ~15
    img += bytes(digest - len(img))
    return bytes(img + hashlib.sha256(img).digest()), last, digest


class Layout(unittest.TestCase):
    def test_the_digest_follows_the_checksum_byte_on_a_16_byte_boundary(self):
        # 24 + 8 + 8 + 8 = 48: segments that end 16-aligned put the checksum
        # byte at 48 and the SHA-256 at 64; ending at 63, the SHA-256 is at 64.
        for data, pos, want in ((b"", 48, 64), (bytes(15), 63, 64)):
            with self.subTest(pos=pos):
                img, last, _ = image((CODE, b"\x01" * 8), (0, data))
                self.assertEqual(last, 40)
                self.assertEqual(l0_seal.layout(img), (40, want))
                self.assertEqual(want, pos + 16 if pos % 16 == 0 else pos + 1)

    def test_an_image_it_cannot_walk_is_refused(self):
        good, _, _ = image((CODE, bytes(8)))
        two = bytearray(24)
        two[0], two[1], two[23] = 0xE9, 2, 1
        two += struct.pack("<II", CODE, 1000) + bytes(16)
        cases = (
            ("not an ESP-IDF image", image((CODE, bytes(8)), magic=0xE8)[0]),
            ("not an ESP-IDF image", b"\xE9" + bytes(9)),
            ("no appended SHA-256", image((CODE, bytes(8)), hashed=0)[0]),
            ("no segments", image(count=0)[0]),
            ("runs past the image", bytes(two)),
            ("ends before its appended SHA-256", good[:-32]),
        )
        for want, img in cases:
            with self.subTest(want=want, n=len(img)):
                with self.assertRaises(l0_seal.SealError) as e:
                    l0_seal.layout(img)
                self.assertIn(want, str(e.exception))


class UnsignedImage(unittest.TestCase):
    def test_the_zero_pad_grows_until_the_sha256_ends_16_past_4kb(self):
        # 24 + 8 + 100 + 8 + 52 = 192: 16-aligned, so the SHA-256 is at 208.
        img, pad, digest = image((CODE, b"\xA5" * 100), (0, bytes(52)))
        self.assertEqual(digest, 208)
        out, got = l0_seal.unsigned_image(img)
        self.assertEqual((got + 32) % 4096, 16)
        self.assertEqual(len(out), got + 32)
        grow = got - digest
        self.assertGreater(grow, 0)
        self.assertEqual(struct.unpack_from("<II", out, pad), (0, 52 + grow))
        self.assertEqual(out[:pad + 4], img[:pad + 4])
        self.assertFalse(any(out[pad + 8:got]))
        self.assertEqual(out[-32:], hashlib.sha256(out[:-32]).digest())
        self.assertEqual(l0_seal.layout(out), (pad, got))

    def test_an_image_already_16_past_a_boundary_is_returned_as_it_was(self):
        # 24 + 8 + 16 + 8 + 4008 = 4064, so the SHA-256 is at 4080 and ends at 4112.
        img, _, digest = image((CODE, b"\x5A" * 16), (0, bytes(4008)))
        self.assertEqual(digest, 4080)
        self.assertEqual(l0_seal.unsigned_image(img), (img, 4080))

    def test_a_sha256_that_does_not_match_is_refused(self):
        img, _, _ = image((CODE, b"\xA5" * 100), (0, bytes(40)))
        bad = bytearray(img)
        bad[40] ^= 1
        with self.assertRaises(l0_seal.SealError) as e:
            l0_seal.unsigned_image(bytes(bad))
        self.assertIn("does not match", str(e.exception))

    def test_a_last_segment_that_is_not_the_zero_pad_is_refused(self):
        for what, segs in (("code", ((CODE, bytes(40)),)),
                           ("non-zero", ((CODE, b"\xA5" * 100), (0, bytes(39) + b"\x01")))):
            with self.subTest(what):
                img, _, _ = image(*segs)
                with self.assertRaises(l0_seal.SealError) as e:
                    l0_seal.unsigned_image(img)
                self.assertIn("no room for the trailer", str(e.exception))


class BoardId(unittest.TestCase):
    def profile(self, text):
        tmp = tempfile.TemporaryDirectory(prefix="tfw-l0-seal-")
        self.addCleanup(tmp.cleanup)
        path = os.path.join(tmp.name, "board_profile.h")
        with open(path, "w") as f:
            f.write(text)
        return path

    def test_the_profile_s_id_is_read_in_hex_or_decimal(self):
        for text, want in (("#define TFW_BOARD_ID 0x2Au\n", 42),
                           ("/* board */\n#define TFW_BOARD_ID 7\n", 7)):
            with self.subTest(text=text):
                self.assertEqual(l0_seal.board_id(self.profile(text)), want)

    def test_a_profile_with_no_tfw_board_id_is_refused(self):
        for text in ("#define TFW_BOARD_ID_MAX 9\n", "// #define TFW_BOARD_ID 3\n"):
            with self.subTest(text=text):
                with self.assertRaises(l0_seal.SealError) as e:
                    l0_seal.board_id(self.profile(text))
                self.assertIn("no TFW_BOARD_ID", str(e.exception))


if __name__ == "__main__":
    unittest.main()
