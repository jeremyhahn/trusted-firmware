#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Print the pty QEMU attached a guest's serial port to, read over QMP.

    qmp_pty.py <qmp socket> [<chardev label>] [<bound seconds>]

qemu_wire_test.sh starts QEMU with `-serial pty -qmp unix:<socket>,...` and
needs the /dev/pts path of serial0, which QEMU picks. QMP's query-chardev
names it as "pty:<path>" (QEMU's docs/interop/qmp-spec.rst and
qapi/char.json). The greeting, qmp_capabilities and query-chardev share one
deadline; a monitor that does not answer within it fails the run with what
was awaited, never a hang. Exit 1 and a reason on stderr on any failure.
"""

import json
import socket
import sys
import time


def fail(why):
    print(f"qmp_pty: {why}", file=sys.stderr)
    sys.exit(1)


def main(argv):
    if not 2 <= len(argv) <= 4:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        sys.exit(2)
    path = argv[1]
    label = argv[2] if len(argv) > 2 else "serial0"
    bound = float(argv[3]) if len(argv) > 3 else 10.0
    deadline = time.monotonic() + bound

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.settimeout(bound)
        s.connect(path)
    except OSError as e:
        fail(f"connecting to {path}: {e}")
    stream = s.makefile("rwb")

    def receive(what):
        # Every QMP message is one line of JSON; events may arrive between
        # a command and its answer and are passed over.
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                fail(f"no {what} within {bound:g} s")
            s.settimeout(left)
            try:
                line = stream.readline()
            except OSError as e:
                fail(f"waiting for {what}: {e}")
            if not line:
                fail(f"QEMU closed the monitor before {what}")
            msg = json.loads(line)
            if "event" in msg:
                continue
            if "error" in msg:
                fail(f"{what}: {msg['error'].get('desc', msg['error'])}")
            return msg

    def execute(cmd):
        stream.write(json.dumps({"execute": cmd}).encode() + b"\n")
        stream.flush()
        return receive(f"the answer to {cmd}")["return"]

    if "QMP" not in receive("the QMP greeting"):
        fail(f"{path} is not a QMP monitor")
    execute("qmp_capabilities")
    for dev in execute("query-chardev"):
        if dev.get("label") == label:
            name = dev.get("filename", "")
            if not name.startswith("pty:"):
                fail(f"{label} is {name!r}, not a pty: start QEMU with -serial pty")
            print(name[len("pty:"):])
            return
    fail(f"QEMU has no chardev {label}")


if __name__ == "__main__":
    main(sys.argv)
