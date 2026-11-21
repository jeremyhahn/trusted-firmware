# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# app_symbols.py, the check app-firmware-check (mk/app.mk) runs on a linked
# image: a symbol the link kept is found, one --gc-sections dropped is not.
# The image is built here by the host cc, so the test needs no cross toolchain.

import contextlib
import importlib.util
import io
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

_spec = importlib.util.spec_from_file_location(
    "app_symbols", os.path.join(os.path.dirname(os.path.abspath(__file__)), "app_symbols.py"))
app_symbols = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(app_symbols)

_CC = shutil.which("cc")

# kept() is called from main and stays; dropped() is called from nowhere, so
# --gc-sections discards its section and its symbol has no loaded section.
_SOURCE = """
int kept(int x) { return x + 1; }
int dropped(int x) { return x * 3; }
/* The name a compiler pass gives a clone (tfw_app_init.lto_priv.0), set by
   hand so the test does not depend on which pass the host cc runs. */
int suffixed(int x) __asm__("suffixed.lto_priv.0");
int suffixed(int x) { return x - 1; }
int main(void) { return kept(0) + suffixed(1); }
"""


def _run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        rc = app_symbols.main(list(argv))
    return rc, out.getvalue().split()


@unittest.skipIf(_CC is None, "no host cc to link a test image")
class LinkedImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._dir = tempfile.TemporaryDirectory()
        src = os.path.join(cls._dir.name, "app.c")
        with open(src, "w") as f:
            f.write(_SOURCE)
        cls.elf = os.path.join(cls._dir.name, "app.elf")
        subprocess.run([_CC, "-O0", "-ffunction-sections", "-Wl,--gc-sections",
                        "-o", cls.elf, src], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls._dir.cleanup()

    def test_kept_symbol_is_found(self):
        self.assertEqual(_run(["app_symbols.py", self.elf, "kept"]), (0, ["kept"]))

    def test_dropped_symbol_exits_1(self):
        # The symbol is in the object, so a linker map would still name its
        # section; the image has no loaded section for it.
        self.assertEqual(_run(["app_symbols.py", self.elf, "dropped"]), (1, []))

    def test_compiler_suffix_still_matches(self):
        self.assertEqual(_run(["app_symbols.py", self.elf, "suffixed"]),
                         (0, ["suffixed.lto_priv.0"]))

    def test_regex_matches_the_whole_name(self):
        self.assertEqual(_run(["app_symbols.py", self.elf, "kep"]), (1, []))


def _elf(wide, big):
    """A minimal ELF: a loaded .text holding 'loaded', a not-loaded .note
    holding 'unloaded', and an undefined 'undef'. No program, only the
    header fields and tables app_symbols.py reads."""
    end = ">" if big else "<"
    ehsize = 64 if wide else 52
    strtab = b"\0loaded\0unloaded\0undef\0.text\0.note\0"
    # (name offset, section index): 0 is undefined, 1 .text, 2 .note
    syms = [(0, 0), (1, 1), (8, 2), (17, 0)]
    if wide:
        symtab = b"".join(struct.pack(end + "IBBHQQ", n, 0x12, 0, s, 0, 0) for n, s in syms)
        sec_fmt, sym_size = end + "IIQQQQIIQQ", 24
    else:
        symtab = b"".join(struct.pack(end + "IIIBBH", n, 0, 0, 0x12, 0, s) for n, s in syms)
        sec_fmt, sym_size = end + "IIIIIIIIII", 16
    str_off = ehsize
    sym_off = str_off + len(strtab)
    shoff = sym_off + len(symtab)
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (23, 1, app_symbols.SHF_ALLOC | 0x4, 0, 0, 0, 0, 0, 1, 0),  # .text
        (29, 7, 0, 0, 0, 0, 0, 0, 1, 0),  # .note, not SHF_ALLOC
        (0, 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0),  # .strtab
        (0, app_symbols.SHT_SYMTAB, 0, 0, sym_off, len(symtab), 3, 1, 8, sym_size),
    ]
    shentsize = struct.calcsize(sec_fmt)
    header = bytearray(ehsize)
    header[:6] = b"\x7fELF" + bytes([2 if wide else 1, 2 if big else 1])
    if wide:
        struct.pack_into(end + "Q", header, 0x28, shoff)
        struct.pack_into(end + "HH", header, 0x3A, shentsize, len(sections))
    else:
        struct.pack_into(end + "I", header, 0x20, shoff)
        struct.pack_into(end + "HH", header, 0x2E, shentsize, len(sections))
    return bytes(header) + strtab + symtab + b"".join(struct.pack(sec_fmt, *s) for s in sections)


class HandBuiltHeaders(unittest.TestCase):
    """The host cc links only 64-bit little-endian; the targets' images are
    32-bit (Cortex-M, ESP32) and app_symbols.py reads big-endian too."""

    def test_every_class_and_byte_order(self):
        for wide in (False, True):
            for big in (False, True):
                with self.subTest(wide=wide, big=big):
                    self.assertEqual(app_symbols.defined_symbols(_elf(wide, big)), ["loaded"])


class NotElf(unittest.TestCase):
    def test_not_elf_raises_value_error(self):
        with self.assertRaises(ValueError):
            app_symbols.defined_symbols(b"MZ\x90\x00" + bytes(60))

    def test_not_elf_exits_2(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "app.bin")
            with open(path, "wb") as f:
                f.write(b"not an image")
            self.assertEqual(_run(["app_symbols.py", path, "kept"]), (2, []))


if __name__ == "__main__":
    unittest.main()
