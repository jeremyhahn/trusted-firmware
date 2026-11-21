# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests for scripts/fake_probe_tool.py's memory model and flag reader.

scripts/test_cortex_m_flash.py rests on the fake: an address is one key
however it is written, a read of the written length gives the bytes back, any
other read is erased flash, FAKE_SHORT shortens only an erased read, and
after() takes the words behind a flag. The helpers are called directly; no
tool is linked onto PATH and no device is opened.
"""

import importlib.util
import json
import os
import shutil
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
# FAKE_PROBE_TOOL points the suite at a scratch copy for a mutation run only.
FAKE = os.environ.get("FAKE_PROBE_TOOL", os.path.join(HERE, "fake_probe_tool.py"))
spec = importlib.util.spec_from_file_location("fake_probe_tool", FAKE)
fpt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fpt)


class FakeProbeToolMemTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.mem = os.path.join(self.tmp, "mem.json")
        env = {k: v for k, v in os.environ.items() if k != "FAKE_SHORT"}
        env["FAKE_MEM"] = self.mem
        patcher = mock.patch.dict(os.environ, env, clear=True)
        patcher.start()
        self.addCleanup(patcher.stop)

    def path(self, name):
        return os.path.join(self.tmp, name)

    def write(self, data, addr):
        img = self.path("img.bin")
        with open(img, "wb") as f:
            f.write(data)
        fpt.write(img, addr)

    def read(self, addr, size):
        out = self.path("out.bin")
        fpt.read(addr, size, out)
        with open(out, "rb") as f:
            return f.read()

    def stored(self):
        with open(self.mem, encoding="utf-8") as f:
            return json.load(f)

    def test_no_mem_file_is_empty(self):
        self.assertFalse(os.path.exists(self.mem))
        self.assertEqual(fpt.load_mem(), {})

    def test_write_keys_the_address_in_decimal_and_reads_back(self):
        self.write(b"\x01\x02\x03", "0x08000000")
        self.assertEqual(self.stored(), {"134217728": "010203"})
        self.assertEqual(self.read("134217728", "3"), b"\x01\x02\x03")
        self.assertEqual(self.read("0x08000000", "0x3"), b"\x01\x02\x03")

    def test_other_length_or_address_reads_erased(self):
        self.write(b"\x01\x02\x03", "0x08000000")
        self.assertEqual(self.read("0x08000000", "0x4"), b"\xff" * 4)
        self.assertEqual(self.read("0x20000000", "2"), b"\xff" * 2)

    def test_fake_short_shortens_only_an_erased_read(self):
        self.write(b"\x01\x02\x03", "0x08000000")
        with mock.patch.dict(os.environ, {"FAKE_SHORT": "1"}):
            self.assertEqual(self.read("0x08000000", "4"), b"\xff" * 3)
            self.assertEqual(self.read("0x08000000", "3"), b"\x01\x02\x03")

    def test_second_write_keeps_the_first_key(self):
        self.write(b"\x01\x02\x03", "0x08000000")
        self.write(b"\xaa", "0x100")
        self.assertEqual(self.stored(), {"134217728": "010203", "256": "aa"})


class FakeProbeToolAfterTest(unittest.TestCase):
    def test_after_takes_the_words_behind_a_flag(self):
        self.assertEqual(fpt.after(["-c", "port=SWD", "-u", "0x0", "4", "f"], "-u", 3), ["0x0", "4", "f"])
        self.assertEqual(fpt.after(["load", "-v", "x", "-o", "0x1"], "-o"), ["0x1"])
        self.assertEqual(fpt.after(["-u", "0x0"], "-u", 3), ["0x0"])
        with self.assertRaises(ValueError):
            fpt.after(["a"], "-u")


if __name__ == "__main__":
    unittest.main()
