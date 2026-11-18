#!/usr/bin/env python3
"""Check a vendored tree against its SHA256SUMS.

    vendor_sums.py <dir>

<dir>/SHA256SUMS lists every vendored file of <dir> (sha256sum format, paths
relative to <dir>), summed from a fresh fetch of the upstream commit its
VERSION names. The check fails on a file whose bytes differ, a listed file
that is missing, and a file in <dir> the list does not name (VERSION, README.tfw.md and
SHA256SUMS themselves aside). third_party/lwip is checked by make lwip-check
and by scripts-test (test_vendor_sums.py).
"""

import hashlib
import os
import sys

SUMS = "SHA256SUMS"
OURS = {SUMS, "VERSION", "README.tfw.md"}  # ours, not upstream's


def read_sums(path):
    """Return {relative path: hex digest} from a sha256sum file."""
    sums = {}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            digest, sep, name = line.partition("  ")
            if not sep or len(digest) != 64:
                raise ValueError(f"{path}:{n}: not a sha256sum line")
            sums[name.removeprefix("./")] = digest
    return sums


def tree_files(root):
    """Return the relative paths of every file under root but OURS."""
    out = set()
    for d, _, files in os.walk(root):
        for name in files:
            rel = os.path.relpath(os.path.join(d, name), root)
            if rel not in OURS:
                out.add(rel.replace(os.sep, "/"))
    return out


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def check(root):
    """Return the list of problems; empty when the tree matches."""
    sums = read_sums(os.path.join(root, SUMS))
    files = tree_files(root)
    problems = [f"missing: {n}" for n in sorted(set(sums) - files)]
    problems += [f"not in {SUMS}: {n}" for n in sorted(files - set(sums))]
    for n in sorted(files & set(sums)):
        if sha256(os.path.join(root, n)) != sums[n]:
            problems.append(f"differs from upstream: {n}")
    return problems


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    root = argv[1]
    try:
        problems = check(root)
    except (OSError, ValueError) as e:
        print(f"vendor-sums: {e}", file=sys.stderr)
        return 1
    for p in problems:
        print(f"vendor-sums: {root}: {p}", file=sys.stderr)
    if problems:
        return 1
    print(f"vendor-sums: {root}: every file matches {SUMS}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
