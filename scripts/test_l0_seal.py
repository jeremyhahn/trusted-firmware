# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for boot/hal/esp32/l0_seal.py's image helpers and the formats it writes (make scripts-test).

mk/arch/esp32-targets.mk's ESP_L0_SEAL puts trailer v1 and the SUIT envelope
on the ESP32 app image, inside the ESP-IDF build only. These tests pin the
three helpers that need nothing but the standard library: layout walks the
ESP-IDF image to its last segment and its appended SHA-256, unsigned_image
grows the zero pad segment until the SHA-256 ends 16 bytes past a 4 KB
boundary, and board_id reads TFW_BOARD_ID from the board profile. They also
pin the bytes layer 0 checks: trailer writes trailer v1's 128 bytes at
include/tfw/boot.h's offsets with its ES256 over the first 48, keyslot writes
X || Y into boot/verify/p256_key_slot.c's layout and returns its SHA-256 pin,
and seal hands the RSA signer the image, the trailer, the u16 envelope length
and the envelope, after any tamper. The script imports cryptography, which
the toolchain image does not carry, so it is loaded with stub modules for
those imports; the stubs leave sys.modules when the load ends, and the tests
stand in for the keys and patch the module's cryptography names.
TFW_L0_SEAL_SCRIPT names another copy of the script, for a mutation run only.
No ESP-IDF, no cryptography, no network.
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


class FakeKey:
    """A stand-in P-256 key: sign() records what it was given, and the public
    numbers are the x and y it was built with."""

    def __init__(self, x=0, y=0):
        self.signed = []
        self.x, self.y = x, y

    def sign(self, data, algorithm):
        self.signed.append(bytes(data))
        return b"der"

    def public_key(self):
        return self

    def public_numbers(self):
        return types.SimpleNamespace(x=self.x, y=self.y)


# Distinct bytes in every position, so a swapped or reversed half shows.
R = int.from_bytes(bytes(range(1, 33)), "big")
S = int.from_bytes(bytes(range(0x81, 0xA1)), "big")


def signing(test):
    """Patch the module's cryptography names: the DER signature decodes to
    (R, S), and ec and hashes are mocks."""
    for name, new in (("decode_dss_signature", lambda der: (R, S)),
                      ("ec", mock.MagicMock()), ("hashes", mock.MagicMock())):
        p = mock.patch.object(l0_seal, name, new)
        p.start()
        test.addCleanup(p.stop)


class Trailer(unittest.TestCase):
    MEASUREMENT = bytes(range(0x40, 0x60))

    def setUp(self):
        signing(self)
        self.key = FakeKey()
        self.raw = l0_seal.trailer(0x2A, 0x01020304, 0x0A0B0C0D, self.MEASUREMENT, self.key)

    def test_the_fields_sit_at_boot_h_s_offsets_little_endian(self):
        # include/tfw/boot.h: magic 0, trailer_version 4, board_id 6,
        # image_flags 7, image_version 8, image_len 12, measurement 16,
        # signature 48 (r || s), reserved 112, 128 in all.
        raw = self.raw
        self.assertEqual(len(raw), 128)
        self.assertEqual(raw[:4], b"TFIM")
        self.assertEqual(struct.unpack("<IHBBII", raw[:16]),
                         (0x4D494654, 1, 0x2A, 0, 0x01020304, 0x0A0B0C0D))
        self.assertEqual(raw[16:48], self.MEASUREMENT)
        self.assertEqual(raw[48:80], bytes(range(1, 33)))
        self.assertEqual(raw[80:112], bytes(range(0x81, 0xA1)))
        self.assertEqual(raw[112:128], bytes(16))

    def test_the_signature_covers_exactly_the_first_48_bytes(self):
        self.assertEqual(self.key.signed, [self.raw[:48]])


class Keyslot(unittest.TestCase):
    def test_the_slot_holds_x_then_y_and_the_pin_is_their_sha256(self):
        xy = bytes(range(0xE0, 0x100)) + bytes(range(0x00, 0x40, 2))
        self.assertEqual(len(xy), 64)
        key = FakeKey(int.from_bytes(xy[:32], "big"), int.from_bytes(xy[32:], "big"))
        tmp = tempfile.TemporaryDirectory(prefix="tfw-l0-seal-")
        self.addCleanup(tmp.cleanup)
        out = os.path.join(tmp.name, "p256_key_slot.c")
        with mock.patch.object(l0_seal, "load_p256", return_value=key) as load:
            pin = l0_seal.keyslot("image-key.pem", out)
        load.assert_called_once_with("image-key.pem")
        self.assertEqual(pin, hashlib.sha256(xy).hexdigest())
        with open(out) as f:
            text = f.read()
        head, sep, rest = text.partition("const uint8_t tfw_boot_p256_key[64] = {\n")
        self.assertTrue(sep, text)
        self.assertIn("#include <stdint.h>", head)
        body, sep, tail = rest.partition("\n};\n")
        self.assertTrue(sep, text)
        self.assertEqual(tail, "")
        lines = body.split("\n")
        self.assertEqual(len(lines), 8)
        got = []
        for i, line in enumerate(lines):
            vals = line.strip().rstrip(",").split(", ")
            self.assertEqual(len(vals), 8, line)
            self.assertTrue(line.startswith("    0x"), line)
            self.assertEqual(line.endswith(","), i < 7, line)
            for v in vals:
                self.assertRegex(v, r"^0x[0-9A-F]{2}u$")
                got.append(int(v[:-1], 16))
        self.assertEqual(bytes(got), xy)


class Seal(unittest.TestCase):
    ENV = b"\xD8\x6B" + bytes(range(1, 0x5F))

    def setUp(self):
        signing(self)
        self.img, _, _ = image((CODE, b"\xA5" * 100), (0, bytes(52)))
        self.unsigned, self.digest = l0_seal.unsigned_image(self.img)
        self.key = FakeKey()
        self.envelope = mock.patch.object(l0_seal, "envelope", return_value=self.ENV).start()
        self.rsa = mock.patch.object(l0_seal, "rsa_sign").start()
        self.addCleanup(mock.patch.stopall)

    def seal(self, **kw):
        got = l0_seal.seal(self.img, 0x2A, 7, self.key, "rsa.pem", "out.bin", "suit-emit", **kw)
        self.assertEqual(got, self.digest)
        self.envelope.assert_called_once_with(self.unsigned[:self.digest], 7, self.key, "suit-emit")
        self.rsa.assert_called_once()
        data, sign_key, out = self.rsa.call_args.args
        self.assertEqual((sign_key, out), ("rsa.pem", "out.bin"))
        return data

    def test_the_rsa_signer_gets_the_image_trailer_u16_length_and_envelope(self):
        data = self.seal()
        tr = l0_seal.trailer(0x2A, 7, self.digest, self.unsigned[self.digest:], FakeKey())
        self.assertEqual(data, self.unsigned + tr + struct.pack("<H", len(self.ENV)) + self.ENV)
        self.assertEqual(struct.unpack_from("<II", tr, 8), (7, self.digest))
        self.assertEqual(tr[16:48], hashlib.sha256(self.unsigned[:self.digest]).digest())

    def test_a_tamper_is_seen_in_the_bytes_the_rsa_signer_gets(self):
        def flip(raw):
            raw[6] ^= 0xFF

        def grow(env):
            env += b"\x00"

        clean = self.seal()
        self.rsa.reset_mock()
        self.envelope.reset_mock()
        data = self.seal(tamper=flip, tamper_envelope=grow)
        n = len(self.unsigned)
        self.assertEqual(data[:n], self.unsigned)
        self.assertEqual(data[n + 6], 0x2A ^ 0xFF)
        self.assertEqual(data[n:n + 6] + data[n + 7:n + 128], clean[n:n + 6] + clean[n + 7:n + 128])
        self.assertEqual(data[n + 128:], struct.pack("<H", len(self.ENV) + 1) + self.ENV + b"\x00")


if __name__ == "__main__":
    unittest.main()
