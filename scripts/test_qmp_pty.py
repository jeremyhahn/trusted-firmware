# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for test/integration/cortex-m/qmp_pty.py (make scripts-test).

A fake QMP monitor on a Unix socket stands in for QEMU: it greets, sends
its scripted lines for each request, and records the raw lines it was sent.
No QEMU. QMP_PTY_PY, when set, names the script to load instead of the real
one (a mutation run only).
"""

import contextlib
import gc
import importlib.util
import io
import json
import os
import socket
import tempfile
import threading
import time
import unittest
import warnings

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("QMP_PTY_PY") or os.path.join(
    REPO, "test", "integration", "cortex-m", "qmp_pty.py")
spec = importlib.util.spec_from_file_location("qmp_pty", SCRIPT)
qp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qp)

GREETING = {"QMP": {"version": {}, "capabilities": []}}
OK = [{"return": {}}]
SERIAL0 = {"label": "serial0", "filename": "pty:fake-pty-7", "frontend-open": True}
MONITOR = {"label": "compat_monitor0", "filename": "pty:fake-pty-9", "frontend-open": True}


class FakeMonitor:
    """One connection: send the greeting, then answers[i] (a list of
    messages) after the i-th line received. answers=None closes the
    connection after the greeting and one request; silent=True
    accepts and never writes."""

    def __init__(self, path, answers, greeting=GREETING, silent=False):
        self.received = []
        self.conn = None
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(1)
        self.server.settimeout(5)
        self.answers, self.greeting, self.silent = answers, greeting, silent
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def send(self, stream, msg):
        stream.write(json.dumps(msg).encode() + b"\n")

    def serve(self):
        try:
            self.conn, _ = self.server.accept()
        except OSError:
            return
        with self.conn, self.conn.makefile("rwb") as stream:
            if self.silent:
                # Wait for the client to give up and close; never write.
                stream.read()
                return
            self.send(stream, self.greeting)
            stream.flush()
            if self.answers is None:
                # Take the request first: hanging up before it would race
                # the client's write into a broken pipe.
                self.received.append(stream.readline())
                return
            for line in stream:
                self.received.append(line)
                for msg in self.answers.pop(0) if self.answers else []:
                    self.send(stream, msg)
                stream.flush()

    def close(self):
        if self.conn is not None:
            with contextlib.suppress(OSError):
                self.conn.shutdown(socket.SHUT_RDWR)
        self.server.close()
        self.thread.join(5)


class TestQmpPty(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="qmp-pty-")
        self.addCleanup(tmp.cleanup)
        self.path = os.path.join(tmp.name, "qmp.sock")

    def monitor(self, answers, **kw):
        m = FakeMonitor(self.path, answers, **kw)
        self.addCleanup(m.close)
        return m

    def run_main(self, *args, argv=None):
        out, err = io.StringIO(), io.StringIO()
        if argv is None:
            argv = ["qmp_pty.py", self.path] + list(args)
        rc = 0
        # main leaves its socket for the process exit to close; the warning
        # that drop raises here is not the script's output.
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err), \
                warnings.catch_warnings():
            warnings.simplefilter("ignore", ResourceWarning)
            try:
                qp.main(argv)
            except SystemExit as e:
                rc = e.code
            gc.collect()
        return rc, out.getvalue(), err.getvalue()

    def assertFails(self, why, *args):
        rc, out, err = self.run_main(*args)
        self.assertEqual((1, ""), (rc, out))
        self.assertTrue(err.startswith("qmp_pty: "), err)
        self.assertIn(why, err)
        return err

    def test_prints_the_pty_of_serial0(self):
        m = self.monitor([OK, [{"return": [MONITOR, SERIAL0]}]])
        self.assertEqual((0, "fake-pty-7\n", ""), self.run_main())
        m.close()
        for line in m.received:
            self.assertTrue(line.endswith(b"\n") and line.count(b"\n") == 1, line)
        self.assertEqual([{"execute": "qmp_capabilities"}, {"execute": "query-chardev"}],
                         [json.loads(line) for line in m.received])

    def test_events_before_each_answer_are_passed_over(self):
        event = {"event": "RESUME", "timestamp": {"seconds": 0, "microseconds": 0}}
        self.monitor([[event] + OK, [event, {"return": [SERIAL0]}]])
        self.assertEqual((0, "fake-pty-7\n", ""), self.run_main())

    def test_a_label_argument_picks_that_chardev(self):
        self.monitor([OK, [{"return": [SERIAL0, MONITOR]}]])
        self.assertEqual((0, "fake-pty-9\n", ""), self.run_main("compat_monitor0"))

    def test_a_chardev_that_is_not_a_pty_fails(self):
        self.monitor([OK, [{"return": [{"label": "serial0", "filename": "file:/x"}]}]])
        self.assertFails("serial0 is 'file:/x', not a pty")

    def test_no_chardev_of_the_label_fails(self):
        self.monitor([OK, [{"return": [MONITOR]}]])
        self.assertFails("QEMU has no chardev serial0")

    def test_an_error_answer_fails_with_its_desc(self):
        self.monitor([[{"error": {"class": "GenericError", "desc": "capabilities negotiated"}}]])
        self.assertFails("the answer to qmp_capabilities: capabilities negotiated")

    def test_a_greeting_without_qmp_fails(self):
        self.monitor([OK, [{"return": [SERIAL0]}]], greeting={"hello": 1})
        self.assertFails(f"{self.path} is not a QMP monitor")

    def test_a_monitor_closed_after_the_greeting_fails(self):
        self.monitor(None)
        self.assertFails("QEMU closed the monitor before the answer to qmp_capabilities")

    def test_a_silent_monitor_fails_within_the_bound(self):
        self.monitor([], silent=True)
        start = time.monotonic()
        rc, out, err = self.run_main("serial0", "0.5")
        self.assertLess(time.monotonic() - start, 5)
        self.assertEqual((1, ""), (rc, out))
        self.assertTrue(err.startswith("qmp_pty: "), err)
        self.assertTrue("no the QMP greeting within 0.5 s" in err
                        or "waiting for the QMP greeting" in err, err)

    def test_no_socket_fails(self):
        self.assertFails("connecting to " + self.path)

    def test_a_wrong_argument_count_prints_the_usage(self):
        for argv in (["qmp_pty.py"], ["qmp_pty.py", self.path, "serial0", "1", "x"]):
            rc, out, err = self.run_main(argv=argv)
            self.assertEqual((2, ""), (rc, out), argv)
            self.assertIn("qmp_pty.py <qmp socket> [<chardev label>] [<bound seconds>]", err)


if __name__ == "__main__":
    unittest.main()
