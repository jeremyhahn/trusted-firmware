# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for test/integration/esp32/qemu_test.py's Qemu.wait_for (make scripts-test).

A plain Python subprocess stands in for QEMU: wait_for only reads the log the
process writes and waits on the process, so no ESP-IDF, QEMU or board is
needed. The case that matters is a line printed just before the process
exits, after wait_for's last read of the log: the wait must still find it.
"""

import importlib.util
import os
import sys
import tempfile
import types
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("ESP32_QEMU_TEST_SCRIPT") or os.path.join(
    REPO, "test", "integration", "esp32", "qemu_test.py")

try:
    # Imported here, before sys.modules is patched, so the patch's restore
    # does not drop them: the Rust bindings initialise once per process.
    from cryptography.hazmat.primitives import serialization  # noqa: F401
    from cryptography.hazmat.primitives.asymmetric import ec  # noqa: F401
    HAVE_CRYPTOGRAPHY = True
except ImportError:
    HAVE_CRYPTOGRAPHY = False


def load_harness(idf_path):
    """The harness as a module, its ESP-IDF, esptool and l0_seal imports stubbed."""
    qemu_ext = types.ModuleType("idf_py_actions.qemu_ext")
    qemu_ext.QEMU_TARGETS = {}
    bin_image = types.ModuleType("esptool.bin_image")
    bin_image.LoadFirmwareImage = None
    stubs = {
        "idf_py_actions": types.ModuleType("idf_py_actions"),
        "idf_py_actions.qemu_ext": qemu_ext,
        "esptool": types.ModuleType("esptool"),
        "esptool.bin_image": bin_image,
        "l0_seal": types.ModuleType("l0_seal"),
    }
    spec = importlib.util.spec_from_file_location("esp32_qemu_test", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    with mock.patch.dict(os.environ, {"IDF_PATH": idf_path}), \
            mock.patch.dict(sys.modules, stubs), \
            mock.patch.object(sys, "path", list(sys.path)):
        spec.loader.exec_module(mod)
    return mod


@unittest.skipUnless(HAVE_CRYPTOGRAPHY, "the harness needs cryptography")
class TestEsp32QemuWait(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.h = load_harness(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def qemu(self, code):
        """A stand-in QEMU running `code`, its output in a fresh log."""
        fd, logpath = tempfile.mkstemp(suffix=".log", dir=self.tmp.name)
        os.close(fd)
        q = self.h.Qemu([sys.executable, "-c", code], logpath)
        self.addCleanup(q.stop)
        return q

    # (a) a line printed just before QEMU exits is found, not reported missing
    def test_line_printed_just_before_exit_is_found(self):
        q = self.qemu('import time; time.sleep(0.1); '
                      'print("tfw-l0: refused", flush=True)')
        q.wait_for(r"tfw-l0: refused", timeout=10)

    # (b) QEMU exiting without the line still fails the wait
    def test_exit_without_the_line_fails(self):
        q = self.qemu('import time; time.sleep(0.1); print("other", flush=True)')
        with self.assertRaises(self.h.Failure) as cm:
            q.wait_for(r"tfw-l0: refused", timeout=10)
        self.assertTrue(str(cm.exception).startswith("QEMU exited ("),
                        str(cm.exception))

    # (c) fewer than `count` lines before the exit still fails the wait
    def test_exit_with_fewer_than_count_fails(self):
        q = self.qemu('import time; time.sleep(0.1); '
                      'print("tfw-l0: refused", flush=True)')
        with self.assertRaises(self.h.Failure) as cm:
            q.wait_for(r"tfw-l0: refused", count=2, timeout=10)
        self.assertTrue(str(cm.exception).startswith("QEMU exited ("),
                        str(cm.exception))

    # (d) a QEMU that never prints the line fails at the deadline
    def test_no_line_by_the_deadline_fails(self):
        q = self.qemu("import time; time.sleep(30)")
        try:
            with self.assertRaises(self.h.Failure) as cm:
                q.wait_for(r"tfw-l0: refused", timeout=1)
            self.assertTrue(str(cm.exception).startswith("1s without"),
                            str(cm.exception))
        finally:
            q.stop()


if __name__ == "__main__":
    unittest.main()
