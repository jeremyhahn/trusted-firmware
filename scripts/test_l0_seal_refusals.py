# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for boot/hal/esp32/l0_seal.py's refusals (make scripts-test).

envelope() has the board's suit-emit write the manifest and its
Sig_structure, signs that, has suit-emit wrap the two, and refuses an
envelope outside layer 0's 1 to 320 bytes or one that does not carry the
image's SHA-256; a suit-emit step that fails is named with its output.
main() refuses a board id of 0 or past one byte and an image version of 0
or past 32 bits, with exit 1, before any key is loaded. The script is the
stub-loaded module test_l0_seal.py loads, and suit-emit is a stand-in sh
script. No ESP-IDF, no cryptography, no network.
"""

import contextlib
import hashlib
import io
import os
import stat
import tempfile
import unittest
from unittest import mock

from test_l0_seal import l0_seal

# Writes the files l0_seal.py asks suit-emit for: the manifest and tbs on
# "tbs", and on "envelope" keeps the sig it was given and writes $STUB_DIR/env.
SUIT_EMIT = """#!/bin/sh
if [ "$1" = "$STUB_FAIL" ]; then echo "stub: $1 broke"; exit 3; fi
case "$1" in
tbs) cp "$2" "$STUB_DIR/image"; printf %s "$3" > "$STUB_DIR/version"
     printf man > "$4"; printf to-be-signed > "$5" ;;
envelope) cp "$3" "$STUB_DIR/sig"; cp "$STUB_DIR/env" "$4" ;;
*) exit 2 ;;
esac
"""

IMAGE = b"\xA5" * 100
R, S = 0x0102, 0xFEDC


def stub_dir(test):
    tmp = tempfile.TemporaryDirectory(prefix="tfw-l0-seal-")
    test.addCleanup(tmp.cleanup)
    return tmp.name


class Envelope(unittest.TestCase):
    def setUp(self):
        self.dir = stub_dir(self)
        self.suit_emit = os.path.join(self.dir, "suit-emit")
        with open(self.suit_emit, "w") as f:
            f.write(SUIT_EMIT)
        os.chmod(self.suit_emit, stat.S_IRWXU)
        self.key = mock.Mock()
        self.key.sign.return_value = b"der"
        for name, new in (("decode_dss_signature", mock.Mock(return_value=(R, S))),
                          ("ec", mock.Mock()), ("hashes", mock.Mock())):
            p = mock.patch.object(l0_seal, name, new)
            p.start()
            self.addCleanup(p.stop)
        p = mock.patch.dict(os.environ, {"STUB_DIR": self.dir, "STUB_FAIL": ""})
        p.start()
        self.addCleanup(p.stop)

    def run_envelope(self, env):
        with open(os.path.join(self.dir, "env"), "wb") as f:
            f.write(env)
        return l0_seal.envelope(IMAGE, 7, self.key, self.suit_emit)

    def named(self, n, image=IMAGE):
        """An n-byte envelope carrying SHA-256(image) at its end."""
        return b"\xD8" * (n - 32) + hashlib.sha256(image).digest()

    def read(self, name):
        with open(os.path.join(self.dir, name), "rb") as f:
            return f.read()

    def test_an_envelope_naming_the_image_is_returned(self):
        env = self.named(200)
        self.assertEqual(self.run_envelope(env), env)
        self.assertEqual(self.read("image"), IMAGE)
        self.assertEqual(self.read("version"), b"7")
        self.assertEqual(self.key.sign.call_args.args[0], b"to-be-signed")
        l0_seal.decode_dss_signature.assert_called_once_with(b"der")
        self.assertEqual(self.read("sig"), R.to_bytes(32, "big") + S.to_bytes(32, "big"))

    def test_an_empty_or_over_320_byte_envelope_is_refused(self):
        self.assertEqual(l0_seal.ENVELOPE_MAX, 320)
        self.assertEqual(self.run_envelope(self.named(320)), self.named(320))
        for env in (b"", self.named(321)):
            with self.subTest(n=len(env)):
                with self.assertRaises(l0_seal.SealError) as e:
                    self.run_envelope(env)
                self.assertEqual(str(e.exception),
                                 f"a {len(env)}-byte envelope: layer 0 reads 1 to 320")

    def test_an_envelope_that_does_not_name_the_image_is_refused(self):
        with self.assertRaises(l0_seal.SealError) as e:
            self.run_envelope(self.named(200, image=IMAGE + b"\x00"))
        self.assertIn("does not name the image", str(e.exception))

    def test_a_failed_suit_emit_step_is_named_with_its_output(self):
        for step in ("tbs", "envelope"):
            with self.subTest(step=step):
                os.environ["STUB_FAIL"] = step
                with self.assertRaises(l0_seal.SealError) as e:
                    self.run_envelope(self.named(200))
                self.assertEqual(str(e.exception),
                                 f"suit-emit {step} failed:\nstub: {step} broke\n")
        self.assertEqual(self.key.sign.call_count, 1)


class Main(unittest.TestCase):
    def setUp(self):
        self.dir = stub_dir(self)
        self.input = os.path.join(self.dir, "in.bin")
        with open(self.input, "wb") as f:
            f.write(IMAGE)
        for name in ("load_p256", "seal"):
            p = mock.patch.object(l0_seal, name,
                                  side_effect=AssertionError(f"{name} reached"))
            p.start()
            self.addCleanup(p.stop)

    def main(self, board, version):
        profile = os.path.join(self.dir, "board_profile.h")
        with open(profile, "w") as f:
            f.write(f"#define TFW_BOARD_ID {board}\n")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            rc = l0_seal.main(["seal", self.input, os.path.join(self.dir, "out.bin"),
                               "--board-profile", profile, "--version", version,
                               "--image-key", "image.pem", "--sign-key", "rsa.pem",
                               "--suit-emit", "suit-emit"])
        return rc, err.getvalue()

    def test_a_board_id_of_0_or_past_one_byte_is_refused(self):
        for board, shown in (("0", "0x0"), ("0x100u", "0x100")):
            with self.subTest(board=board):
                self.assertEqual(self.main(board, "1"), (
                    1, f"l0_seal: board id {shown}: 0 is unbound, and it is one byte\n"))

    def test_an_image_version_of_0_or_past_32_bits_is_refused(self):
        for version in ("0", "4294967296"):
            with self.subTest(version=version):
                self.assertEqual(self.main("0xFF", version), (
                    1, f"l0_seal: image version {version}: 0 is refused, "
                       "and it is 32 bits\n"))
        l0_seal.load_p256.assert_not_called()

    def test_the_last_board_id_and_version_in_range_reach_the_key(self):
        with self.assertRaisesRegex(AssertionError, "load_p256 reached"):
            self.main("0xFF", "4294967295")


if __name__ == "__main__":
    unittest.main()
