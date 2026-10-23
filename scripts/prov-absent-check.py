#!/usr/bin/env python3
"""prov-absent-check: no production image's object list carries provisioning.

Row device-provisioning (docs/SPEC.md): provisioning commands are absent from
every production image. The gate is file selection -- mk/firmware.mk adds
src/caps/provision.c and its backend to the object list only for PROVISION=1,
and src/core/commands.def emits the 0x70-0x75 entries only under
TFW_FEATURE_PROVISION -- so this check reads the object lists the build links,
asked of make itself, and holds every one of them to that:

  - every board, every tier it builds, and every extra row of
    firmware-matrix-list; as the board defaults it and, where the board names
    a controller, with ETH=0 and ETH=1; and the example application on each
    board;
  - a production image (PROVISION=0) links no provisioning translation unit
    and compiles with no TFW_FEATURE_PROVISION define;
  - a jig image (PROVISION=1) of the same row links src/caps/provision.c and
    defines TFW_FEATURE_PROVISION=1. Otherwise the patterns below no longer
    name what a jig links, and a pass would have checked nothing;
  - the boot image of every row links no provisioning unit, jig or not, and
    its list holds boot/boot_main.c, or the probe read the wrong variable;
  - the bench bake image of every board (platform-bake-srcs) links no
    provisioning unit and no element or crypto-provider unit, and its list
    holds tools/platform-bake/bake.c. It sets the serial, and no other tool's
    step is within its reach (R5).

Nothing is compiled and no toolchain is needed: firmware-srcs and feature-defs
print, and the boot list is read through --eval. Run it from the repository
root; scripts/test_prov_absent_check.py runs it under `make scripts-test`.
docs/device-provisioning/engineering.md is the page.
"""

import concurrent.futures
import fnmatch
import os
import subprocess
import sys

# The provisioning translation units: the wire, the element's backend and any
# backend after it (src/caps/provision_<x>.c), and an architecture's own half
# (src/hal/esp32/prov_uds.c). A pattern, so a new backend is caught the day it
# is added, not the day someone remembers to list it.
PROV_PATTERNS = ("src/caps/provision.c", "src/caps/provision_*.c", "src/hal/*/prov_*.c")
PROV_WIRE = "src/caps/provision.c"
PROV_DEF = "TFW_FEATURE_PROVISION"
PROV_DEF_ON = "-DTFW_FEATURE_PROVISION=1"
BOOT_MAIN = "boot/boot_main.c"
APP_EXAMPLE = "test/app/example"

# The bench bake image (mk/platform-bake.mk) writes the serial and nothing
# else, so it links no provisioning unit and none of these: a crypto tier's
# provider or the element's driver, an architecture's own provider, any other
# element code, and the bus the element sits on. Then no step of the bake image
# can reach the element's or the root's, nor undo one.
ELEMENT_PATTERNS = ("src/crypto/*/*.c", "src/crypto/deviceid_*.c", "src/hal/*/crypto_*.c",
                    "src/*/*atecc*.c", "src/hal/*/hal_bus.c")
BAKE_MAIN = "tools/platform-bake/bake.c"

BOOT_PROBE = "tfw-prov-absent-boot"
BOOT_EVAL = BOOT_PROBE + ": ; @echo $(BOOT_SRCS)"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def prov_objects(srcs):
    """The provisioning translation units in one object list."""
    return sorted(s for s in srcs if any(fnmatch.fnmatchcase(s, p) for p in PROV_PATTERNS))


def prov_defs(defs):
    """The -D flags in one image's feature defines that name TFW_FEATURE_PROVISION."""
    return sorted(d for d in defs if PROV_DEF in d)


def check(images):
    """Return one failure line per broken rule; an empty list is a pass.

    images is a list of (label, provision, srcs, defs, boot_srcs): label names
    the make variables, provision is 0 or 1, the rest are lists of words.
    """
    fails = []
    seen = {0: 0, 1: 0}
    for label, provision, srcs, defs, boot in images:
        seen[provision] += 1
        if not srcs:
            fails.append(f"{label}: the object list is empty")
        if BOOT_MAIN not in boot:
            fails.append(f"{label}: the boot list lacks {BOOT_MAIN} -- the probe read nothing")
        for s in prov_objects(boot):
            fails.append(f"{label}: the boot image links {s}")
        if provision == 0:
            for s in prov_objects(srcs):
                fails.append(f"{label}: a production image links {s}")
            for d in prov_defs(defs):
                fails.append(f"{label}: a production image compiles with {d}")
        else:
            if PROV_WIRE not in srcs:
                fails.append(f"{label}: the jig image lacks {PROV_WIRE} -- the check would be vacuous")
            if PROV_DEF_ON not in defs:
                fails.append(f"{label}: the jig image lacks {PROV_DEF_ON} -- the check would be vacuous")
    if seen[0] == 0:
        fails.append("no production image was listed")
    if seen[1] == 0:
        fails.append("no jig image was listed")
    return fails


def bake_objects(srcs):
    """The provisioning and element translation units in a bake image's list."""
    pats = PROV_PATTERNS + ELEMENT_PATTERNS
    return sorted(s for s in srcs if any(fnmatch.fnmatchcase(s, p) for p in pats))


def check_bake(bakes):
    """Return one failure line per broken rule for the bake images; an empty
    list is a pass. bakes is a list of (label, srcs)."""
    fails = []
    for label, srcs in bakes:
        if BAKE_MAIN not in srcs:
            fails.append(f"{label}: the bake image's list lacks {BAKE_MAIN} -- the probe read nothing")
        for s in bake_objects(srcs):
            fails.append(f"{label}: the bake image links {s}")
    if not bakes:
        fails.append("no bake image was listed")
    return fails


def plan(boards, matrix_rows, unprovisioned=frozenset()):
    """The make variable sets to ask about, one list of words each.

    boards maps a board to (tiers, eth_chip). matrix_rows are the words of each
    firmware-matrix-list line: "board tier [VAR=value ...]". A row that sets
    its own variables keeps them and varies PROVISION only; a plain board and
    tier is asked as the board defaults it and, where the board names a
    controller, with ETH=0 and ETH=1 as well.
    The example application goes on each board's first tier.
    unprovisioned holds the (board, tier) pairs the build says have no
    provisioning backend (provision-backend prints "none"): their PROVISION=1
    is an $(error), so they are asked as production images only -- which is
    the rule that matters on them.
    """
    bases = []
    for board in sorted(boards):
        tiers, chip = boards[board]
        eths = [[]] + ([["ETH=0"], ["ETH=1"]] if chip else [])
        for tier in tiers:
            for eth in eths:
                bases.append([f"BOARD={board}", f"CRYPTO={tier}"] + eth)
        if tiers:
            bases.append([f"BOARD={board}", f"CRYPTO={tiers[0]}", f"APP_DIR={APP_EXAMPLE}"])
    for row in matrix_rows:
        if len(row) > 2:
            bases.append([f"BOARD={row[0]}", f"CRYPTO={row[1]}"] + row[2:])
    out = []
    for base in bases:
        pair = (base[0].split("=", 1)[1], base[1].split("=", 1)[1])
        for p in ((0,) if pair in unprovisioned else (0, 1)):
            out.append(base + [f"PROVISION={p}"])
    return out


def unprovisioned_pairs(pairs, root=ROOT):
    """The (board, tier) pairs among pairs whose build has no provisioning
    backend, asked of make's provision-backend rather than listed here."""
    return frozenset(p for p in sorted(set(pairs))
                     if make(["provision-backend", f"BOARD={p[0]}", f"CRYPTO={p[1]}"], root).strip() == "none")


def make_env():
    """The environment without a parent make's flags: under `make -j ci` they
    would carry -j, and the three goals' lines could interleave."""
    env = dict(os.environ)
    for k in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "MAKELEVEL"):
        env.pop(k, None)
    return env


def make(args, root=ROOT):
    out = subprocess.run(["make", "-s", "--no-print-directory", "-j1", "-C", root] + args,
                         env=make_env(), capture_output=True, text=True, check=False)
    if out.returncode != 0:
        raise RuntimeError(f"make {' '.join(args)}: rc {out.returncode}: {out.stderr.strip()[-400:]}")
    return out.stdout


def ask(variables, root=ROOT):
    """One image: (label, provision, srcs, defs, boot_srcs), asked of make."""
    out = make([f"--eval={BOOT_EVAL}", "firmware-srcs", "feature-defs", BOOT_PROBE] + variables, root)
    lines = out.split("\n")
    if len(lines) != 4 or lines[3] != "":
        raise RuntimeError(f"make {' '.join(variables)}: expected 3 lines, got {len(lines) - 1}")
    provision = 1 if "PROVISION=1" in variables else 0
    return (" ".join(variables), provision, lines[0].split(), lines[1].split(), lines[2].split())


def sweep(root=ROOT):
    boards = {}
    for board in sorted(os.listdir(os.path.join(root, "boards"))):
        if not os.path.isfile(os.path.join(root, "boards", board, "profile.mk")):
            continue
        tiers = make(["board-tiers", f"BOARD={board}"], root).split()
        chip = make(["board-eth-chip", f"BOARD={board}"], root).strip()
        boards[board] = (tiers, chip)
    rows = [line.split() for line in make(["firmware-matrix-list"], root).splitlines() if line.strip()]
    pairs = [(b, t) for b in boards for t in boards[b][0]] + [(r[0], r[1]) for r in rows if len(r) > 1]
    none = unprovisioned_pairs(pairs, root)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        return list(pool.map(lambda v: ask(v, root), plan(boards, rows, none)))


def ask_bake(variables, root=ROOT):
    """One bake image: (label, srcs), the list platform-bake links."""
    return ("platform-bake " + " ".join(variables), make(["platform-bake-srcs"] + variables, root).split())


def sweep_bake(root=ROOT):
    boards = [b for b in sorted(os.listdir(os.path.join(root, "boards")))
              if os.path.isfile(os.path.join(root, "boards", b, "profile.mk"))]
    return [ask_bake([f"BOARD={b}"], root) for b in boards]


def main():
    images = sweep()
    bakes = sweep_bake()
    fails = check(images) + check_bake(bakes)
    for f in fails:
        print(f"prov-absent-check: {f}")
    if fails:
        return 1
    prod = sum(1 for i in images if i[1] == 0)
    print(f"prov-absent-check: {prod} production object lists, none links a provisioning unit; "
          f"{len(bakes)} bake image lists, none links an element or provisioning unit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
