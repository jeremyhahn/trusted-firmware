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
int main(void) { return kept(0); }
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


if __name__ == "__main__":
    unittest.main()
