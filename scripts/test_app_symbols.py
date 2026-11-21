# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests for app_symbols.py, the ELF symbol reader of app-firmware-check.

The images are built byte by byte here, so no toolchain is needed: a symbol
defined in an allocated section is found, a gc-dropped one is not (exit 1),
a compiler '.lto_priv' suffix matches, and a non-ELF file is refused (exit 2).
One case links a real host image with --gc-sections where gcc is installed.
"""
import contextlib
import importlib.util
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "app_symbols.py")
_spec = importlib.util.spec_from_file_location("app_symbols", SCRIPT)
app_symbols = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(app_symbols)

# Section indices of the images build_elf writes.
TEXT = 1      # .text, SHF_ALLOC: the image loads it
COMMENT = 2   # .comment, no SHF_ALLOC: what a discarded definition is left in
SHN_UNDEF = 0
SHN_ABS = 0xFFF1


def build_elf(symbols, wide=False, big=False):
    """An ELF holding symbols, a list of (name, section index)."""
    end = ">" if big else "<"
    strtab = b"\0"
    entries = [(0, SHN_UNDEF)]
    for name, shndx in symbols:
        entries.append((len(strtab), shndx))
        strtab += name.encode("ascii") + b"\0"
    if wide:
        ehsize, shentsize, sym_size = 64, 64, 24
        sym = [struct.pack(end + "IBBHQQ", n, 0x12, 0, s, 0x1000, 4)
               for n, s in entries]
    else:
        ehsize, shentsize, sym_size = 52, 40, 16
        sym = [struct.pack(end + "IIIBBH", n, 0x1000, 4, 0x12, 0, s)
               for n, s in entries]
    symtab = b"".join(sym)
    text, comment = b"\0" * 4, b"gcc\0"
    off_text = ehsize
    off_comment = off_text + len(text)
    off_symtab = off_comment + len(comment)
    off_strtab = off_symtab + len(symtab)
    shoff = off_strtab + len(strtab)
    # (name, type, flags, addr, offset, size, link, info, align, entsize)
    secs = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (0, 1, 0x6, 0x1000, off_text, len(text), 0, 0, 4, 0),
        (0, 1, 0x0, 0, off_comment, len(comment), 0, 0, 1, 0),
        (0, 2, 0x0, 0, off_symtab, len(symtab), 4, 1, 4, sym_size),
        (0, 3, 0x0, 0, off_strtab, len(strtab), 0, 0, 1, 0),
    ]
    sec_fmt = end + ("IIQQQQIIQQ" if wide else "IIIIIIIIII")
    ident = b"\x7fELF" + bytes([2 if wide else 1, 2 if big else 1, 1]) + b"\0" * 9
    hdr_fmt = end + ("HHIQQQIHHHHHH" if wide else "HHIIIIIHHHHHH")
    header = ident + struct.pack(hdr_fmt, 2, 0x28, 1, 0x1000, 0, shoff, 0,
                                 ehsize, 0, 0, shentsize, len(secs), 0)
    body = text + comment + symtab + strtab
    return header + body + b"".join(struct.pack(sec_fmt, *s) for s in secs)


class AppSymbolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)

    def write(self, data, name="app.elf"):
        path = os.path.join(self.tmp, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def run_main(self, path, regex):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = app_symbols.main(["app_symbols.py", path, regex])
        return rc, out.getvalue().split(), err.getvalue()

    def test_linked_symbol_found(self):
        path = self.write(build_elf([("tfw_app_init", TEXT), ("main", TEXT)]))
        rc, names, _ = self.run_main(path, "tfw_app_.*")
        self.assertEqual(rc, 0)
        self.assertEqual(names, ["tfw_app_init"])

    def test_gc_dropped_symbol_exit_1(self):
        # --gc-sections removed the application: its symbol is gone, or left
        # only in a section the image does not load, or undefined.
        path = self.write(build_elf([
            ("main", TEXT),
            ("tfw_app_left", COMMENT),
            ("tfw_app_undef", SHN_UNDEF),
            ("tfw_app_abs", SHN_ABS),
        ]))
        rc, names, _ = self.run_main(path, "tfw_app_.*")
        self.assertEqual(rc, 1)
        self.assertEqual(names, [])

    def test_lto_priv_suffix_matched(self):
        path = self.write(build_elf([
            ("tfw_app_init.lto_priv.0", TEXT),
            ("tfw_app_init_extra", TEXT),
        ]))
        rc, names, _ = self.run_main(path, "tfw_app_init")
        self.assertEqual(rc, 0)
        self.assertEqual(names, ["tfw_app_init.lto_priv.0"])

    def test_elf64_big_endian_read(self):
        path = self.write(build_elf([("tfw_app_tick", TEXT),
                                     ("tfw_app_left", COMMENT)],
                                    wide=True, big=True))
        rc, names, _ = self.run_main(path, "tfw_app_.*")
        self.assertEqual(rc, 0)
        self.assertEqual(names, ["tfw_app_tick"])

    def test_non_elf_refused(self):
        path = self.write(b"MZ\x90\0 not an ELF image" + b"\0" * 64, "app.bin")
        proc = subprocess.run([sys.executable, "-I", SCRIPT, path, "tfw_app_.*"],
                              capture_output=True, text=True, check=False)
        self.assertEqual(proc.returncode, 2)
        self.assertEqual(proc.stdout, "")
        self.assertIn("not an ELF file", proc.stderr)

    def test_usage_exit_2(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            rc = app_symbols.main(["app_symbols.py", "only-one-arg"])
        self.assertEqual(rc, 2)
        self.assertIn("usage", err.getvalue())

    @unittest.skipUnless(shutil.which("gcc"), "gcc not installed")
    def test_real_gc_sections_link(self):
        src = os.path.join(self.tmp, "app.c")
        elf = os.path.join(self.tmp, "app")
        with open(src, "w") as f:
            f.write("void tfw_app_kept(void) {}\n"
                    "void tfw_app_dropped(void) {}\n"
                    "int main(void) { tfw_app_kept(); return 0; }\n")
        subprocess.run(["gcc", "-O0", "-ffunction-sections",
                        "-Wl,--gc-sections", src, "-o", elf], check=True)
        rc, names, _ = self.run_main(elf, "tfw_app_kept")
        self.assertEqual((rc, names), (0, ["tfw_app_kept"]))
        rc, names, _ = self.run_main(elf, "tfw_app_dropped")
        self.assertEqual((rc, names), (1, []))


if __name__ == "__main__":
    unittest.main()
