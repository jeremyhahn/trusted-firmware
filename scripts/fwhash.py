#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The expected firmware hash, computed from a build the way tfw-boot measures
# the application on every reset (boot/measure/measure_soft.c,
# tfw_boot_measure_from): SHA-256 over [base, base + image_len), the
# application region as the part reads it once the image is programmed.
#
# base is the board's: TFW_APP_REGION_BASE, which the make variable FWHASH_BASE
# carries (mk/release.mk). On AVR the application starts at address 0; on the
# Cortex-M board it is the slot tfw-boot reads by address
# (boot/hal/cortex-m/boot_hal_cortex-m.c, tfw_boot_flash_read). --size bounds
# the region: a HEX that names a byte outside [base, base + size) is not an
# application image for this board.
#
# The input is the Intel HEX the build writes and a programmer flashes. A byte
# the HEX does not name is erased flash, 0xFF, which is what the device reads
# there; `objcopy -O binary` fills a gap with 0x00 instead, so a hash of that
# file would only match while the image has no gap.
#
# image_len is the trailer's. Without --image-len it is one past the highest
# byte the HEX names, less base: the span a signing tool records for an
# unsigned image. With it, bytes past image_len (a trailer, for one) are not
# measured, and a span longer than the image is measured over erased flash, as
# the device does.
#
#   python3 scripts/fwhash.py build/atmega2560/soft/trusted-firmware.hex
#   python3 scripts/fwhash.py --base 0x38000000 --size 131072 build/mps2-an505/soft/trusted-firmware.hex
#   <measurement, 64 hex digits>  <image_len>  <file>

import argparse
import hashlib
import sys

# Past any application region this project's parts have: a region larger is
# malformed, and refusing it bounds the buffer a bad file can make us allocate.
MAX_FLASH = 16 * 1024 * 1024

DATA, EOF, EXT_SEGMENT, START_SEGMENT, EXT_LINEAR, START_LINEAR = range(6)


class HexError(ValueError):
    pass


def check_region(base, size):
    if base < 0 or size <= 0 or size > MAX_FLASH or base + size > 1 << 32:
        raise HexError(f"region 0x{base:X} + {size} is not an application region")


def parse_hex(text, base=0, size=MAX_FLASH):
    """Return the HEX's data as a sorted list of (offset from base, bytes), each
    record checked: its checksum, its length and its type. Two records that name
    the same byte are refused, because which one the programmer writes last is
    the programmer's choice and the measurement would depend on it. A record
    outside [base, base + size) is refused: it is not this board's application
    region, so the device never measures it."""
    check_region(base, size)
    ext = 0
    chunks = []
    ended = False
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if ended:
            raise HexError(f"line {lineno}: a record after the end-of-file record")
        if not line.startswith(":"):
            raise HexError(f"line {lineno}: not an Intel HEX record")
        try:
            rec = bytes.fromhex(line[1:])
        except ValueError:
            raise HexError(f"line {lineno}: not hexadecimal") from None
        if len(rec) < 5 or len(rec) != 5 + rec[0]:
            raise HexError(f"line {lineno}: the record length does not match its count")
        if sum(rec) & 0xFF:
            raise HexError(f"line {lineno}: bad checksum")
        count, addr, rtype, data = rec[0], (rec[1] << 8) | rec[2], rec[3], rec[4:-1]
        if rtype == DATA:
            if count:
                chunks.append((ext + addr, data))
        elif rtype == EOF:
            ended = True
        elif rtype in (EXT_SEGMENT, EXT_LINEAR):
            if count != 2:
                raise HexError(f"line {lineno}: an address record carries two bytes")
            value = (data[0] << 8) | data[1]
            ext = value << 4 if rtype == EXT_SEGMENT else value << 16
        elif rtype in (START_SEGMENT, START_LINEAR):
            pass  # an entry point, not flash content
        else:
            raise HexError(f"line {lineno}: unknown record type {rtype:02X}")
    if not ended:
        raise HexError("no end-of-file record: the file is truncated")
    chunks.sort(key=lambda c: c[0])
    for (a0, d0), (a1, _) in zip(chunks, chunks[1:]):
        if a0 + len(d0) > a1:
            raise HexError(f"two records name the byte at 0x{a1:X}")
    if chunks and chunks[0][0] < base:
        raise HexError(f"a record names the byte at 0x{chunks[0][0]:X}, below the"
                       f" application region at 0x{base:X}")
    if chunks and chunks[-1][0] + len(chunks[-1][1]) > base + size:
        raise HexError(f"a record names a byte past the {size}-byte application"
                       f" region at 0x{base:X}")
    return [(addr - base, data) for addr, data in chunks]


def flash_image(chunks, image_len=None, size=MAX_FLASH):
    """The region [base, base + image_len) as the part reads it: the HEX's
    bytes, 0xFF elsewhere. chunks are parse_hex's, offsets from base."""
    end = chunks[-1][0] + len(chunks[-1][1]) if chunks else 0
    n = end if image_len is None else image_len
    if n <= 0:
        raise HexError("image_len is zero: tfw-boot refuses an empty image")
    if n > size:
        raise HexError(f"image_len {n} is past the {size}-byte application region")
    flash = bytearray(b"\xff" * max(n, end))
    for addr, data in chunks:
        flash[addr:addr + len(data)] = data
    return bytes(flash[:n])


def measure(text, image_len=None, base=0, size=MAX_FLASH):
    """(measurement, image_len) of an Intel HEX, as tfw_boot_measure computes it."""
    image = flash_image(parse_hex(text, base, size), image_len, size)
    return hashlib.sha256(image).digest(), len(image)


def main(argv=None):
    ap = argparse.ArgumentParser(description="The measurement tfw-boot takes of an application image.")
    ap.add_argument("hexfile", help="the application's Intel HEX, as built")
    ap.add_argument("--image-len", type=lambda s: int(s, 0), default=None,
                    help="the trailer's image_len; default one past the image's last byte")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0,
                    help="the application region's first address (TFW_APP_REGION_BASE); default 0")
    ap.add_argument("--size", type=lambda s: int(s, 0), default=MAX_FLASH,
                    help="the application region's length in bytes; a record past it is refused")
    ap.add_argument("--write-image", metavar="FILE", default=None,
                    help="also write the measured bytes, [base, base + image_len), to FILE")
    args = ap.parse_args(argv)
    try:
        with open(args.hexfile, encoding="ascii") as f:
            image = flash_image(parse_hex(f.read(), args.base, args.size), args.image_len, args.size)
        digest, n = hashlib.sha256(image).digest(), len(image)
        if args.write_image is not None:
            with open(args.write_image, "wb") as f:
                f.write(image)
    except (OSError, UnicodeDecodeError, HexError) as e:
        print(f"fwhash: {args.hexfile}: {e}", file=sys.stderr)
        return 1
    print(f"{digest.hex()}  {n}  {args.hexfile}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
