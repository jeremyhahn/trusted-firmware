#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Name the symbols of an ELF image that match a pattern and are in it.

app-firmware-check (mk/app.mk) asks whether a linked image holds one of the
application's own symbols. A linker map answers the wrong question: it lists
the input sections --gc-sections discarded too, so .text.tfw_app_init is in
the map of an image the application was dropped from. This reads the ELF's
symbol table instead and counts a symbol only where it is defined in a
section the image loads (SHF_ALLOC): a discarded one has no such section.

Usage: app_symbols.py ELF REGEX
Prints each matching symbol and exits 0, or exits 1 when none matches, and
exits 2 when the ELF cannot be read or parsed; a name matches when REGEX
matches it whole, or up to a '.' suffix the compiler adds
(tfw_app_init.lto_priv.0). Standard library only: it runs on every
architecture's ELF, the ESP-IDF image's included, with no toolchain nm.
"""
import re
import struct
import sys

SHT_SYMTAB = 2
SHF_ALLOC = 0x2
SHN_LORESERVE = 0xFF00


def defined_symbols(data):
    """The names of the symbols defined in an allocated section of data."""
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    wide = {1: False, 2: True}[data[4]]
    end = {1: "<", 2: ">"}[data[5]]
    if wide:
        shoff, = struct.unpack_from(end + "Q", data, 0x28)
        shentsize, shnum = struct.unpack_from(end + "HH", data, 0x3A)
        sec_fmt, sym_fmt, sym_size = end + "IIQQQQIIQQ", end + "IBBHQQ", 24
    else:
        shoff, = struct.unpack_from(end + "I", data, 0x20)
        shentsize, shnum = struct.unpack_from(end + "HH", data, 0x2E)
        sec_fmt, sym_fmt, sym_size = end + "IIIIIIIIII", end + "IIIBBH", 16
    sections = [struct.unpack_from(sec_fmt, data, shoff + i * shentsize)
                for i in range(shnum)]
    # (name, type, flags, addr, offset, size, link, info, align, entsize)
    names = []
    for sec in sections:
        if sec[1] != SHT_SYMTAB:
            continue
        strtab = sections[sec[6]]
        for off in range(sec[4], sec[4] + sec[5], sym_size):
            sym = struct.unpack_from(sym_fmt, data, off)
            name_off, shndx = sym[0], (sym[3] if wide else sym[5])
            if shndx == 0 or shndx >= SHN_LORESERVE or shndx >= len(sections):
                continue
            if not sections[shndx][2] & SHF_ALLOC:
                continue
            start = strtab[4] + name_off
            names.append(data[start:data.index(b"\0", start)].decode("ascii", "replace"))
    return names


def main(argv):
    if len(argv) != 3:
        print("usage: app_symbols.py ELF REGEX", file=sys.stderr)
        return 2
    try:
        with open(argv[1], "rb") as f:
            data = f.read()
        names = defined_symbols(data)
    except (OSError, ValueError, KeyError, IndexError, struct.error) as e:
        print("app_symbols.py: %s: %s" % (argv[1], e), file=sys.stderr)
        return 2
    pattern = re.compile("(?:%s)(?:\\..*)?" % argv[2])
    found = sorted({n for n in names if pattern.fullmatch(n)})
    for n in found:
        print(n)
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
