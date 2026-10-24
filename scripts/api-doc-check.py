#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""api-doc-check: docs/host-sdk/api.md names exactly what sdk/go exports.

    python3 scripts/api-doc-check.py [ROOT]

The page's `## Exported symbols` section is a table per public package whose
first column is a symbol in backticks: `pkg.Name` for a function, type,
constant or variable, `pkg.Type.Method` for a method. This reads every public
package under sdk/go (every directory not under internal/ or testdata/ whose
package is not main) and fails, naming the symbol, when

  - an exported function, type, method, constant or variable is not a row;
  - a row, or a `pkg.Name` or `pkg.Name(...)` anywhere on the page, names a
    symbol that does not exist;
  - a symbol has two rows.

Every non-test .go file counts, whatever its build constraint: a symbol behind
the tfwsim or codec_json tag, or for one GOOS, is still part of the API a
consumer can build. Methods count when their receiver type is exported.
"""

import os
import re
import sys

PAGE = "docs/host-sdk/api.md"
SDK = "sdk/go"
SECTION = "## Exported symbols"

IDENT = r"[A-Za-z_][A-Za-z0-9_]*"
FUNC_RE = re.compile(r"func\s+(%s)\s*[\[(]" % IDENT)
METHOD_RE = re.compile(r"func\s*\(\s*(?:%s\s+)?\*?\s*(%s)\s*(?:\[[^\]]*\])?\s*\)\s*(%s)\s*[\[(]" % (IDENT, IDENT, IDENT))
DECL_RE = re.compile(r"(const|var|type)\s+(\(|%s(?:\s*,\s*%s)*)" % (IDENT, IDENT))
ENTRY_RE = re.compile(r"\s+(%s(?:\s*,\s*%s)*)" % (IDENT, IDENT))
PACKAGE_RE = re.compile(r"package\s+(%s)" % IDENT)
ROW_RE = re.compile(r"\|\s*`[^`]+`")
NAME_RE = re.compile(r"`([^`]+)`")
REF_RE = re.compile(r"`(%s)\.(%s)[`(]" % (IDENT, IDENT))


def strip(src):
    """Blank comments and the inside of string and rune literals, keeping
    newlines, so a brace or a `func` in either is not read as code."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * src.count("\n", i, j))
            i = j
        elif c == "`":
            j = src.find("`", i + 1)
            j = n if j < 0 else j + 1
            out.append('""' + "\n" * src.count("\n", i, j))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def exported(name):
    return name[:1].isupper()


def parse(src):
    """The package name and the exported symbols of one Go file, as
    (name, receiver-or-None) pairs; methods are kept whatever their receiver,
    so the caller can drop those on an unexported type."""
    text = strip(src)
    m = PACKAGE_RE.search(text)
    package = m.group(1) if m else None
    symbols = []
    depth = 0
    block = None
    for line in text.split("\n"):
        start = depth
        depth += sum(line.count(c) for c in "([{") - sum(line.count(c) for c in ")]}")
        if start == 0:
            block = None
            m = METHOD_RE.match(line)
            if m:
                if exported(m.group(2)):
                    symbols.append((m.group(2), m.group(1)))
                continue
            m = FUNC_RE.match(line)
            if m:
                if exported(m.group(1)):
                    symbols.append((m.group(1), None))
                continue
            m = DECL_RE.match(line)
            if m:
                if m.group(2) == "(":
                    block = m.group(1)
                else:
                    names = [s.strip() for s in m.group(2).split(",")]
                    if m.group(1) == "type":
                        names = names[:1]
                    symbols.extend((s, None) for s in names if exported(s))
        elif start == 1 and block:
            m = ENTRY_RE.match(line)
            if m:
                names = [s.strip() for s in m.group(1).split(",")]
                if block == "type":
                    names = names[:1]
                symbols.extend((s, None) for s in names if exported(s))
    return package, symbols


def public_packages(root):
    """{package name: sorted directory} for every public package of sdk/go."""
    found = {}
    base = os.path.join(root, SDK)
    for d, dirs, files in os.walk(base):
        dirs[:] = sorted(x for x in dirs if x not in ("internal", "testdata") and not x.startswith((".", "_")))
        for f in sorted(files):
            if f.endswith(".go") and not f.endswith("_test.go"):
                with open(os.path.join(d, f), encoding="utf-8") as fh:
                    package, _ = parse(fh.read())
                if package and package != "main":
                    found[package] = d
                break
    return found


def exports(root):
    """Every exported symbol of sdk/go's public packages, as `pkg.Name` and
    `pkg.Type.Method` keys."""
    keys = set()
    for package, d in public_packages(root).items():
        methods = []
        for f in sorted(os.listdir(d)):
            if not f.endswith(".go") or f.endswith("_test.go"):
                continue
            with open(os.path.join(d, f), encoding="utf-8") as fh:
                _, symbols = parse(fh.read())
            for name, recv in symbols:
                if recv is None:
                    keys.add("%s.%s" % (package, name))
                else:
                    methods.append((recv, name))
        for recv, name in methods:
            if exported(recv):
                keys.add("%s.%s.%s" % (package, recv, name))
    return keys


def page_names(text):
    """The first-column symbols of the Exported symbols section, in order, and
    every `pkg.Name` the page mentions anywhere."""
    rows = []
    inside = False
    for line in text.split("\n"):
        if line.startswith("## "):
            inside = line.strip() == SECTION
            continue
        if inside and ROW_RE.match(line):
            rows.extend(NAME_RE.findall(line.split("|")[1]))
    refs = set()
    for m in REF_RE.finditer(text):
        refs.add("%s.%s" % m.groups())
    return rows, refs


def check(root):
    errors = []
    try:
        with open(os.path.join(root, PAGE), encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        return ["%s: %s" % (PAGE, e.strerror)]
    if SECTION not in text.split("\n"):
        return ["%s: no %r section" % (PAGE, SECTION)]
    have = exports(root)
    packages = set(public_packages(root))
    rows, refs = page_names(text)
    seen = set()
    for r in rows:
        if r in seen:
            errors.append("%s: %s has two rows" % (PAGE, r))
        seen.add(r)
    for key in sorted(have - seen):
        errors.append("%s: %s is exported and has no row" % (PAGE, key))
    for key in sorted((seen | {r for r in refs if r.split(".")[0] in packages}) - have):
        errors.append("%s: %s is named and not exported" % (PAGE, key))
    return errors


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    errors = check(root)
    for e in errors:
        print("api-doc-check: %s" % e)
    if errors:
        print("api-doc-check: %d failure(s); a row is `pkg.Name` or `pkg.Type.Method` with its signature and doc comment" % len(errors))
        return 1
    print("api-doc-check: %s names every exported symbol of %s and nothing else" % (PAGE, SDK))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
