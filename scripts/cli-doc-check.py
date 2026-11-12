#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""cli-doc-check: docs/host-sdk/cli.md lists exactly the verbs and flags tfwctl has.

    python3 scripts/cli-doc-check.py [ROOT]

The page's `tfwctl` section has a `### Commands` table whose first column is
a verb in backticks (`ping`, or `netcfg set` for a verb with a sub-verb) and
a `### Flags` table whose first column is a flag (`-baud`). This reads every
non-test .go file of sdk/go/cmd/tfwctl and fails, naming the verb or flag,
when

  - a verb a `switch cmd {` dispatches (in main.go's run, or a file it hands
    the verb to: peripherals.go, hsm.go, ...), or one main compares
    `flag.Arg(0)` against (`ports`), has no row;
  - a row names a verb tfwctl does not dispatch;
  - a flag main.go defines with `flag.<Type>("name", ...)` has no row, or a
    row names a flag it does not define.
"""

import os
import re
import sys

PAGE = "docs/host-sdk/cli.md"
CMD = "sdk/go/cmd/tfwctl"
SECTION = "## `tfwctl`"
COMMANDS = "### Commands"
FLAGS = "### Flags"

SWITCH_RE = re.compile(r"^(\s*)switch cmd \{\s*$")
CASE_RE = re.compile(r'^(\s*)case\s+("[^"]*"(?:\s*,\s*"[^"]*")*)\s*:')
STRING_RE = re.compile(r'"([^"]*)"')
ARG0_RE = re.compile(r'flag\.Arg\(0\)\s*==\s*"([^"]+)"')
FLAG_RE = re.compile(r'\bflag\.(?:Bool|Int|Int64|Uint|Uint64|String|Duration|Float64|Func|Var|BoolVar|IntVar|StringVar|DurationVar)\(\s*(?:[^,()"]+,\s*)?"([^"]+)"')
ROW_RE = re.compile(r"\|\s*`[^`]+`")
NAME_RE = re.compile(r"`([^`]+)`")


def dispatched(src):
    """The verbs one Go file dispatches: the string cases of every
    `switch cmd {` at that switch's own depth, so a nested switch on an
    argument (`in`, `out`, `pullup`) is not read as a verb, and the verbs
    main tests `flag.Arg(0)` against before it opens a port."""
    verbs = set(ARG0_RE.findall(src))
    indent = None
    for line in src.split("\n"):
        if indent is None:
            m = SWITCH_RE.match(line)
            if m:
                indent = m.group(1)
            continue
        if line == indent + "}":
            indent = None
            continue
        m = CASE_RE.match(line)
        if m and m.group(1) == indent:
            verbs.update(STRING_RE.findall(m.group(2)))
    return verbs


def go_files(root):
    d = os.path.join(root, CMD)
    for f in sorted(os.listdir(d)):
        if f.endswith(".go") and not f.endswith("_test.go"):
            with open(os.path.join(d, f), encoding="utf-8") as fh:
                yield f, fh.read()


def verbs(root):
    """Every verb tfwctl dispatches."""
    found = set()
    for _, src in go_files(root):
        found |= dispatched(src)
    return found


def flags(root):
    """Every flag main.go defines on the default flag set, as `-name`."""
    with open(os.path.join(root, CMD, "main.go"), encoding="utf-8") as fh:
        return {"-" + name for name in FLAG_RE.findall(fh.read())}


def in_section(line):
    """Whether a line is the tfwctl section's heading, whatever follows the
    command's name on it."""
    return line == SECTION or line.startswith(SECTION + " ")


def page_rows(text):
    """{subsection: [first-column names]} for the Commands and Flags tables
    of the tfwctl section; a Commands row keeps its verb, the first word."""
    rows = {COMMANDS: [], FLAGS: []}
    inside, sub = False, None
    for line in text.split("\n"):
        if line.startswith("## "):
            inside, sub = in_section(line), None
            continue
        if not inside:
            continue
        if line.startswith("### "):
            sub = line.strip() if line.strip() in rows else None
            continue
        if sub and ROW_RE.match(line):
            for name in NAME_RE.findall(line.split("|")[1]):
                rows[sub].append(name.split()[0] if sub == COMMANDS else name)
    return rows


def check(root):
    try:
        with open(os.path.join(root, PAGE), encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        return ["%s: %s" % (PAGE, e.strerror)]
    if not any(in_section(line) for line in text.split("\n")):
        return ["%s: no %r section" % (PAGE, SECTION)]
    rows = page_rows(text)
    errors = []
    for sub, have, what in ((COMMANDS, verbs(root), "verb"), (FLAGS, flags(root), "flag")):
        if not rows[sub]:
            errors.append("%s: no %r table in the %s section" % (PAGE, sub, SECTION))
            continue
        seen = set(rows[sub])
        for name in sorted(have - seen):
            errors.append("%s: tfwctl %s %s has no row in %s" % (PAGE, what, name, sub))
        for name in sorted(seen - have):
            errors.append("%s: %s %s is not a tfwctl %s" % (PAGE, sub, name, what))
    return errors


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    errors = check(root)
    for e in errors:
        print("cli-doc-check: %s" % e)
    if errors:
        print("cli-doc-check: %d failure(s); a row is the verb or flag in backticks, its args and what it prints" % len(errors))
        return 1
    print("cli-doc-check: %s lists every verb and flag of %s and nothing else" % (PAGE, CMD))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
