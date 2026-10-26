#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Print a QEMU Cortex-M guest's registers, read over QMP, for a failed run.

    qmp_regs.py <qmp socket> [<bound seconds>] [<argument> ...]

qemu_wire_test.sh calls it when a row fails, before its trap kills QEMU, and
appends what it prints to the boot's QEMU log, so a guest that stopped
answering says where it stopped. Each HMP command goes through QMP's
human-monitor-command (qapi/misc.json): `info registers` (the core registers,
with XPSR's handler or thread mode), then the System Control Block and NVIC
words a hang is told apart by (ARMv7-M and ARMv8-M ARM, B3.2 and B3.4, the
same addresses on both), then the 32 words at the stack pointer `info
registers` named, where an exception frame is, then each <argument> in turn,
then the special registers `info registers` leaves out.

    ICSR    0xE000ED04  the whole word: VECTACTIVE, RETTOBASE, VECTPENDING,
                        ISRPENDING, PENDSTSET and, on ARMv8-M, STTNS
    VTOR    0xE000ED08  then AIRCR (BFHFNMINS on ARMv8-M), SCR and CCR
    SHPR1   0xE000ED18  then SHPR2 and SHPR3, SysTick's priority in its top byte
    SHCSR   0xE000ED24  then CFSR, HFSR, DFSR, MMFAR and BFAR: a fault's kind
                        and address
    DHCSR   0xE000EDF0  C_HALT and C_MASKINTS: a debugger's hold on the core
    ISER    0xE000E100  the external interrupts enabled
    ISPR    0xE000E200  pending
    IABR    0xE000E300  active
    IPR     0xE000E400  the priorities of interrupts 0 to 47
    SysTick 0xE000E010  CTRL, LOAD, VAL and CALIB, the bank the core runs in
    SysTick 0xE002E010  the Non-secure bank, through the System Control
                        Space's Non-secure alias; an ARMv7-M core has none
                        and QEMU says it cannot access the memory

An <argument> is an HMP command, or one of two that read the firmware's own
state by symbol:

    elf=<file>   the ELF image the guest runs, for the sym= after it
    sym=<name>   every object named <name> in its symbol table, a local one
                 under the source file its symbol table puts it in and with
                 any suffix LTO gave it (rx_head.lto_priv.0): its address,
                 size and file, then its bytes (at most 256)

A name the image does not have is said and passed over.

PRIMASK, BASEPRI, FAULTMASK and CONTROL are not in `info registers`
(target/arm/cpu.c, arm_cpu_dump_state) and have no address, so they are read
from QEMU's gdbstub: HMP `gdbserver` starts it on <qmp socket>.gdb, and the GDB
remote protocol's qXfer:features:read names each register's regnum in the
org.gnu.gdb.arm.m-system feature and, with the Security Extension, the
org.gnu.gdb.arm.secext one (target/arm/gdbstub.c), then `p` reads it. A pending
exception that is not taken in thread mode with no fault is told apart here:
PRIMASK 1, a BASEPRI at or above its priority, or FAULTMASK 1.

A debug read of these has no side effect in QEMU's model. Every command shares
one deadline, so a monitor that does not answer ends the dump with what was
awaited, never a hang. What was read before a failure is already printed.
Exit 1 and a reason on stderr on any failure.
"""

import json
import re
import socket
import struct
import sys
import time

COMMANDS = (
    "info registers",
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
    "x /4wx 0xe002e010",
)

# The stack pointer in `info registers` (target/arm/cpu.c, arm_cpu_dump_state).
SP = re.compile(r"\bR13=([0-9a-fA-F]{8})\b")

# The gdbstub's names for the special registers; a secext name carries _s or _ns.
SPECIAL = ("primask", "basepri", "faultmask", "control")
INCLUDE = re.compile(r'<xi:include\s+href="([^"]+)"')
REG = re.compile(r"<reg\b[^>]*>")
ATTR = re.compile(r'([\w:]+)="([^"]*)"')
HEX = re.compile(r"(?:[0-9a-fA-F]{2})+")

# The ELF constants objects() reads, and the most of one object sym= dumps.
SHT_SYMTAB = 2
STT_OBJECT = 1
STT_FILE = 4
STB_LOCAL = 0
SYM_MAX = 256


class Failed(Exception):
    pass


def dump(path, bound, extra, out):
    """Print each command and its answer to out; raise Failed on a failure."""
    deadline = time.monotonic() + bound
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.settimeout(bound)
        s.connect(path)
    except OSError as e:
        s.close()
        raise Failed(f"connecting to {path}: {e}")
    with s, s.makefile("rwb") as stream:

        def receive(what):
            # Every QMP message is one line of JSON; events may arrive between
            # a command and its answer and are passed over.
            while True:
                left = deadline - time.monotonic()
                if left <= 0:
                    raise Failed(f"no {what} within {bound:g} s")
                s.settimeout(left)
                try:
                    line = stream.readline()
                except OSError as e:
                    raise Failed(f"waiting for {what}: {e}")
                if not line:
                    raise Failed(f"QEMU closed the monitor before {what}")
                msg = json.loads(line)
                if "event" in msg:
                    continue
                if "error" in msg:
                    raise Failed(f"{what}: {msg['error'].get('desc', msg['error'])}")
                return msg

        def execute(cmd, arguments=None):
            req = {"execute": cmd}
            if arguments is not None:
                req["arguments"] = arguments
            stream.write(json.dumps(req).encode() + b"\n")
            stream.flush()
            return receive(f"the answer to {cmd}")["return"]

        def hmp(line):
            text = execute("human-monitor-command", {"command-line": line})
            print(f"qmp_regs: {line}", file=out)
            print(text.rstrip("\n"), file=out)
            out.flush()
            return text

        if "QMP" not in receive("the QMP greeting"):
            raise Failed(f"{path} is not a QMP monitor")
        execute("qmp_capabilities")
        regs = ""
        for line in COMMANDS:
            text = hmp(line)
            if line == "info registers":
                regs = text
        sp = SP.search(regs)
        if sp:
            hmp(f"x /32wx 0x{sp.group(1).lower()}")
        elf, table = None, None
        for line in extra:
            if line.startswith("elf="):
                elf, table = line[len("elf="):], None
            elif line.startswith("sym="):
                name = line[len("sym="):]
                if elf is None:
                    raise Failed(f"sym={name} names no elf= before it")
                if table is None:
                    table = objects(elf)
                found = [o for o in table if o[0] == name or o[0].startswith(name + ".")]
                if not found:
                    print(f"qmp_regs: {elf} has no object {name}", file=out)
                for sym, src, addr, size in found:
                    print(f"qmp_regs: {sym} ({src}), {size} bytes at 0x{addr:08x}", file=out)
                    n = min(size, SYM_MAX)
                    if n and n % 4 == 0:
                        hmp(f"x /{n // 4}wx 0x{addr:08x}")
                    else:
                        hmp(f"x /{max(n, 1)}bx 0x{addr:08x}")
            else:
                hmp(line)
        special(path + ".gdb", deadline, bound, hmp, out)


def objects(path):
    """[(name, source file, address, size)] for every data object in the
    little-endian ELF32 file at path, in its symbol table's order (ELF, the
    System V gABI's Sections and Symbol Table chapters). A local's file is the
    STT_FILE symbol before it."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise Failed(f"reading {path}: {e}")
    if data[:6] != b"\x7fELF\x01\x01":
        raise Failed(f"{path} is not a little-endian ELF32 file")
    try:
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
        # sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, ...
        sections = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
        found = []
        for sh in sections:
            if sh[1] != SHT_SYMTAB:
                continue
            strtab = sections[sh[6]]
            names = data[strtab[4]:strtab[4] + strtab[5]]
            src = "?"
            for off in range(sh[4], sh[4] + sh[5], 16):
                st_name, value, size, info = struct.unpack_from("<IIIB", data, off)
                name = names[st_name:names.index(b"\0", st_name)].decode("ascii", "replace")
                if info & 0xF == STT_FILE:
                    src = name
                elif info & 0xF == STT_OBJECT:
                    found.append((name, src if info >> 4 == STB_LOCAL else "global", value, size))
    except (struct.error, IndexError, ValueError) as e:
        raise Failed(f"{path}'s symbol table: {e}")
    return found


def special(path, deadline, bound, hmp, out):
    """Start QEMU's gdbstub on path, print the special registers, detach."""
    hmp(f"gdbserver unix:{path},server=on,wait=off")
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.settimeout(max(deadline - time.monotonic(), 0.001))
        s.connect(path)
    except OSError as e:
        s.close()
        raise Failed(f"connecting to the gdbstub at {path}: {e}")
    with s:

        def byte(what):
            left = deadline - time.monotonic()
            if left <= 0:
                raise Failed(f"no {what} within {bound:g} s")
            s.settimeout(left)
            try:
                b = s.recv(1)
            except socket.timeout:
                raise Failed(f"no {what} within {bound:g} s")
            except OSError as e:
                raise Failed(f"waiting for {what}: {e}")
            if not b:
                raise Failed(f"QEMU closed the gdbstub before {what}")
            return b[0]

        def packet(data):
            # $data#checksum, the checksum the sum of data's bytes mod 256. The
            # '+' acknowledging ours, and anything else before the reply's '$',
            # is passed over; in the reply '}' escapes the byte after it, XORed
            # with 0x20 (GDB manual, E.1 Overview).
            what = f"the gdbstub's answer to {data}"
            raw = data.encode()
            s.sendall(b"$" + raw + b"#" + b"%02x" % (sum(raw) & 0xFF))
            while byte(what) != ord("$"):
                pass
            body, escaped = bytearray(), False
            while True:
                c = byte(what)
                if escaped:
                    body.append(c ^ 0x20)
                    escaped = False
                elif c == ord("}"):
                    escaped = True
                elif c == ord("#"):
                    break
                else:
                    body.append(c)
            byte(what)
            byte(what)
            s.sendall(b"+")
            return body.decode("ascii", "replace")

        def annex(name):
            text = ""
            while True:
                r = packet(f"qXfer:features:read:{name}:{len(text.encode()):x},800")
                if not r or r[0] not in "ml":
                    raise Failed(f"the gdbstub has no {name}: {r!r}")
                text += r[1:]
                if r[0] == "l":
                    return text

        packet("?")
        regs, todo, seen = [], ["target.xml"], set()
        while todo:
            name = todo.pop(0)
            if name in seen:
                continue
            seen.add(name)
            xml = annex(name)
            todo += INCLUDE.findall(xml)
            for tag in REG.findall(xml):
                a = dict(ATTR.findall(tag))
                if a.get("name", "").split("_")[0] in SPECIAL and "regnum" in a:
                    regs.append((a["name"], int(a["regnum"])))
        if not regs:
            raise Failed("the gdbstub names no PRIMASK, BASEPRI, FAULTMASK or CONTROL")
        values = []
        for name, num in regs:
            r = packet(f"p{num:x}")
            if not HEX.fullmatch(r):
                raise Failed(f"the gdbstub's {name}: {r!r}")
            values.append(f"{name.upper()}={int.from_bytes(bytes.fromhex(r), 'little'):08x}")
        print("qmp_regs: gdbstub special registers", file=out)
        print(" ".join(values), file=out)
        out.flush()
        packet("D")


def main(argv, out=sys.stdout):
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    bound = float(argv[2]) if len(argv) > 2 else 5.0
    try:
        dump(argv[1], bound, argv[3:], out)
    except Failed as e:
        print(f"qmp_regs: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
