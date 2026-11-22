#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# optee_run.run reads the console in chunks, and a read can end inside a
# marker line. A marker counts only once its line has ended (#640).

import contextlib
import importlib.util
import io
import os
import types
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("optee_run", os.path.join(HERE, "optee_run.py"))
optee_run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(optee_run)


class FakeProc:
    def __init__(self):
        self.stdin = io.BytesIO()
        self.stdout = types.SimpleNamespace(fileno=lambda: 0)

    def poll(self):
        return 0

    def wait(self):
        return 0


def run_with(chunks):
    """Run optee_run.run on a console that returns chunks, one per read,
    then b"" (QEMU exited). Returns the status and what went to stderr."""
    chunks = list(chunks)
    saved = optee_run.subprocess, optee_run.os
    optee_run.subprocess = types.SimpleNamespace(
        Popen=lambda *a, **k: FakeProc(), PIPE=-1, STDOUT=-2)
    optee_run.os = types.SimpleNamespace(read=lambda fd, n: chunks.pop(0) if chunks else b"")
    err = io.StringIO()
    try:
        with contextlib.redirect_stderr(err):
            status = optee_run.run("qemu", "true", "/mnt/host", io.BytesIO())
    finally:
        optee_run.subprocess, optee_run.os = saved
    return status, err.getvalue()


BOOT = [b"login: ", b"# "]


class StatusSplitTest(unittest.TestCase):
    def test_status_split_across_reads(self):
        status, _ = run_with(BOOT + [b"TFW-GUEST-STATUS=1", b"27\r\n"])
        self.assertEqual(status, 127)

    def test_status_in_one_read(self):
        status, _ = run_with(BOOT + [b"TFW-GUEST-STATUS=3\n"])
        self.assertEqual(status, 3)

    def test_error_split_across_reads(self):
        status, err = run_with(BOOT + [b"TFW-GUEST-ERROR=no-", b"share\r\n"])
        self.assertIsNone(status)
        self.assertIn("refused the command: no-share", err)

    def test_qemu_exits_mid_marker(self):
        status, err = run_with(BOOT + [b"TFW-GUEST-STATUS=1"])
        self.assertIsNone(status)
        self.assertIn("QEMU exited", err)


if __name__ == "__main__":
    unittest.main()
