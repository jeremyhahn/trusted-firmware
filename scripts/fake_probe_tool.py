#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""A stand-in for the Cortex-M probe tools, for scripts/test_cortex_m_flash.py.

Linked into a temporary directory under each tool's name (STM32_Programmer_CLI,
picotool, JLinkExe, pyocd, arm-none-eabi-objcopy) and put first on PATH. It
logs every call to $FAKE_LOG, one line each, and models the part's memory in
$FAKE_MEM: a write stores the file's bytes at its address, a read of the same
address and length gives them back, any other read gives 0xFF bytes. It talks
to no device.
"""

import json
import os
import sys


def load_mem():
    path = os.environ["FAKE_MEM"]
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def write(path, addr):
    mem = load_mem()
    with open(path, "rb") as f:
        mem[str(int(addr, 0))] = f.read().hex()
    with open(os.environ["FAKE_MEM"], "w", encoding="utf-8") as f:
        json.dump(mem, f)


def read(addr, size, path):
    size = int(size, 0)
    data = bytes.fromhex(load_mem().get(str(int(addr, 0)), ""))
    if len(data) != size:
        data = b"\xff" * (size - int(os.environ.get("FAKE_SHORT", "0")))
    with open(path, "wb") as f:
        f.write(data)


def after(argv, flag, n=1):
    i = argv.index(flag)
    return argv[i + 1:i + 1 + n]


def main():
    tool = os.path.basename(sys.argv[0])
    argv = sys.argv[1:]
    with open(os.environ["FAKE_LOG"], "a", encoding="utf-8") as log:
        log.write(" ".join([tool] + argv) + "\n")
    if tool == "arm-none-eabi-objcopy":
        with open(argv[-2], "rb") as src, open(argv[-1], "wb") as dst:
            dst.write(src.read())
    elif tool == "STM32_Programmer_CLI":
        if "-u" in argv:
            read(*after(argv, "-u", 3))
        elif "-d" in argv:
            write(*after(argv, "-d", 2))
        elif "-ob" in argv:
            print("OPTION BYTES (fake)")
    elif tool == "picotool":
        if argv[0] == "save":
            lo, hi, path = after(argv, "-r", 3)
            read(lo, str(int(hi, 0) - int(lo, 0)), path)
        elif argv[0] == "load":
            write(after(argv, "-v")[0], after(argv, "-o")[0])
        elif argv[:2] == ["otp", "dump"]:
            print("OTP (fake)")
    elif tool == "JLinkExe":
        with open(after(argv, "-CommandFile")[0], encoding="utf-8") as f:
            for line in f:
                cmd, _, rest = line.strip().partition(" ")
                args = rest.split(",")
                if cmd == "savebin":
                    read(args[1], args[2], args[0])
                elif cmd == "loadbin":
                    write(args[0], args[1])
    elif tool == "pyocd":
        if argv[0] == "cmd":
            _, addr, size, path = after(argv, "-c")[0].split()
            read(addr, size, path)
        elif argv[0] == "load":
            write(argv[-1], after(argv, "--base-address")[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
