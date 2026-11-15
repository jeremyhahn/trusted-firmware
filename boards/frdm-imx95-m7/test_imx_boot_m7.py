#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Host test of imx-boot-m7.py on a synthetic ELF and boot container.

make host-test runs it (mk/host.mk, test_imx_boot_m7). The container is
built here with the layout the script reads (a header at 0x8000, 128-byte
entries, SHA-384 per image), holding one other image and the M7 entry, so
the test can check that only the M7 slot and its entry's size and hash
change. The ELF is a minimal 32-bit Arm ELF with one PT_LOAD segment.
"""

import hashlib
import importlib.util
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location('imx_boot_m7', os.path.join(HERE, 'imx-boot-m7.py'))
mk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mk)

BASE = mk.BOOT_CONTAINER
OTHER_OFF, OTHER_SIZE = 0x400, 0x400
M7_OFF, M7_SIZE = 0x800, 0x1000
SP = mk.DTCM_M7 + mk.DTCM_SIZE


def flags(core):
    return (mk.HASH_SHA384 << 8) | (core << 4) | 3


def container():
    """An image set whose boot container holds a 2-core image and the M7's."""
    img = bytearray(os.urandom(BASE + M7_OFF + M7_SIZE + 0x400))
    count = 2
    struct.pack_into('<BHB', img, BASE, 0, mk.HDR_LEN + count * mk.ENTRY_LEN, mk.TAG_CONTAINER)
    struct.pack_into('<IHBBH', img, BASE + 4, 0, 0, 0, count, 0)
    for i, (off, size, load, core) in enumerate(((OTHER_OFF, OTHER_SIZE, 0x1FFC0000, 0x2),
                                                 (M7_OFF, M7_SIZE, mk.M7_LOAD, mk.M7_CORE))):
        at = BASE + mk.HDR_LEN + i * mk.ENTRY_LEN
        struct.pack_into('<IIQQII', img, at, off, size, load, load, flags(core), 0)
        data = img[BASE + off:BASE + off + size]
        img[at + 32:at + 96] = hashlib.sha384(data).digest().ljust(64, b'\0')
    return bytes(img)


def elf(lo=mk.ITCM_M7, size=0x600, sp=SP, reset=None, machine=40):
    """A 32-bit little-endian ELF loading `size` bytes at `lo`, its vector
    table first: the stack, then the reset target (Thumb, inside by default)."""
    if reset is None:
        reset = lo + 0x41
    body = bytearray(os.urandom(size))
    struct.pack_into('<II', body, 0, sp, reset)
    ehdr = bytearray(52)
    ehdr[:6] = b'\x7fELF\x01\x01'
    struct.pack_into('<HH', ehdr, 16, 2, machine)
    struct.pack_into('<I', ehdr, 28, 52)                       # e_phoff
    struct.pack_into('<HH', ehdr, 42, 32, 1)                   # e_phentsize, e_phnum
    phdr = struct.pack('<IIIIIIII', mk.PT_LOAD, 84, lo, lo, size, size, 5, 4)
    return bytes(ehdr + phdr + body), bytes(body)


def m7_at(img):
    return BASE + mk.HDR_LEN + mk.ENTRY_LEN


class Accepts(unittest.TestCase):
    def test_replaces_the_m7_slot_and_keeps_every_other_byte(self):
        orig = container()
        e, body = elf()
        new = mk.build(orig, e)
        self.assertEqual(len(new), len(orig))
        at = m7_at(orig)
        start = BASE + M7_OFF
        padded = body + bytes(-len(body) % mk.ALIGN)
        self.assertEqual(new[start:start + len(padded)], padded)
        self.assertEqual(new[start + len(padded):start + M7_SIZE], bytes(M7_SIZE - len(padded)))
        self.assertEqual(struct.unpack_from('<I', new, at + 4)[0], len(padded))
        self.assertEqual(new[at + 32:at + 80], hashlib.sha384(padded).digest())
        changed = set(range(start, start + M7_SIZE)) | set(range(at + 4, at + 8)) | \
            set(range(at + 32, at + 96))
        for i in range(len(orig)):
            if i not in changed and orig[i] != new[i]:
                self.fail('byte 0x%x changed outside the M7 slot and entry' % i)
        mk.check_hashes(new, BASE, mk.entries(new, BASE)[4])

    def test_the_cli_writes_the_container(self):
        e, _ = elf()
        with tempfile.TemporaryDirectory() as d:
            paths = [os.path.join(d, n) for n in ('orig.bin', 'm7.elf', 'new.bin')]
            for p, data in zip(paths, (container(), e)):
                with open(p, 'wb') as f:
                    f.write(data)
            with open(os.devnull, 'w') as null:
                out, sys.stdout = sys.stdout, null
                try:
                    self.assertEqual(mk.main(['imx-boot-m7.py'] + paths), 0)
                    self.assertEqual(mk.main(['imx-boot-m7.py', '--show', paths[2]]), 0)
                finally:
                    sys.stdout = out


class Refuses(unittest.TestCase):
    def refused(self, orig, e, words):
        with self.assertRaises(SystemExit) as cm:
            mk.build(orig, e)
        self.assertIn(words, str(cm.exception.code))

    def test_the_default_link_address(self):
        e, _ = elf(lo=0x8000)
        self.refused(container(), e, 'IMX_BOOT=1')

    def test_a_reset_target_past_the_image(self):
        e, _ = elf(reset=0x600 + 1)
        self.refused(container(), e, 'reset target')

    def test_a_reset_target_inside_the_vector(self):
        e, _ = elf(reset=0x4 + 1)
        self.refused(container(), e, 'reset target')

    def test_an_arm_mode_reset_target(self):
        e, _ = elf(reset=0x40)
        self.refused(container(), e, 'reset target')

    def test_a_stack_outside_the_dtcm(self):
        e, _ = elf(sp=0x30000000)
        self.refused(container(), e, 'initial SP')

    def test_an_image_larger_than_the_slot(self):
        e, _ = elf(size=M7_SIZE + 4)
        self.refused(container(), e, 'exceeds the M7 slot')

    def test_an_image_outside_the_itcm(self):
        e, _ = elf(lo=mk.ITCM_SIZE - 0x100)
        self.refused(container(), e, 'not inside the ITCM')

    def test_an_elf_for_another_machine(self):
        e, _ = elf(machine=62)
        self.refused(container(), e, 'not an Arm ELF')

    def test_a_container_whose_hash_does_not_match(self):
        orig = bytearray(container())
        orig[BASE + OTHER_OFF] ^= 0xFF
        e, _ = elf()
        self.refused(bytes(orig), e, 'SHA-384 does not match')

    def test_a_container_with_no_m7_entry(self):
        orig = bytearray(container())
        at = m7_at(orig)
        struct.pack_into('<I', orig, at + 24, flags(0x2))
        e, _ = elf()
        self.refused(bytes(orig), e, 'expected one M7')


if __name__ == '__main__':
    unittest.main()
