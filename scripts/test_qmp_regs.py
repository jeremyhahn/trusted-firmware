# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for test/integration/cortex-m/qmp_regs.py (make scripts-test).

A fake QMP monitor on a Unix socket stands in for QEMU: it greets, answers
qmp_capabilities and each human-monitor-command, and records what it was
asked; a fake gdbstub on a second socket answers the GDB remote protocol
packets the special registers are read with. No QEMU.
"""

import importlib.util
import io
import json
import os
import shutil
import socket
import struct
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
spec = importlib.util.spec_from_file_location(
    "qmp_regs", os.path.join(REPO, "test", "integration", "cortex-m", "qmp_regs.py"))
qr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qr)

GREETING = {"QMP": {"version": {}, "capabilities": []}}
REGISTERS = ("R00=00000000 R01=00000001 R02=00000002 R03=00000003\n"
             "R12=0000000c R13=203ffe80 R14=fffffff9 R15=00008a3c\n"
             "XPSR=61000003 -ZC- T handler\n")


class FakeMonitor:
    """One connection: greet, then answer each line with answer(request)."""

    def __init__(self, path, answer, greeting=GREETING):
        self.asked = []
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(1)
        self.answer = answer
        self.greeting = greeting
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        conn, _ = self.server.accept()
        with conn, conn.makefile("rwb") as stream:
            if self.greeting is None:
                return
            stream.write(json.dumps(self.greeting).encode() + b"\n")
            stream.flush()
            for line in stream:
                req = json.loads(line)
                self.asked.append(req)
                for reply in self.answer(req):
                    stream.write(json.dumps(reply).encode() + b"\n")
                stream.flush()

    def close(self):
        self.server.close()
        self.thread.join(5)


# A Cortex-M33's features as QEMU's gdbstub gives them: the static core XML,
# whose registers before xpsr carry no regnum, then the m-system and secext
# features it builds (target/arm/gdbstub.c). The '}' in a comment is sent
# escaped, as QEMU escapes it.
FEATURES = {
    "target.xml": ('<?xml version="1.0"?><target><architecture>arm</architecture>'
                   '<xi:include href="arm-m-profile.xml"/>'
                   '<xi:include href="arm-m-system.xml"/>'
                   '<xi:include href="arm-m-secext.xml"/></target>'),
    "arm-m-profile.xml": ('<feature name="org.gnu.gdb.arm.m-profile"><!-- } -->'
                          '<reg name="r0" bitsize="32"/><reg name="pc" bitsize="32" type="code_ptr"/>'
                          '<reg name="xpsr" bitsize="32" regnum="25"/></feature>'),
    "arm-m-system.xml": ('<feature name="org.gnu.gdb.arm.m-system">'
                         '<reg name="msp" bitsize="32" regnum="26" type="int" group="system"/>'
                         '<reg name="primask" bitsize="32" regnum="28" type="int" group="system"/>'
                         '<reg name="basepri" bitsize="32" regnum="29" type="int" group="system"/>'
                         '<reg name="faultmask" bitsize="32" regnum="30" type="int" group="system"/>'
                         '<reg name="control" bitsize="32" regnum="31" type="int" group="system"/>'
                         '</feature>'),
    "arm-m-secext.xml": ('<feature name="org.gnu.gdb.arm.secext">'
                         '<reg name="msplim_s" bitsize="32" regnum="38" type="int"/>'
                         '<reg name="primask_s" bitsize="32" regnum="42" type="int"/>'
                         '<reg name="primask_ns" bitsize="32" regnum="43" type="int"/>'
                         '</feature>'),
}
# Little-endian hex, as `p` answers: PRIMASK and PRIMASK_S set, CONTROL 0x8.
VALUES = {0x1c: "01000000", 0x1d: "00000000", 0x1e: "00000000", 0x1f: "08000000",
          0x2a: "01000000", 0x2b: "00000000"}
SPECIAL_OUT = ("qmp_regs: gdbstub special registers\n"
               "PRIMASK=00000001 BASEPRI=00000000 FAULTMASK=00000000 CONTROL=00000008 "
               "PRIMASK_S=00000001 PRIMASK_NS=00000000\n")


class FakeGdb:
    """One connection of QEMU's gdbstub: ?, qXfer:features:read, p and D."""

    def __init__(self, path, features=FEATURES, values=VALUES, silent=False):
        self.asked = []
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(1)
        self.server.settimeout(5)
        self.features, self.values, self.silent = features, values, silent
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def reply(self, req):
        if req == "?":
            return "S05"
        if req == "D":
            return "OK"
        if req.startswith("p"):
            return self.values.get(int(req[1:], 16), "E14")
        name, span = req[len("qXfer:features:read:"):].rsplit(":", 1)
        off = int(span.split(",")[0], 16)
        text = self.features.get(name)
        if text is None:
            return "E00"
        # 64 bytes at a time, so an annex takes more than one read.
        chunk = text[off:off + 64]
        return ("l" if off + 64 >= len(text) else "m") + chunk

    def serve(self):
        try:
            conn, _ = self.server.accept()
        except OSError:
            return
        with conn, conn.makefile("rb") as stream:
            while True:
                c = stream.read(1)
                if c in (b"", None):
                    return
                if c != b"$":
                    continue
                body = b""
                while (c := stream.read(1)) not in (b"#", b""):
                    body += c
                if int(stream.read(2), 16) != sum(body) & 0xFF:
                    conn.sendall(b"-")
                    continue
                req = body.decode()
                self.asked.append(req)
                if self.silent:
                    continue
                out = self.reply(req).encode()
                for b in b"}#$*":
                    out = out.replace(bytes([b]), b"}" + bytes([b ^ 0x20]))
                conn.sendall(b"+$" + out + b"#%02x" % (sum(out) & 0xFF))

    def close(self):
        self.server.close()
        self.thread.join(5)


def elf32(symbols):
    """A little-endian ELF32 file holding only a symbol table and its strings:
    each symbol (name, value, size, st_info), after the null one."""
    strtab, entries = b"\0", [bytes(16)]
    for name, value, size, info in symbols:
        entries.append(struct.pack("<IIIBBH", len(strtab), value, size, info, 0, 1))
        strtab += name.encode() + b"\0"
    symtab = b"".join(entries)
    shoff = 52 + len(strtab) + len(symtab)
    header = (b"\x7fELF\x01\x01\x01" + bytes(9)
              + struct.pack("<HHIIIIIHHHHHH", 1, 40, 1, 0, 0, shoff, 0, 52, 0, 0, 40, 3, 0))
    sections = (bytes(40)
                + struct.pack("<10I", 0, 3, 0, 0, 52, len(strtab), 0, 0, 1, 0)
                + struct.pack("<10I", 0, 2, 0, 0, 52 + len(strtab), len(symtab), 1, 1, 4, 16))
    return header + strtab + symtab + sections


STUBS = []


def qemu(req):
    """The answers QEMU gives: `info registers` as REGISTERS, x as one word,
    `gdbserver` as a FakeGdb on its socket."""
    if req["execute"] == "qmp_capabilities":
        return [{"return": {}}]
    line = req["arguments"]["command-line"]
    if line == "info registers":
        return [{"event": "RESET"}, {"return": REGISTERS}]
    if line.startswith("gdbserver unix:"):
        STUBS.append(FakeGdb(line[len("gdbserver unix:"):].split(",")[0]))
        return [{"return": "Waiting for gdb connection\r\n"}]
    return [{"return": line.split()[-1] + ": 0x00000000\n"}]


class TestDump(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="qmp-regs-")
        self.addCleanup(shutil.rmtree, self.dir)
        self.path = os.path.join(self.dir, "qmp.sock")
        self.gdbserver = f"gdbserver unix:{self.path}.gdb,server=on,wait=off"
        self.addCleanup(self.close_stubs)

    def close_stubs(self):
        while STUBS:
            STUBS.pop().close()

    def monitor(self, answer, **kw):
        m = FakeMonitor(self.path, answer, **kw)
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

    def test_dumps_every_command_then_the_stack_then_the_extra(self):
        m = self.monitor(qemu)
        rc, out, err = self.run_main("2", "x /4wx 0x20000000")
        self.assertEqual((0, ""), (rc, err))
        self.assertEqual("qmp_capabilities", m.asked[0]["execute"])
        self.assertEqual(list(qr.COMMANDS) + ["x /32wx 0x203ffe80", "x /4wx 0x20000000",
                                              self.gdbserver],
                         self.lines(m))
        self.assertIn("qmp_regs: info registers\n" + REGISTERS, out)
        self.assertIn("qmp_regs: x /6wx 0xe000ed24\n0xe000ed24: 0x00000000\n", out)
        self.assertTrue(out.endswith(SPECIAL_OUT), out)
        self.assertEqual(["?", "qXfer:features:read:target.xml:0,800",
                          "qXfer:features:read:target.xml:40,800",
                          "qXfer:features:read:target.xml:80,800",
                          "qXfer:features:read:arm-m-profile.xml:0,800"],
                         STUBS[0].asked[:5])
        self.assertEqual(["p1c", "p1d", "p1e", "p1f", "p2a", "p2b", "D"], STUBS[0].asked[-7:])

    def test_a_gdbstub_without_the_special_registers_fails(self):
        def answer(req):
            line = req.get("arguments", {}).get("command-line", "")
            if line.startswith("gdbserver"):
                STUBS.append(FakeGdb(self.path + ".gdb",
                                     features={"target.xml": FEATURES["arm-m-profile.xml"]}))
                return [{"return": ""}]
            return qemu(req)
        self.monitor(answer)
        rc, out, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertIn("qmp_regs: info registers\n", out)
        self.assertIn("the gdbstub names no PRIMASK, BASEPRI, FAULTMASK or CONTROL", err)

    def test_a_register_the_gdbstub_cannot_read_fails(self):
        def answer(req):
            line = req.get("arguments", {}).get("command-line", "")
            if line.startswith("gdbserver"):
                STUBS.append(FakeGdb(self.path + ".gdb", values={0x1c: "01000000"}))
                return [{"return": ""}]
            return qemu(req)
        self.monitor(answer)
        rc, _, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertIn("the gdbstub's basepri: 'E14'", err)

    def test_a_silent_gdbstub_fails_within_the_bound(self):
        def answer(req):
            line = req.get("arguments", {}).get("command-line", "")
            if line.startswith("gdbserver"):
                STUBS.append(FakeGdb(self.path + ".gdb", silent=True))
                return [{"return": ""}]
            return qemu(req)
        self.monitor(answer)
        start = time.monotonic()
        rc, out, err = self.run_main("0.5")
        self.assertLess(time.monotonic() - start, 2)
        self.assertEqual(1, rc)
        self.assertIn("qmp_regs: x /4wx 0xe000e010\n", out)
        self.assertIn("no the gdbstub's answer to ? within 0.5 s", err)

    def test_no_gdbstub_fails(self):
        def answer(req):
            if req.get("arguments", {}).get("command-line", "").startswith("gdbserver"):
                return [{"return": "gdbstub: unknown device\r\n"}]
            return qemu(req)
        self.monitor(answer)
        rc, _, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertIn("connecting to the gdbstub at " + self.path + ".gdb", err)

    def test_no_stack_pointer_skips_the_stack(self):
        def answer(req):
            if req.get("arguments", {}).get("command-line") == "info registers":
                return [{"return": "no registers\n"}]
            return qemu(req)
        m = self.monitor(answer)
        rc, _, _ = self.run_main("2")
        self.assertEqual(0, rc)
        self.assertEqual(list(qr.COMMANDS) + [self.gdbserver], self.lines(m))

    def test_a_silent_monitor_fails_within_the_bound(self):
        self.monitor(lambda req: [])
        start = time.monotonic()
        rc, out, err = self.run_main("0.3")
        self.assertLess(time.monotonic() - start, 2)
        self.assertEqual(1, rc)
        self.assertIn("the answer to qmp_capabilities", err)
        self.assertEqual("", out)

    def test_what_was_read_before_a_failure_is_kept(self):
        def answer(req):
            if req.get("arguments", {}).get("command-line") == "x /1wx 0xe000ed04":
                return [{"error": {"class": "GenericError", "desc": "no CPU"}}]
            return qemu(req)
        self.monitor(answer)
        rc, out, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertIn("qmp_regs: info registers\n", out)
        self.assertIn("the answer to human-monitor-command: no CPU", err)

    def test_not_a_qmp_monitor_fails(self):
        self.monitor(qemu, greeting={"hello": 1})
        rc, _, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertIn("is not a QMP monitor", err)

    def test_a_closed_monitor_fails(self):
        self.monitor(qemu, greeting=None)
        rc, _, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertIn("QEMU closed the monitor before the QMP greeting", err)

    def test_no_socket_fails(self):
        rc, _, err = self.run_main("2")
        self.assertEqual(1, rc)
        self.assertIn("connecting to " + self.path, err)

    def test_the_words_read_are_the_ones_each_hypothesis_needs(self):
        self.assertEqual(("info registers",
                          "x /1wx 0xe000ed04",
                          "x /4wx 0xe000ed08",
                          "x /3wx 0xe000ed18",
                          "x /6wx 0xe000ed24",
                          "x /1wx 0xe000edf0",
                          "x /2wx 0xe000e100",
                          "x /2wx 0xe000e200",
                          "x /2wx 0xe000e300",
                          "x /12wx 0xe000e400",
                          "x /4wx 0xe000e010",
                          "x /4wx 0xe002e010"), qr.COMMANDS)

    def test_sym_dumps_each_object_its_elf_names(self):
        elf = os.path.join(self.dir, "app.elf")
        with open(elf, "wb") as f:
            f.write(elf32([("hal_uart.c", 0, 0, 0x04),
                           ("rx_head", 0x30000010, 1, 0x01),
                           ("rx_buf.lto_priv.0", 0x30000020, 128, 0x01),
                           ("rx_header", 0x30000011, 1, 0x01),
                           ("main.c", 0, 0, 0x04),
                           ("transport", 0x30000100, 300, 0x01),
                           ("rx_head", 0x00001000, 8, 0x12),
                           ("ticks", 0x30000004, 4, 0x11)]))
        m = self.monitor(qemu)
        rc, out, err = self.run_main("2", "x /3wx 0x40200004", "elf=" + elf, "sym=rx_head",
                                     "sym=rx_buf", "sym=rx_dropped", "sym=transport",
                                     "sym=ticks")
        self.assertEqual((0, ""), (rc, err))
        self.assertEqual(["x /3wx 0x40200004", "x /1bx 0x30000010", "x /32wx 0x30000020",
                          "x /64wx 0x30000100", "x /1wx 0x30000004", self.gdbserver],
                         self.lines(m)[len(qr.COMMANDS) + 1:])
        self.assertIn("qmp_regs: rx_head (hal_uart.c), 1 bytes at 0x30000010\n"
                      "qmp_regs: x /1bx 0x30000010\n0x30000010: 0x00000000\n"
                      "qmp_regs: rx_buf.lto_priv.0 (hal_uart.c), 128 bytes at 0x30000020\n"
                      "qmp_regs: x /32wx 0x30000020\n0x30000020: 0x00000000\n"
                      f"qmp_regs: {elf} has no object rx_dropped\n"
                      "qmp_regs: transport (main.c), 300 bytes at 0x30000100\n"
                      "qmp_regs: x /64wx 0x30000100\n0x30000100: 0x00000000\n"
                      "qmp_regs: ticks (global), 4 bytes at 0x30000004\n", out)

    def test_sym_with_no_elf_fails(self):
        self.monitor(qemu)
        rc, out, err = self.run_main("2", "sym=rx_head")
        self.assertEqual(1, rc)
        self.assertIn("qmp_regs: sym=rx_head names no elf= before it", err)
        self.assertIn("qmp_regs: x /4wx 0xe000e010\n", out)

    def test_an_elf_that_is_not_elf32_fails(self):
        path = os.path.join(self.dir, "app.elf")
        with open(path, "wb") as f:
            f.write(b"\x7fELF\x02\x01" + bytes(58))
        self.monitor(qemu)
        rc, _, err = self.run_main("2", "elf=" + path, "sym=rx_head")
        self.assertEqual(1, rc)
        self.assertIn(f"qmp_regs: {path} is not a little-endian ELF32 file", err)

    def test_a_truncated_symbol_table_fails(self):
        path = os.path.join(self.dir, "app.elf")
        with open(path, "wb") as f:
            f.write(elf32([("rx_head", 0x30000010, 1, 0x01)])[:-40])
        self.monitor(qemu)
        rc, _, err = self.run_main("2", "elf=" + path, "sym=rx_head")
        self.assertEqual(1, rc)
        self.assertIn(f"qmp_regs: {path}'s symbol table: ", err)

    def test_usage(self):
        err = io.StringIO()
        saved, qr.sys.stderr = qr.sys.stderr, err
        try:
            self.assertEqual(2, qr.main(["qmp_regs.py"]))
        finally:
            qr.sys.stderr = saved
        self.assertIn("qmp_regs.py <qmp socket>", err.getvalue())


if __name__ == "__main__":
    unittest.main()
