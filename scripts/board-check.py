#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""board-check: the SPEC row `boards` checks a host makes without a compiler.

    python3 scripts/board-check.py [ROOT]

A board is data (docs/boards/adding-a-board.md), so most of what makes one
right is checkable by reading it. This reads every boards/<board>/ and fails,
naming the board and the fact, when

  - a board lacks board_profile.h, board.c, profile.mk or its descriptor test
    (test/unit/test_board_<short>.c, the name board-sweep looks for), or its
    profile names no ARCH with an mk/arch/<arch>.mk;
  - TFW_BOARD_NAME is not the directory's name;
  - two boards claim one TFW_BOARD_ID, the id is outside its architecture's
    range, or the id and directory are not the row of the board-id registry
    (docs/boards/board-id-registry.md) and of its Go mirror (sdk/go/boardid.go),
    which must agree row for row;
  - a profile with a budget names no part in PARTS, its totals are not that
    part's, a budget exceeds its total, or the geometry the profile and
    board_profile.h both state disagree;
  - a part with no boot section carries a tier other than none, states boot
    geometry, or has a BOOT_SUPPORTED row in mk/boot.mk;
  - a pin table names a port, bit, ADC channel or PWM unit the part does not
    have, two pins share one, a table's row count is not its declared count,
    or a pin-number define in board_profile.h is past the last pin.

It changes nothing. A failure is a wrong value or a missing row, not a check to
relax: report the value, and fix it where it is defined.
"""

import ast
import os
import re
import sys

# The memory of each part a budget can name, from its data sheet's features
# page. sram_start is the first SRAM address in the data memory map, so the
# last one is sram_start + sram - 1 (RAM_END). boot is whether the part has a
# boot loader section; one without has nowhere to keep a key (SPEC row boards).
# flash_start (default 0) is where the flash span starts; boot_end, when set,
# is the end of a boot region apart from that flash, else tfw-boot sits at its top.
PARTS = {
    # ATmega48A/PA/88A/PA/168A/PA/328/P data sheet, DS40002061.
    "atmega328p": dict(flash=32768, sram=2048, sram_start=0x0100, eeprom=1024, boot=True),
    # ATmega640/V-1280/V-1281/V-2560/V-2561/V data sheet, DS40002211A.
    "atmega2560": dict(flash=262144, sram=8192, sram_start=0x0200, eeprom=4096, boot=True),
    # ATmega16U4/ATmega32U4 data sheet, Atmel-7766.
    "atmega32u4": dict(flash=32768, sram=2560, sram_start=0x0100, eeprom=1024, boot=True),
    # ATtiny25/45/85 data sheet, Atmel-2586: no boot loader section.
    "attiny85": dict(flash=8192, sram=512, sram_start=0x0060, eeprom=512, boot=False),
    # Arm AN505 as QEMU models it (hw/arm/mps2-tz.c, hw/arm/armsse.c): the code
    # is SSRAM-1 and -2 at 0x38000000, so TFW_FLASH_TOTAL is an address, the
    # end of that span; the SRAM is the IoT Kit's internal 32 KiB. The EEPROM
    # is emulated, a 4096-byte window of RAM (TFW_CM_PLATFORM_SIZE, 0x1000),
    # served by src/hal/cortex-m/hal_eeprom.c; QEMU models no EEPROM part, and
    # the window is RAM, so a write survives a system_reset but not a new QEMU
    # process.
    # tfw-boot is not at the top of that flash but in its own 32 KiB at the
    # base of SSRAM-0, so boot_end is where its region ends.
    "mps2-an505": dict(flash=4194304, flash_start=0x38000000, sram=32768,
                       sram_start=0x30000000, eeprom=4096, boot=True,
                       boot_end=0x10008000),
    # Arm AN500 as QEMU models it (hw/arm/mps2.c): the code is ZBT SSRAM1 at
    # 0x00000000, the SRAM is ZBT SSRAM2 and 3 at 0x20000000. The EEPROM is
    # emulated, a 4096-byte window of RAM (TFW_CM_PLATFORM_SIZE, 0x1000),
    # served by src/hal/cortex-m/hal_eeprom.c; QEMU models no EEPROM part, and
    # the window is RAM, so a write survives a system_reset but not a new QEMU
    # process.
    # tfw-boot is at the base of SSRAM1, where the core's VTOR points at reset,
    # not at its top, so boot_end is where its region ends.
    "mps2-an500": dict(flash=4194304, sram=4194304, sram_start=0x20000000,
                       eeprom=4096, boot=True, boot_end=0x00008000),
    # STM32H573IIK3Q (STM32H573I-DK), ST's cmsis-device-h5 stm32h573xx.h: 2 MiB
    # of flash at 0x08000000, so TFW_FLASH_TOTAL is an address, the end of bank
    # 2; the SRAM is SRAM1's 256 KiB at 0x20000000, the one region the port
    # links; no EEPROM. tfw-boot is the first 32 KiB of flash, not its top.
    "stm32h573iik3q": dict(flash=2097152, flash_start=0x08000000, sram=262144,
                           sram_start=0x20000000, eeprom=0, boot=True,
                           boot_end=0x08008000),
    # RP2350 on the Raspberry Pi Pico 2 W (RP-008373-DS, "Address map"; the
    # board datasheet RP-008304-DS for the flash): the part has no flash of its
    # own, so the flash is the board's 4 MiB QSPI part behind the XIP window at
    # 0x10000000; the SRAM is SRAM0-7, SRAM8-9 left to the boot ROM. No EEPROM
    # cells: the EEPROM is that flash, tfw-boot's 0x140-byte platform span then
    # the 4 KiB user sector below the platform block (#393), 0x1140 in all.
    # tfw-boot is the first 32 KiB of the window, where the boot ROM looks.
    "rp2350": dict(flash=4194304, flash_start=0x10000000, sram=524288,
                   sram_start=0x20000000, eeprom=0x1140, boot=True,
                   boot_end=0x10008000),
    # Nordic nRF54L15 application core (nrfx MDK
    # nrf54l15_xxaa_application_memory.h): 1524 KiB of RRAM from address 0,
    # 256 KiB of RAM at 0x20000000, no EEPROM. tfw-boot is the first 32 KiB of
    # RRAM, where the core fetches its reset vector, so boot_end is 0x8000.
    "nrf54l15": dict(flash=1560576, sram=262144, sram_start=0x20000000,
                     eeprom=0, boot=True, boot_end=0x00008000),
    # NXP MCXN947, core 0's Secure alias (the SDK's MCXN947_cm33_core0.h and
    # its flash linker script, docs/reference/README.md): 2 MiB of flash at
    # 0x10000000, 312 KiB of SRAM at 0x30000000 with RAM ECC off, no EEPROM.
    # tfw-boot is the first 32 KiB of flash, so boot_end is where it ends.
    "mcxn947": dict(flash=2097152, flash_start=0x10000000, sram=319488,
                    sram_start=0x30000000, eeprom=0, boot=True,
                    boot_end=0x10008000),
    # NXP i.MX 95's Cortex-M7 (MIMX9596, UM12527 §1.2 Table 1): its 512 KiB of
    # TCM (IMX95CEC Table 1) as 256 KiB ITCM at M7 0x00000000, the code span,
    # and 256 KiB DTCM at 0x20000000 (TO CONFIRM against IMX95RM; design
    # docs/multi-arch/imx95-design.md question 1); no EEPROM. tfw-boot's
    # reserved 32 KiB is at the ITCM's base, so boot_end is where it ends.
    "mimx9596-m7": dict(flash=262144, sram=262144, sram_start=0x20000000,
                        eeprom=0, boot=True, boot_end=0x00008000),
}

# A port is one I/O register, so a pin's bit is below that register's width.
PORT_BITS = {"avr": 8, "cortex-m": 32, "esp32": 32}

# include/tfw/board.h, and each architecture's TFW_PORT_NONE (hal_types.h).
NONE_ADC = "TFW_ADC_NONE"
NONE_PWM = ("TFW_PWM_NONE", "TFW_PWM_RSVD")
NONE_PORT = "TFW_PORT_NONE"

BOARD_FILES = ("board_profile.h", "board.c", "profile.mk")
REGISTRY = os.path.join("docs", "boards", "board-id-registry.md")
GO_MIRROR = os.path.join("sdk", "go", "boardid.go")
BOOT_MK = os.path.join("mk", "boot.mk")

BOOT_GEOMETRY = ("BOOT_SECTION", "BOOT_BASE", "BOOT_BUDGET", "RAM_END")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def read_mk(path):
    """The simple assignments of a makefile fragment: NAME := value."""
    out = {}
    for line in read(path).splitlines():
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(?::=|\?=|=)\s*(.*?)\s*$",
                     line.split("#", 1)[0])
        if m and m.group(1) not in out:
            out[m.group(1)] = m.group(2)
    return out


def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def read_defines(path):
    """The object-like #defines of a header, comments removed."""
    out = {}
    for line in strip_c_comments(read(path)).splitlines():
        m = re.match(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(.*?))?\s*$", line)
        if m and m.group(1) not in out:
            out[m.group(1)] = m.group(2) or ""
    return out


_OPS = {
    ast.Add: lambda a, b: a + b, ast.Sub: lambda a, b: a - b,
    ast.Mult: lambda a, b: a * b, ast.FloorDiv: lambda a, b: a // b,
    ast.LShift: lambda a, b: a << b, ast.RShift: lambda a, b: a >> b,
    ast.BitOr: lambda a, b: a | b, ast.BitAnd: lambda a, b: a & b,
}


def eval_c(expr, defines, depth=0):
    """An integer constant expression of literals and other defines."""
    if depth > 16:
        raise ValueError("defines nest too deep: %s" % expr)

    def name(m):
        word = m.group(0)
        if word not in defines:
            raise ValueError("%s is not defined" % word)
        return "(%d)" % eval_c(defines[word], defines, depth + 1)

    text = re.sub(r"\b(0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]+\b", r"\1", expr)
    text = re.sub(r"\b[A-Za-z_][A-Za-z0-9_]*\b", name, text)
    text = text.replace("/", "//")
    return _eval_node(ast.parse(text.strip(), mode="eval").body, expr)


def _eval_node(node, expr):
    if isinstance(node, ast.Constant) and isinstance(node.value, int):
        return node.value
    if isinstance(node, ast.BinOp) and type(node.op) in _OPS:
        return _OPS[type(node.op)](_eval_node(node.left, expr), _eval_node(node.right, expr))
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
        v = _eval_node(node.operand, expr)
        return -v if isinstance(node.op, ast.USub) else v
    raise ValueError("not an integer constant expression: %s" % expr)


def table_rows(source, table):
    """The top-level initialiser rows of `table`, each as its inner text.

    A table declared without an initialiser has no rows (the ATtiny85's
    tfw_board_pwms). None when the table is not declared at all."""
    m = re.search(r"\b%s\s*\[[^\]]*\][^;=]*(=\s*\{|;)" % re.escape(table), source)
    if not m:
        return None
    if m.group(1) == ";":
        return []
    rows, depth, start = [], 1, None
    for i in range(m.end(), len(source)):
        c = source[i]
        if c == "{":
            depth += 1
            if depth == 2:
                start = i + 1
        elif c == "}":
            depth -= 1
            if depth == 1:
                rows.append(source[start:i])
            elif depth == 0:
                return rows
    raise ValueError("%s: unterminated initialiser" % table)


PIN_ROW = re.compile(r"^\s*TFW_PB\(\s*([^,]+?)\s*,\s*([^)]+?)\s*\)\s*,"
                     r"\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*(.+?)\s*$", re.S)


def registry_rows(root):
    """(ranges, allocated) from the board-id registry's two tables.

    ranges: [(first, last, family)] for each family the Go `Family()` column
    names; allocated: [(id, directory)] in table order."""
    ranges, allocated, section = [], [], None
    for line in read(os.path.join(root, REGISTRY)).splitlines():
        if line.startswith("## "):
            section = line[3:].strip().lower()
            continue
        cells = [c.strip().strip("`") for c in line.strip().strip("|").split("|")]
        if section == "ranges" and len(cells) == 3:
            m = re.match(r"^(0x[0-9A-Fa-f]{2})`?-`?(0x[0-9A-Fa-f]{2})$", cells[0])
            if m and cells[2] != "unallocated":
                ranges.append((int(m.group(1), 16), int(m.group(2), 16), cells[2]))
        elif section == "allocated ids" and len(cells) == 3:
            if re.match(r"^0x[0-9A-Fa-f]{2}$", cells[0]):
                d = cells[2]
                allocated.append((int(cells[0], 16), d[len("boards/"):] if d.startswith("boards/") else d))
    return ranges, allocated


def go_mirror_rows(root):
    """[(id, directory)] from sdk/go/boardid.go's exported constants and names."""
    text = read(os.path.join(root, GO_MIRROR))
    ids = {n: int(v, 16) for n, v in re.findall(r"\b(Board[A-Z][A-Za-z0-9]*)\s+BoardID\s*=\s*(0x[0-9A-Fa-f]+)", text)}
    names = re.findall(r"\b(Board[A-Z][A-Za-z0-9]*)\s*:\s*\"([^\"]+)\"", text)
    rows, errors = [], []
    for const, d in names:
        if const not in ids:
            errors.append("%s: boardNames names %s, which is not a BoardID constant" % (GO_MIRROR, const))
            continue
        rows.append((ids[const], d))
    for const in sorted(set(ids) - {c for c, _ in names}):
        errors.append("%s: %s (0x%02X) has no boardNames entry" % (GO_MIRROR, const, ids[const]))
    return rows, errors


def boot_supported_boards(root):
    """The board of each BOOT_SUPPORTED row in mk/boot.mk."""
    m = re.search(r"^BOOT_SUPPORTED\s*:=((?:[^\n]*\\\n)*[^\n]*)", read(os.path.join(root, BOOT_MK)), re.M)
    if not m:
        return set()
    return {row.split(":", 1)[0] for row in m.group(1).replace("\\", " ").split()}


def descriptor_test(root, board):
    """board-sweep's name for a board's descriptor test (mk/host.mk)."""
    short = board[len("atmega"):] if board.startswith("atmega") else board
    for name in ("test_board_%s.c" % short, "test_board_%s.c" % board):
        if os.path.isfile(os.path.join(root, "test", "unit", name)):
            return name
    return None


def check_board(root, board, ranges, errors):
    """Check one board directory; return (id, arch) or None when unreadable."""
    def fail(msg):
        errors.append("boards/%s: %s" % (board, msg))

    bdir = os.path.join(root, "boards", board)
    missing = [f for f in BOARD_FILES if not os.path.isfile(os.path.join(bdir, f))]
    for f in missing:
        fail("no %s" % f)
    if descriptor_test(root, board) is None:
        fail("no descriptor test test/unit/test_board_<short>.c")
    if missing:
        return None

    mk = read_mk(os.path.join(bdir, "profile.mk"))
    defs = read_defines(os.path.join(bdir, "board_profile.h"))
    arch = mk.get("ARCH")
    if not arch:
        fail("profile.mk states no ARCH")
    elif not os.path.isfile(os.path.join(root, "mk", "arch", arch + ".mk")):
        fail("ARCH %s has no mk/arch/%s.mk" % (arch, arch))

    def value(name):
        if name not in defs:
            fail("board_profile.h defines no %s" % name)
            return None
        try:
            return eval_c(defs[name], defs)
        except (ValueError, SyntaxError) as e:
            fail("%s: %s" % (name, e))
            return None

    name = defs.get("TFW_BOARD_NAME", "").strip()
    if name != '"%s"' % board:
        fail("TFW_BOARD_NAME is %s, not \"%s\"" % (name or "undefined", board))

    board_id = value("TFW_BOARD_ID")
    if board_id is not None and arch:
        fams = [(lo, hi) for lo, hi, fam in ranges if arch == fam or arch in fam.split("/")]
        if not fams:
            fail("the registry has no id range for ARCH %s" % arch)
        elif not any(lo <= board_id <= hi for lo, hi in fams):
            fail("TFW_BOARD_ID 0x%02X is outside the %s range 0x%02X-0x%02X"
                 % (board_id, arch, fams[0][0], fams[0][1]))

    check_budgets(board, mk, defs, value, fail)
    check_pins(board, arch, bdir, defs, value, fail)
    return (board_id, arch) if board_id is not None else None


def mk_int(mk, name, fail):
    try:
        return int(mk[name], 0)
    except ValueError:
        fail("profile.mk %s is %r, not a number" % (name, mk[name]))
        return None


def check_budgets(board, mk, defs, value, fail):
    budgets = [b for b in ("FLASH_BUDGET", "RAM_BUDGET") if b in mk]
    for budget, total in (("FLASH_BUDGET", "FLASH_TOTAL"), ("RAM_BUDGET", "RAM_TOTAL")):
        if budget in mk:
            if total not in mk:
                fail("%s with no %s" % (budget, total))
                continue
            b, t = mk_int(mk, budget, fail), mk_int(mk, total, fail)
            if b is not None and t is not None and b > t:
                fail("%s %d exceeds %s %d" % (budget, b, total, t))
    if not budgets:
        return
    # An emulated board's part is the machine its emulator models.
    part = mk.get("MCU", mk.get("QEMU_MACHINE"))
    if part is None:
        fail("a budget names no part: profile.mk states no MCU or QEMU_MACHINE")
        return
    if part not in PARTS:
        fail("part %s is not one board-check knows: add its data sheet's numbers to PARTS" % part)
        return
    p = PARTS[part]
    for total, want in (("FLASH_TOTAL", p["flash"]), ("RAM_TOTAL", p["sram"])):
        got = mk_int(mk, total, fail) if total in mk else None
        if got is not None and got != want:
            fail("%s %d, but the %s has %d" % (total, got, part, want))
    flash_end = p.get("flash_start", 0) + p["flash"]
    for name, want in (("TFW_FLASH_TOTAL", flash_end), ("TFW_EEPROM_SIZE", p["eeprom"])):
        got = value(name)
        if got is not None and got != want:
            fail("%s %d, but the %s has %d" % (name, got, part, want))

    stated = [g for g in BOOT_GEOMETRY if g in mk]
    if not p["boot"]:
        tiers = mk.get("BOARD_TIERS", "soft atecc608a").split()
        if tiers != ["none"]:
            fail("the %s has no boot section, so BOARD_TIERS must be none, not %s"
                 % (part, " ".join(tiers)))
        for g in stated:
            fail("the %s has no boot section, but profile.mk states %s" % (part, g))
        return
    if "RAM_END" in mk:
        end = mk_int(mk, "RAM_END", fail)
        want = p["sram_start"] + p["sram"] - 1
        if end is not None and end != want:
            fail("RAM_END 0x%04X, but the %s's last SRAM address is 0x%04X" % (end, part, want))
    if "BOOT_SECTION" in mk and "BOOT_BASE" in mk:
        sect, base = mk_int(mk, "BOOT_SECTION", fail), mk_int(mk, "BOOT_BASE", fail)
        if sect is not None and base is not None:
            if "boot_end" in p:
                if base + sect != p["boot_end"]:
                    fail("BOOT_BASE 0x%X + BOOT_SECTION %d is not the end of the %s's boot region 0x%X"
                         % (base, sect, part, p["boot_end"]))
            elif base + sect != p["flash"]:
                fail("BOOT_BASE 0x%X + BOOT_SECTION %d is not the top of flash %d"
                     % (base, sect, p["flash"]))
            region = value("TFW_BOOT_REGION_BASE")
            if region is not None and region != base:
                fail("BOOT_BASE 0x%X, but TFW_BOOT_REGION_BASE is 0x%X" % (base, region))


def check_pins(board, arch, bdir, defs, value, fail):
    source = strip_c_comments(read(os.path.join(bdir, "board.c")))
    counts = {}
    for table, count in (("tfw_board_ports", "TFW_PORT_COUNT"),
                         ("tfw_board_pins", "TFW_PIN_COUNT"),
                         ("tfw_board_pwms", "TFW_PWM_COUNT")):
        counts[count] = value(count)
        try:
            rows = table_rows(source, table)
        except ValueError as e:
            fail("board.c: %s" % e)
            return
        if rows is None:
            fail("board.c declares no %s" % table)
            return
        if counts[count] is not None and len(rows) != counts[count]:
            fail("board.c: %s has %d rows, %s is %d" % (table, len(rows), count, counts[count]))
        if table == "tfw_board_pins":
            pins = rows
    adc_count = value("TFW_ADC_COUNT")
    bits = PORT_BITS.get(arch)
    if bits is None:
        fail("board-check knows no port width for ARCH %s: add it to PORT_BITS" % arch)

    seen_pb, seen_adc, seen_pwm = {}, {}, {}

    def number(text, what, pin):
        try:
            return eval_c(text, defs)
        except (ValueError, SyntaxError):
            fail("pin %d: %s %r is not a number" % (pin, what, text))
            return None

    for pin, row in enumerate(pins):
        m = PIN_ROW.match(row)
        if not m:
            fail("pin %d: not a { TFW_PB(port, bit), adc, pwm, flags } row" % pin)
            continue
        port, bit, adc, pwm, flags = m.groups()
        flags = set(re.findall(r"TFW_PIN_[A-Z0-9_]+", flags))
        if port != NONE_PORT:
            p, b = number(port, "port", pin), number(bit, "bit", pin)
            if p is not None and counts["TFW_PORT_COUNT"] is not None and p >= counts["TFW_PORT_COUNT"]:
                fail("pin %d: port %d, but TFW_PORT_COUNT is %d" % (pin, p, counts["TFW_PORT_COUNT"]))
            if b is not None and bits is not None and b >= bits:
                fail("pin %d: bit %d of a %d-bit %s port" % (pin, b, bits, arch))
            if (p, b) in seen_pb:
                fail("pin %d: port %s bit %s is pin %d's" % (pin, p, b, seen_pb[(p, b)]))
            seen_pb[(p, b)] = pin
        elif "TFW_PIN_DIGITAL" in flags:
            fail("pin %d: TFW_PIN_DIGITAL on a pin with no port" % pin)
        if adc != NONE_ADC:
            a = number(adc, "ADC channel", pin)
            if a is not None and adc_count is not None and a >= adc_count:
                fail("pin %d: ADC channel %d, but TFW_ADC_COUNT is %d" % (pin, a, adc_count))
            if a in seen_adc:
                fail("pin %d: ADC channel %s is pin %d's" % (pin, a, seen_adc[a]))
            seen_adc[a] = pin
        if (adc != NONE_ADC) != ("TFW_PIN_ADC" in flags):
            fail("pin %d: an ADC channel and the TFW_PIN_ADC flag must come together" % pin)
        if pwm not in NONE_PWM:
            u = number(pwm, "PWM unit", pin)
            if u is not None and counts["TFW_PWM_COUNT"] is not None and u >= counts["TFW_PWM_COUNT"]:
                fail("pin %d: PWM unit %d, but TFW_PWM_COUNT is %d" % (pin, u, counts["TFW_PWM_COUNT"]))
            if u in seen_pwm:
                fail("pin %d: PWM unit %s is pin %d's" % (pin, u, seen_pwm[u]))
            seen_pwm[u] = pin
        if (pwm not in NONE_PWM) != ("TFW_PIN_PWM" in flags):
            fail("pin %d: a PWM unit and the TFW_PIN_PWM flag must come together" % pin)
        if not flags & {"TFW_PIN_DIGITAL", "TFW_PIN_ADC"}:
            fail("pin %d: neither digital nor ADC" % pin)

    pin_count = counts["TFW_PIN_COUNT"]
    for name in sorted(defs):
        if re.match(r"^TFW_(?!ADDR_)[A-Z0-9_]*_PIN(_[A-Z0-9]+)?$", name) and pin_count is not None:
            n = value(name)
            if n is not None and n >= pin_count:
                fail("%s is pin %d, but TFW_PIN_COUNT is %d" % (name, n, pin_count))


def check(root):
    """Every failure under `root`, one line each; empty when the boards are right."""
    errors = []
    ranges, registry = registry_rows(root)
    go_rows, go_errors = go_mirror_rows(root)
    errors.extend(go_errors)

    boards_dir = os.path.join(root, "boards")
    boards = sorted(d for d in os.listdir(boards_dir) if os.path.isdir(os.path.join(boards_dir, d)))
    claimed = {}
    for board in boards:
        got = check_board(root, board, ranges, errors)
        if got is None:
            continue
        board_id = got[0]
        if board_id in claimed:
            errors.append("boards/%s: TFW_BOARD_ID 0x%02X is also boards/%s's: ids never repeat"
                          % (board, board_id, claimed[board_id]))
        else:
            claimed[board_id] = board

    for label, rows in ((REGISTRY, registry), (GO_MIRROR, go_rows)):
        by_id, by_dir = {}, {}
        for board_id, d in rows:
            if board_id in by_id:
                errors.append("%s: 0x%02X is allocated twice (%s, %s)" % (label, board_id, by_id[board_id], d))
            if d in by_dir:
                errors.append("%s: %s has two ids (0x%02X, 0x%02X)" % (label, d, by_dir[d], board_id))
            by_id.setdefault(board_id, d)
            by_dir.setdefault(d, board_id)
        for board_id, board in sorted(claimed.items()):
            if by_dir.get(board) != board_id:
                errors.append("boards/%s: TFW_BOARD_ID 0x%02X is not its row in %s (%s)"
                              % (board, board_id, label,
                                 "0x%02X" % by_dir[board] if board in by_dir else "no row"))
        for d, board_id in sorted(by_dir.items()):
            if d not in boards:
                errors.append("%s: 0x%02X names %s, which is not a directory under boards/" % (label, board_id, d))

    for board in sorted(boot_supported_boards(root)):
        mkpath = os.path.join(boards_dir, board, "profile.mk")
        if not os.path.isfile(mkpath):
            errors.append("%s: BOOT_SUPPORTED names %s, which is not a board" % (BOOT_MK, board))
        elif "BOOT_SECTION" not in read_mk(mkpath):
            errors.append("%s: BOOT_SUPPORTED names %s, which states no BOOT_SECTION" % (BOOT_MK, board))
    return errors


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    errors = check(root)
    for e in errors:
        print("board-check: %s" % e)
    if errors:
        print("board-check: %d failure(s); docs/boards/adding-a-board.md says what each value must be" % len(errors))
        return 1
    print("board-check: every board is registered, unique and in range")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
