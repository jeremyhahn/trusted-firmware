# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Edge-case tests for test/integration/cortex-m/qmp_pty.py (make scripts-test).

The paths scripts/test_qmp_pty.py does not reach: a QMP error with no desc,
a chardev with no filename, a monitor that hangs up before its greeting, an
event before the greeting, a monitor that greets and then goes silent, and
two chardevs with one label. The fake monitor, the answers and the loaded
script come from scripts/test_qmp_pty.py (QMP_PTY_PY is honoured there).
No QEMU.
"""

import importlib.util
import json
import os
import socket
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "qmp_pty_helpers", os.path.join(HERE, "test_qmp_pty.py"))
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)

EVENT = {"event": "RESUME", "timestamp": {"seconds": 0, "microseconds": 0}}


class HangUpMonitor:
    """Accepts one connection and closes it before any greeting."""

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
        conn.close()

    def close(self):
        self.server.close()
        self.thread.join(5)


class EventFirstMonitor(h.FakeMonitor):
    """A FakeMonitor that sends an event before its greeting."""

    def send(self, stream, msg):
        if msg is self.greeting:
            super().send(stream, EVENT)
        super().send(stream, msg)


class TestQmpPtyEdges(unittest.TestCase):
    # The helpers of TestQmpPty, bound here so its own tests are not run twice.
    setUp = h.TestQmpPty.setUp
    monitor = h.TestQmpPty.monitor
    run_main = h.TestQmpPty.run_main

    def test_an_error_with_no_desc_prints_the_whole_error(self):
        self.monitor([[{"error": {"class": "GenericError"}}]])
        self.assertEqual(
            (1, "", "qmp_pty: the answer to qmp_capabilities: {'class': 'GenericError'}\n"),
            self.run_main())

    def test_a_chardev_with_no_filename_is_not_a_pty(self):
        self.monitor([h.OK, [{"return": [{"label": "serial0"}]}]])
        self.assertEqual(
            (1, "", "qmp_pty: serial0 is '', not a pty: start QEMU with -serial pty\n"),
            self.run_main())

    def test_a_monitor_closed_before_the_greeting_fails(self):
        m = HangUpMonitor(self.path)
        self.addCleanup(m.close)
        self.assertEqual(
            (1, "", "qmp_pty: QEMU closed the monitor before the QMP greeting\n"),
            self.run_main())

    def test_an_event_before_the_greeting_is_passed_over(self):
        m = EventFirstMonitor(self.path, [h.OK, [{"return": [h.SERIAL0]}]])
        self.addCleanup(m.close)
        self.assertEqual((0, "fake-pty-7\n", ""), self.run_main())

    def test_silence_after_the_greeting_fails_within_the_bound(self):
        m = self.monitor([])
        start = time.monotonic()
        rc, out, err = self.run_main("serial0", "0.5")
        self.assertLess(time.monotonic() - start, 5)
        self.assertEqual((1, ""), (rc, out))
        self.assertTrue(
            err == "qmp_pty: waiting for the answer to qmp_capabilities: timed out\n"
            or err.startswith("qmp_pty: no the answer to qmp_capabilities within 0.5 s"),
            err)
        m.close()
        self.assertEqual([{"execute": "qmp_capabilities"}],
                         [json.loads(line) for line in m.received])

    def test_the_first_chardev_of_a_repeated_label_wins(self):
        self.monitor([h.OK, [{"return": [
            {"label": "serial0", "filename": "pty:first"},
            {"label": "serial0", "filename": "file:/x"}]}]])
        self.assertEqual((0, "first\n", ""), self.run_main())


if __name__ == "__main__":
    unittest.main()
