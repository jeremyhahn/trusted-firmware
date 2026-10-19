#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Trailer v1 on an ESP-IDF app image, for the ESP32 layer 0's ES256 gate.

    l0_seal.py keyslot <image-key.pem> <p256_key_slot.c>
        Writes boot/verify/p256_key_slot.c's slot with the P-256 image key in
        it, for the bootloader build, and prints the key's pin: SHA-256 over
        X || Y, what tfwprov --bake-eeprom puts in tfw_plat.

    l0_seal.py seal <in.bin> <out.bin> --board-profile <board_profile.h>
                    --version V --image-key <p256.pem> --sign-key <rsa3072.pem>
                    --suit-emit <suit-emit>
        Takes the image ESP-IDF built and signed, and writes it again with
        trailer v1 and the SUIT envelope after the appended SHA-256, re-signed
        for secure boot v2. The board id is the profile's TFW_BOARD_ID, the one
        the bootloader was compiled with, so the two cannot be given apart; the
        envelope is what the board's own suit-emit builds, so its vendor and
        class ids are the ones the bootloader compares against.

The layout is docs/multi-arch/esp32-p4-design.md section 2.2:

    | ESP-IDF image | SHA-256 | trailer v1 | u16 len | SUIT envelope | 0xFF to 4 KB | signature sector |
    '------- image_len ------'

The envelope's manifest names the image's SHA-256 and image_len, and the
trailer's image_version as its sequence number; its COSE_Sign1 is ES256 with
the image key, over the Sig_structure suit-emit writes. The length is
little-endian.

image_len is the image without its appended SHA-256 and the measurement is that
SHA-256, so nothing is hashed twice: esp_image_verify checks the digest against
the image, and the gate checks the trailer against the digest. The trailer lies
in the span espsecure pads to the 4 KB boundary, which secure boot v2's digest
covers, so the RSA signature covers the trailer and the envelope too.

Room: ESP-IDF builds a signed app with --secure-pad-v2, which pads it with a
zero segment so the SHA-256 ends exactly on a 64 KB boundary -- where the
bootloader then looks for the signature sector, leaving no byte between the two.
The zero segment is grown here until the SHA-256 ends 16 bytes past a 4 KB
boundary: 4080 bytes to the signature sector, for the trailer's 128 and the SUIT
envelope. Zeros leave the XOR checksum as it is, and a multiple of
16 leaves its alignment; the SHA-256 is recomputed. An image whose last segment
is not that zero segment is refused, never sealed some other way.

Run under ESP-IDF's Python, which carries cryptography and espsecure.py.
"""

import argparse
import hashlib
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

# include/tfw/boot.h
TRAILER_MAGIC = 0x4D494654
TRAILER_VERSION = 1
TRAILER_LEN = 128
TR_SIGNED_LEN = 48
# src/core/suit.h
ENVELOPE_MAX = 320

# ESP-IDF's image format: esp_app_format/include/esp_app_format.h
ESP_MAGIC = 0xE9
HEADER_LEN = 24                # esp_image_header_t
HASH_APPENDED = 23             # its hash_appended byte
SEGMENT_HEADER_LEN = 8         # esp_image_segment_header_t
SECTOR = 4096
ROOM_END = 16                  # where the SHA-256 ends past a 4 KB boundary


class SealError(Exception):
    pass


def layout(img):
    """(offset of the last segment's header, offset of the appended SHA-256)."""
    if len(img) < HEADER_LEN or img[0] != ESP_MAGIC:
        raise SealError('not an ESP-IDF image')
    if img[HASH_APPENDED] != 1:
        raise SealError('the image has no appended SHA-256 to bind the trailer to')
    pos, last = HEADER_LEN, None
    for _ in range(img[1]):
        if pos + SEGMENT_HEADER_LEN > len(img):
            raise SealError('a segment header runs past the image')
        last = pos
        _, n = struct.unpack_from('<II', img, pos)
        pos += SEGMENT_HEADER_LEN + n
    if last is None:
        raise SealError('the image has no segments')
    # The checksum byte ends the image on a 16-byte boundary.
    digest = (pos + 16) & ~15
    if digest + 32 > len(img):
        raise SealError('the image ends before its appended SHA-256')
    return last, digest


def unsigned_image(img):
    """The image up to and including its SHA-256, grown to leave the room."""
    last, digest = layout(img)
    img = bytearray(img[:digest + 32])
    if hashlib.sha256(img[:digest]).digest() != bytes(img[digest:]):
        raise SealError('the appended SHA-256 does not match the image')
    grow = (ROOM_END - (digest + 32)) % SECTOR
    if grow:
        addr, n = struct.unpack_from('<II', img, last)
        data = last + SEGMENT_HEADER_LEN
        if addr != 0 or any(img[data:data + n]):
            raise SealError('the last segment is not the zero pad --secure-pad-v2 adds: '
                            'no room for the trailer')
        struct.pack_into('<I', img, last + 4, n + grow)
        img[data + n:data + n] = bytes(grow)
        digest += grow
        img[digest:] = hashlib.sha256(img[:digest]).digest()
    return bytes(img), digest


def load_p256(path):
    key = serialization.load_pem_private_key(Path(path).read_bytes(), None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != 'secp256r1':
        raise SealError(f'{path}: not a P-256 private key')
    return key


def public_xy(key):
    n = key.public_key().public_numbers()
    return n.x.to_bytes(32, 'big') + n.y.to_bytes(32, 'big')


def trailer(board_id, version, image_len, measurement, key):
    """Trailer v1, signed ES256 over its first 48 bytes; r || s big-endian."""
    signed = struct.pack('<IHBBII', TRAILER_MAGIC, TRAILER_VERSION, board_id, 0,
                         version, image_len) + measurement
    assert len(signed) == TR_SIGNED_LEN
    r, s = decode_dss_signature(key.sign(signed, ec.ECDSA(hashes.SHA256())))
    raw = signed + r.to_bytes(32, 'big') + s.to_bytes(32, 'big')
    return raw.ljust(TRAILER_LEN, b'\0')


def run_suit_emit(suit_emit, *args):
    r = subprocess.run([str(suit_emit)] + [str(a) for a in args],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0:
        raise SealError(f'suit-emit {args[0]} failed:\n{r.stdout}')


def envelope(image, version, key, suit_emit):
    """The board's SUIT envelope for `image`, the bytes image_len covers:
    suit-emit writes the manifest and its Sig_structure, the image key signs
    that ES256, and suit-emit wraps the two."""
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t / 'image.bin').write_bytes(image)
        run_suit_emit(suit_emit, 'tbs', t / 'image.bin', version, t / 'man', t / 'tbs')
        r, s = decode_dss_signature(key.sign((t / 'tbs').read_bytes(),
                                             ec.ECDSA(hashes.SHA256())))
        (t / 'sig').write_bytes(r.to_bytes(32, 'big') + s.to_bytes(32, 'big'))
        run_suit_emit(suit_emit, 'envelope', t / 'man', t / 'sig', t / 'env')
        env = (t / 'env').read_bytes()
    if not 0 < len(env) <= ENVELOPE_MAX:
        raise SealError(f'a {len(env)}-byte envelope: layer 0 reads 1 to {ENVELOPE_MAX}')
    if hashlib.sha256(image).digest() not in env:
        raise SealError('the envelope does not name the image it is sealed with')
    return env


def rsa_sign(unsigned, sign_key, out):
    """espsecure.py pads to 4 KB with 0xFF and appends the signature sector."""
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'unsigned.bin'
        src.write_bytes(unsigned)
        r = subprocess.run(['espsecure.py', 'sign_data', '--version', '2', '--keyfile',
                            str(sign_key), '--output', str(out), str(src)],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode != 0:
            raise SealError(f'espsecure.py sign_data failed:\n{r.stdout}')


def seal(img, board_id, version, image_key, sign_key, out, suit_emit, tamper=None,
         tamper_envelope=None):
    """Seal `img` into `out`. `tamper` and `tamper_envelope`, for the QEMU
    harness only, edit the trailer and the envelope bytes before the RSA
    signature is made over them."""
    unsigned, digest = unsigned_image(img)
    raw = bytearray(trailer(board_id, version, digest, unsigned[digest:], image_key))
    env = bytearray(envelope(unsigned[:digest], version, image_key, suit_emit))
    if tamper is not None:
        tamper(raw)
    if tamper_envelope is not None:
        tamper_envelope(env)
    room = bytes(raw) + struct.pack('<H', len(env)) + bytes(env)
    assert len(room) <= SECTOR - ROOM_END
    rsa_sign(unsigned + room, sign_key, out)
    return digest


def keyslot(pem, out):
    xy = public_xy(load_p256(pem))
    body = ',\n'.join('    ' + ', '.join(f'0x{b:02X}u' for b in xy[i:i + 8])
                      for i in range(0, 64, 8))
    Path(out).write_text(
        '/* Generated by boot/hal/esp32/l0_seal.py from the build\'s image key.\n'
        ' * Not committed: boot/verify/p256_key_slot.c is the source. */\n'
        '#include <stdint.h>\n\n'
        'const uint8_t tfw_boot_p256_key[64] = {\n' + body + '\n};\n')
    return hashlib.sha256(xy).hexdigest()


def board_id(profile):
    m = re.search(r'^#define\s+TFW_BOARD_ID\s+(0x[0-9A-Fa-f]+|\d+)u?\b',
                  Path(profile).read_text(), re.M)
    if m is None:
        raise SealError(f'{profile}: no TFW_BOARD_ID')
    return int(m.group(1), 0)


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest='cmd', required=True)
    k = sub.add_parser('keyslot')
    k.add_argument('pem')
    k.add_argument('out')
    s = sub.add_parser('seal')
    s.add_argument('input')
    s.add_argument('output')
    s.add_argument('--board-profile', required=True)
    s.add_argument('--version', type=int, required=True)
    s.add_argument('--image-key', required=True)
    s.add_argument('--sign-key', required=True)
    s.add_argument('--suit-emit', required=True)
    a = p.parse_args(argv)
    try:
        if a.cmd == 'keyslot':
            print(keyslot(a.pem, a.out))
            return 0
        board = board_id(a.board_profile)
        if not 0 < board < 0x100:
            raise SealError(f'board id {board:#x}: 0 is unbound, and it is one byte')
        if not 0 < a.version < 1 << 32:
            raise SealError(f'image version {a.version}: 0 is refused, and it is 32 bits')
        n = seal(Path(a.input).read_bytes(), board, a.version,
                 load_p256(a.image_key), a.sign_key, a.output, a.suit_emit)
        print(f'sealed {a.output}: board {board:#04x}, version {a.version}, image_len {n}')
        return 0
    except SealError as e:
        print(f'l0_seal: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
