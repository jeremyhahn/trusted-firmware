#!/usr/bin/env python3
"""govulncheck-gate: govulncheck's JSON output held to a list of reviewed findings.

Reads `govulncheck -format json` from stdin (in JSON mode govulncheck exits 0
whatever it finds) and fails when a vulnerability is reachable from the
module's code -- a finding whose trace names a function -- and
scripts/govulncheck-allow.txt has no entry for it under the module named by
the first argument. An entry is `<module dir> <OSV id> <reason>`, one a line,
and the reason is required: what the vulnerability is, why it stays, the path
govulncheck reports and what removes the entry. An entry with no reachable
finding behind it is reported, so a stale one is seen and deleted. A finding
in a module or package the code imports but never calls is printed and does
not fail, as in govulncheck's text mode. mk/stages.mk pipes xkms-security into
it; scripts/test_govulncheck_gate.py runs it under `make scripts-test`.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ALLOW = os.path.join(HERE, "govulncheck-allow.txt")


def read_allow(path, module):
    """The {OSV id: reason} entries of `path` for `module`."""
    allow = {}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 2)
            if len(parts) < 3:
                raise ValueError(f"{path}:{n}: an entry is <module dir> <OSV id> <reason>")
            if parts[0] == module:
                allow[parts[1]] = parts[2]
    return allow


def messages(text):
    """The JSON objects of govulncheck's stream, which is not one per line."""
    dec = json.JSONDecoder()
    i = 0
    while True:
        while i < len(text) and text[i].isspace():
            i += 1
        if i >= len(text):
            return
        obj, i = dec.raw_decode(text, i)
        yield obj


def frame(f):
    recv = f.get("receiver", "").lstrip("*")
    name = f"{recv}.{f['function']}" if recv else f["function"]
    pos = f.get("position") or {}
    where = f" ({os.path.basename(pos['filename'])}:{pos.get('line', 0)})" if pos.get("filename") else ""
    return f"{f.get('package', f.get('module', ''))}.{name}{where}"


def gate(text, module, allow):
    """(exit code, report lines) for govulncheck's JSON `text`."""
    msgs = list(messages(text))
    if not any("config" in m for m in msgs):
        return 1, ["FAIL no govulncheck output: did it run?"]
    reached, imported = {}, set()
    for m in msgs:
        f = m.get("finding")
        if not f:
            continue
        trace = f.get("trace") or []
        if trace and trace[0].get("function"):
            reached.setdefault(f["osv"], trace)
        else:
            imported.add(f["osv"])
    rc, out = 0, []
    for osv in sorted(reached):
        path = " -> ".join(frame(f) for f in reversed(reached[osv]))
        if osv in allow:
            out.append(f"allowed {osv}: {path}")
            out.append(f"  reason: {allow[osv]}")
        else:
            rc = 1
            out.append(f"FAIL {osv} is reachable and not in scripts/govulncheck-allow.txt for {module}: {path}")
    for osv in sorted(imported - set(reached)):
        out.append(f"imported, not called: {osv}")
    for osv in sorted(set(allow) - set(reached)):
        out.append(f"stale entry: {module} {osv} has no reachable finding; delete it")
    return rc, out


def main(argv):
    if len(argv) != 2:
        print("usage: govulncheck -format json ./... | govulncheck-gate.py <module dir>", file=sys.stderr)
        return 2
    rc, out = gate(sys.stdin.read(), argv[1], read_allow(ALLOW, argv[1]))
    print("\n".join(out))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
