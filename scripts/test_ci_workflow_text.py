#!/usr/bin/env python3
"""The scripts tests run on the standard library alone (#613, Codex round 5).

scripts-test runs in the toolchain image, whose Python has no PyYAML
(docker/packages.txt installs no python3-yaml). Every scripts/test_*.py
imports only standard-library modules and the repository's own scripts.
The CI tests read a workflow with load() below, as text, the way the
`Runner labels (ruling 633)` step does: block mappings and sequences by
indentation, plain, quoted and `|`/`>` scalars. A flow collection stays its
text, and every scalar is a string (`false` is "false").

Run: python3 scripts/test_ci_workflow_text.py
"""

import ast
import glob
import json
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
KEY = re.compile(r"(['\"]?)([^\s'\"#:][^:#]*?)\1\s*:(?:\s+(.*))?$")


def strip_comment(s):
    q = None
    for i, c in enumerate(s):
        if q:
            if c == q:
                q = None
        elif c in "'\"" and (i == 0 or s[i - 1] in " \t[{,:"):
            q = c
        elif c == "#" and (i == 0 or s[i - 1] in " \t"):
            return s[:i].rstrip()
    return s.rstrip()


def ind(s):
    return len(s) - len(s.lstrip(" "))


def blank(s):
    t = s.strip()
    return not t or t.startswith("#")


def scalar(t):
    t = t.strip()
    if len(t) >= 2 and t[0] == t[-1] == "'":
        return t[1:-1].replace("''", "'")
    if len(t) >= 2 and t[0] == t[-1] == '"':
        return json.loads(t)
    return t


def nonblank(lines, i):
    while i < len(lines) and blank(lines[i]):
        i += 1
    return i


def block_scalar(lines, i, base, style):
    """The `|` or `>` scalar whose lines follow line i - 1, indented past base."""
    out, j = [], i
    while j < len(lines) and (not lines[j].strip() or ind(lines[j]) > base):
        out.append(lines[j])
        j += 1
    while out and not out[-1].strip():
        out.pop()
    cut = min((ind(s) for s in out if s.strip()), default=0)
    out = [s[cut:] for s in out]
    text = (" " if style.startswith(">") else "\n").join(out)
    return (text if style.endswith("-") or not out else text + "\n"), j


def value(lines, i, base, inline):
    """The value of the key on line i - 1 (indent base), whose inline text is inline."""
    inline = strip_comment(inline or "").strip()
    if inline[:1] in ("|", ">"):
        return block_scalar(lines, i, base, inline)
    j = nonblank(lines, i)
    if not inline and j < len(lines) and (ind(lines[j]) > base and KEY.match(lines[j].lstrip())
                                          or ind(lines[j]) >= base and lines[j].lstrip()[:2] in ("- ", "-")):
        return node(lines, j, ind(lines[j]))
    parts, j = [inline] if inline else [], i
    while j < len(lines) and (blank(lines[j]) or ind(lines[j]) > base):
        if not blank(lines[j]):
            parts.append(strip_comment(lines[j]).strip())
        j += 1
    return (scalar(" ".join(parts)) if parts else None), j


def node(lines, i, n):
    """The mapping or sequence whose first line, i, sits at indent n."""
    if lines[i][n:].startswith("- ") or lines[i][n:].rstrip() == "-":
        out = []
        while i < len(lines) and ind(lines[i]) == n and lines[i][n:n + 1] == "-":
            rest = lines[i][n + 1:].lstrip(" ")
            if KEY.match(rest):
                lines[i] = " " * (len(lines[i]) - len(rest)) + rest
                v, i = node(lines, i, ind(lines[i]))
            else:
                v, i = value(lines, i + 1, n, rest)
            out.append(v)
            i = nonblank(lines, i)
        return out, i
    out = {}
    while i < len(lines) and ind(lines[i]) == n:
        m = KEY.match(lines[i][n:])
        if not m:
            raise ValueError("line %d: not a key: %r" % (i + 1, lines[i]))
        out[m.group(2)], i = value(lines, i + 1, n, m.group(3))
        i = nonblank(lines, i)
    return out, i


def load(path):
    """The workflow at path as dicts, lists and strings, read as text."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    i = nonblank(lines, 0)
    return node(lines, i, ind(lines[i]))[0] if i < len(lines) else {}


class StandardLibraryOnly(unittest.TestCase):
    def test_tests_import_only_stdlib_and_scripts(self):
        own = {os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(HERE, "*.py"))}
        own |= {os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(HERE, "*", "__init__.py"))}
        tests = sorted(glob.glob(os.path.join(HERE, "test_*.py")))
        self.assertTrue(tests)
        for path in tests:
            with open(path, encoding="utf-8") as f:
                tree = ast.parse(f.read(), path)
            names = set()
            for n in ast.walk(tree):
                if isinstance(n, ast.Import):
                    names |= {a.name.split(".")[0] for a in n.names}
                elif isinstance(n, ast.ImportFrom) and n.level == 0:
                    names.add(n.module.split(".")[0])
            with self.subTest(test=os.path.basename(path)):
                self.assertEqual(sorted(names - set(sys.stdlib_module_names) - own), [])


if __name__ == "__main__":
    unittest.main()
