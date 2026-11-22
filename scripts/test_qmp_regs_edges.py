# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for the paths of test/integration/cortex-m/qmp_regs.py that
scripts/test_qmp_regs.py does not reach (make scripts-test): an elf= that
cannot be read, an elf= read only on a sym=, a second elf= replacing the
first, a size-0 object, a QMP error with no desc, a gdbstub with no
target.xml or that hangs up, and an annex included twice.

The fakes are scripts/test_qmp_regs.py's, loaded here; its tests are not run
twice. No QEMU.
"""

import importlib.util
import io
import os
import shutil
import socket
import tempfile
import threading
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("qmp_regs_helpers", os.path.join(HERE, "test_qmp_regs.py"))
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
qr = h.qr


class HangUp:
    """A gdbstub that accepts, reads the first packet and closes."""

    def __init__(self, path):
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(1)
        self.server.settimeout(5)
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        try:
            conn, _ = self.server.accept()
        except OSError:
            return
        with conn:
            conn.recv(64)

    def close(self):
        self.server.close()
        self.thread.join(5)


class TestEdges(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="qmp-regs-")
        self.addCleanup(shutil.rmtree, self.dir)
        self.path = os.path.join(self.dir, "qmp.sock")
        self.gdbserver = f"gdbserver unix:{self.path}.gdb,server=on,wait=off"
        self.addCleanup(self.close_stubs)

    def close_stubs(self):
        while h.STUBS:
            h.STUBS.pop().close()

    def monitor(self, answer, **kw):
        m = h.FakeMonitor(self.path, answer, **kw)
        self.addCleanup(m.close)
        return m

    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        saved, qr.sys.stderr = qr.sys.stderr, err
        try:
            rc = qr.main(["qmp_regs.py", self.path] + list(args), out)
        finally:
            qr.sys.stderr = saved
        return rc, out.getvalue(), err.getvalue()

    def lines(self, m):
        return [r["arguments"]["command-line"] for r in m.asked
                if r["execute"] == "human-monitor-command"]

    def gdb(self, **kw):
        """QEMU's answers, with `gdbserver` starting a FakeGdb(**kw)."""
        def answer(req):
            line = req.get("arguments", {}).get("command-line", "")
            if line.startswith("gdbserver"):
                h.STUBS.append(h.FakeGdb(self.path + ".gdb", **kw))
                return [{"return": ""}]
            return h.qemu(req)
        return answer

    def write_elf(self, name, symbols):
        path = os.path.join(self.dir, name)
        with open(path, "wb") as f:
            f.write(h.elf32(symbols))
        return path

    def test_an_elf_that_cannot_be_read_fails_on_its_sym(self):
        path = os.path.join(self.dir, "none.elf")
        m = self.monitor(h.qemu)
        rc, _, err = self.run_main("2", "elf=" + path, "sym=rx_head")
        self.assertEqual(1, rc)
        self.assertEqual(f"qmp_regs: reading {path}: [Errno 2] No such file or directory: '{path}'\n",
                         err)
        self.assertEqual("x /32wx 0x203ffe80", self.lines(m)[-1])

    def test_an_elf_with_no_sym_after_it_is_never_opened(self):
        self.monitor(h.qemu)
        rc, _, err = self.run_main("2", "elf=" + os.path.join(self.dir, "none.elf"))
        self.assertEqual((0, ""), (rc, err))

    def test_a_gdbstub_with_no_target_xml_fails(self):
        self.monitor(self.gdb(features={}))
        rc, _, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertEqual("qmp_regs: the gdbstub has no target.xml: 'E00'\n", err)

    def test_a_gdbstub_that_hangs_up_fails(self):
        def answer(req):
            line = req.get("arguments", {}).get("command-line", "")
            if line.startswith("gdbserver"):
                h.STUBS.append(HangUp(self.path + ".gdb"))
                return [{"return": ""}]
            return h.qemu(req)
        self.monitor(answer)
        rc, _, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertEqual("qmp_regs: QEMU closed the gdbstub before the gdbstub's answer to ?\n", err)

    def test_a_qmp_error_with_no_desc_prints_the_whole_error(self):
        def answer(req):
            if req["execute"] == "human-monitor-command":
                return [{"error": {"class": "GenericError"}}]
            return h.qemu(req)
        self.monitor(answer)
        rc, out, err = self.run_main("2")
        self.assertEqual((1, ""), (rc, out))
        self.assertEqual("qmp_regs: the answer to human-monitor-command: {'class': 'GenericError'}\n",
                         err)

    def test_a_second_elf_replaces_the_first_and_a_size_0_object_reads_one_byte(self):
        a = self.write_elf("a.elf", [("rx_head", 0x30000010, 1, 0x11)])
        b = self.write_elf("b.elf", [("rx_head", 0x30000040, 6, 0x11),
                                     ("marker", 0x30000050, 0, 0x11)])
        m = self.monitor(h.qemu)
        rc, _, err = self.run_main("2", "elf=" + a, "sym=rx_head", "elf=" + b, "sym=rx_head",
                                   "sym=marker")
        self.assertEqual((0, ""), (rc, err))
        self.assertEqual(["x /1bx 0x30000010", "x /6bx 0x30000040", "x /1bx 0x30000050",
                          self.gdbserver],
                         self.lines(m)[len(qr.COMMANDS) + 1:])

    def test_an_annex_included_twice_is_read_once(self):
        f = dict(h.FEATURES)
        secext = '<xi:include href="arm-m-secext.xml"/>'
        f["target.xml"] = f["target.xml"].replace(
            secext, '<xi:include href="arm-m-system.xml"/>' + secext)
        self.monitor(self.gdb(features=f))
        rc, out, err = self.run_main("2")
        self.assertEqual((0, ""), (rc, err))
        self.assertTrue(out.endswith(h.SPECIAL_OUT), out)
        self.assertEqual(1, h.STUBS[0].asked.count("qXfer:features:read:arm-m-system.xml:0,800"))
        self.assertEqual(["p1c", "p1d", "p1e", "p1f", "p2a", "p2b", "D"], h.STUBS[0].asked[-7:])


if __name__ == "__main__":
    unittest.main()
