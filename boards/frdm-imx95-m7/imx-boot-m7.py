#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Put our Cortex-M7 image into an FRDM-IMX95-PRO boot container (imx-boot).

Ruling 644 (1), row #428: the System Manager boots the M7 in its logical
machine (LM1) from the M7 image of the boot container, so loading our image
means a boot container that carries it. This script does what imx-mkimage's
`-m7 <image> 0 0x303C0000` would, without rebuilding the rest: it reads the
board's own imx-boot (the backup of the eMMC user area from 32 KiB, see
docs/development/bench/frdm-imx95.md), finds the one M7 entry of the boot
container (the second container of the image set, at 0x8000), and replaces
that entry's data, size and SHA-384 with ours. Every other byte (the ELE
firmware container, the OEI and its DDR training data, the System Manager and
its mSel 0 configuration, U-Boot SPL, the TF-A/U-Boot/OP-TEE container) is
kept as it was, so the System Manager starts LM1 exactly as before, on our
image.

The container stays unsigned: its signature block holds no signature, as the
board's own does (OEM Open, no SRK fused). Nothing here signs, encrypts or
touches a fuse.

Usage:
    imx-boot-m7.py <imx-boot-orig.bin> <m7.bin> <imx-boot-new.bin>
    imx-boot-m7.py --show <imx-boot.bin>

<m7.bin> is `arm-none-eabi-objcopy -O binary` of the frdm-imx95-m7 ELF built
with APP_BASE=0x00000000: the SM starts the M7 at vector 0 (ITCM base), where
the board's map reserves tfw-boot, which is not built for this board yet.
"""

import hashlib
import struct
import sys

TAG_CONTAINER = 0x87
HDR_LEN = 16
ENTRY_LEN = 128
BOOT_CONTAINER = 0x8000      # second container of the image set (the first is the ELE's)
M7_LOAD = 0x303C0000         # the M7 ITCM base, as the SM and the ROM see it
M7_CORE = 0xB                # image flags bits 7:4 for the vendor's M7 entry
HASH_SHA384 = 1              # image flags bits 10:8
ALIGN = 0x400                # every image of the board's container is a multiple of this


def entries(img, base):
    ver, length, tag = struct.unpack_from('<BHB', img, base)
    if tag != TAG_CONTAINER:
        raise SystemExit('no container header at 0x%x' % base)
    _flags, _sw, _fuse, count, sig = struct.unpack_from('<IHBBH', img, base + 4)
    out = []
    for i in range(count):
        at = base + HDR_LEN + i * ENTRY_LEN
        off, size, load, entry, flags, meta = struct.unpack_from('<IIQQII', img, at)
        out.append(dict(at=at, off=off, size=size, load=load, entry=entry,
                        flags=flags, meta=meta, hash=img[at + 32:at + 96]))
    return ver, length, count, sig, out


def check_hashes(img, base, ents):
    for i, e in enumerate(ents):
        if (e['flags'] >> 8) & 7 != HASH_SHA384:
            raise SystemExit('entry %d: hash type is not SHA-384' % i)
        data = img[base + e['off']:base + e['off'] + e['size']]
        if not e['hash'].startswith(hashlib.sha384(data).digest()):
            raise SystemExit('entry %d: SHA-384 does not match its data' % i)


def show(img):
    ver, length, count, sig, ents = entries(img, BOOT_CONTAINER)
    print('boot container at 0x%x: version %d, length 0x%x, %d images, signature block 0x%x'
          % (BOOT_CONTAINER, ver, length, count, sig))
    for i, e in enumerate(ents):
        print('  %d: offset 0x%06x size 0x%06x load 0x%08x entry 0x%08x flags 0x%04x meta 0x%x'
              % (i, e['off'], e['size'], e['load'], e['entry'], e['flags'], e['meta']))
    check_hashes(img, BOOT_CONTAINER, ents)
    print('every image SHA-384 matches')


def m7_entry(ents):
    found = [e for e in ents if (e['flags'] >> 4) & 0xF == M7_CORE and e['flags'] & 0xF == 3]
    if len(found) != 1 or found[0]['load'] != M7_LOAD:
        raise SystemExit('expected one M7 executable entry loading at 0x%x' % M7_LOAD)
    return found[0]


def build(orig, m7):
    img = bytearray(orig)
    _ver, _length, _count, _sig, ents = entries(img, BOOT_CONTAINER)
    check_hashes(img, BOOT_CONTAINER, ents)
    e = m7_entry(ents)
    sp, reset = struct.unpack_from('<II', m7, 0)
    if reset & 1 == 0 or reset >= 0x40000 or not 0x20000000 <= sp <= 0x20040000:
        raise SystemExit('m7.bin does not open with a vector table linked at ITCM 0')
    data = m7 + bytes(-len(m7) % ALIGN)
    if len(data) > e['size']:
        raise SystemExit('our image (0x%x) exceeds the M7 slot (0x%x)' % (len(data), e['size']))
    start = BOOT_CONTAINER + e['off']
    # The slot keeps its place: ours, then zeros to the vendor image's end.
    img[start:start + e['size']] = data + bytes(e['size'] - len(data))
    struct.pack_into('<I', img, e['at'] + 4, len(data))
    img[e['at'] + 32:e['at'] + 96] = hashlib.sha384(data).digest().ljust(64, b'\0')
    _v, _l, _c, _s, after = entries(img, BOOT_CONTAINER)
    check_hashes(img, BOOT_CONTAINER, after)
    return bytes(img)


def main(argv):
    if len(argv) == 3 and argv[1] == '--show':
        with open(argv[2], 'rb') as f:
            show(f.read())
        return 0
    if len(argv) != 4:
        raise SystemExit(__doc__)
    with open(argv[1], 'rb') as f:
        orig = f.read()
    with open(argv[2], 'rb') as f:
        m7 = f.read()
    new = build(orig, m7)
    changed = [i for i in range(len(orig)) if orig[i] != new[i]]
    with open(argv[3], 'wb') as f:
        f.write(new)
    print('wrote %s: %d bytes, changed bytes 0x%x to 0x%x only'
          % (argv[3], len(new), changed[0], changed[-1]))
    print('sha256 %s' % hashlib.sha256(new).hexdigest())
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
