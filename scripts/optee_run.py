#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The console side of `make optee-run` (mk/optee.mk; the design is
# docs/optee-trustzone/README.md): boots QEMU virt with the OP-TEE stack
# headless, logs in on the normal world's console, runs one command there and
# exits with that command's status.
#
# QEMU is the one the pinned manifest builds, started with the arguments the
# pinned `build` repository's own headless run uses (QEMU_CHECK_ARGS, the normal
# world on stdio). Upstream drives that console with expect and runs only
# xtest; this runs any command, so go-enclave's backendtest can be the command.
#
# Nothing waits on a clock. Each step waits for the console to print what the
# next step needs (the login prompt, the shell prompt, the status line), and a
# QEMU that exits first, or a kernel panic, fails the run.
#
# The command runs in a subshell, in the share's mount point, once the guest's
# init has started tee-supplicant: Buildroot's init starts it before the getty,
# and the guest checks it is running before the command starts. Its status
# comes back as a marker line the guest prints with printf, so the line the
# terminal echoes, which names the marker only in pieces, never matches.
#
#   python3 scripts/optee_run.py --mount /mnt/host --cmd 'xtest -l 0' "<qemu and its arguments>"

import argparse
import os
import re
import subprocess
import sys

LOGIN = b"login:"
PROMPT = b"# "
PANIC = b"Kernel panic"
STATUS = re.compile(rb"TFW-GUEST-STATUS=(\d+)")
ERROR = re.compile(rb"TFW-GUEST-ERROR=([a-z-]+)")

# The console's text is matched in a window this long: past every prompt and
# marker, and it bounds what a chatty boot keeps in memory.
WINDOW = 8192

# A failure of the run itself, not of the command: the guest never reached the
# command, or never said how it ended.
RUN_FAILED = 125


def guest_line(cmd, mount):
    """The one line typed at the guest's shell. Upstream's headless run sets
    the same LD_LIBRARY_PATH, because the toolchain's libraries are in one
    directory or the other."""
    mark = "printf 'TFW-%s=%s\\n' "
    return (
        "export LD_LIBRARY_PATH=/lib:/lib/arm-linux-gnueabihf; "
        f"if ! pidof tee-supplicant >/dev/null; then {mark}GUEST-ERROR no-tee-supplicant; "
        f"elif ! grep -q ' {mount} 9p ' /proc/mounts; then {mark}GUEST-ERROR no-share; "
        f"elif ! cd {mount}; then {mark}GUEST-ERROR no-share; "
        f"else ( {cmd} ); {mark}GUEST-STATUS $?; fi\r"
    )


def run(qemu, cmd, mount, out):
    """Boot, run cmd, and return its status, or None with the reason on
    stderr. qemu is a shell command line, as upstream's expect script runs it."""
    p = subprocess.Popen(["sh", "-c", "exec " + qemu], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    state, buf = "login", b""
    try:
        while True:
            chunk = os.read(p.stdout.fileno(), 65536)
            if not chunk:
                print(f"optee-run: QEMU exited (status {p.wait()}) while waiting for the {state}",
                      file=sys.stderr)
                return None
            out.write(chunk)
            out.flush()
            buf = (buf + chunk)[-WINDOW:]
            if PANIC in buf:
                print("optee-run: the guest kernel panicked", file=sys.stderr)
                return None
            if state == "login" and LOGIN in buf:
                p.stdin.write(b"root\r")
                state, buf = "prompt", buf[buf.rindex(LOGIN) + len(LOGIN):]
            if state == "prompt" and PROMPT in buf:
                p.stdin.write(guest_line(cmd, mount).encode())
                state, buf = "status", b""
            if state == "status":
                m = ERROR.search(buf)
                if m:
                    print(f"optee-run: the guest refused the command: {m.group(1).decode()}",
                          file=sys.stderr)
                    return None
                m = STATUS.search(buf)
                if m:
                    return int(m.group(1))
    finally:
        if p.poll() is None:
            p.terminate()
        p.wait()
        out.flush()


def main(argv=None):
    ap = argparse.ArgumentParser(description="Boot OP-TEE on QEMU virt and run one command in the guest.")
    ap.add_argument("--cmd", required=True, help="the command, as typed at the guest's shell")
    ap.add_argument("--mount", required=True, help="where the guest mounts the share (virtio-9p tag host)")
    ap.add_argument("qemu", help="QEMU and its arguments, one shell command line")
    args = ap.parse_args(argv)
    if not args.cmd.strip():
        print("optee-run: --cmd names nothing to run", file=sys.stderr)
        return RUN_FAILED
    status = run(args.qemu, args.cmd, args.mount, sys.stdout.buffer)
    if status is None:
        return RUN_FAILED
    print(f"\noptee-run: the guest command exited {status}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
