#!/usr/bin/env python3
"""Byte-compare the vendored lwIP against an independently fetched upstream.

    lwip_upstream_check.py <vendored dir> <upstream url> <commit>

Fetches <commit> from <upstream url> into a scratch repository and checks
that the fetched commit id is <commit>. Then it checks that every file under
<vendored dir>/src, plus COPYING and README, is byte-identical to the same
path in that commit. SHA256SUMS in the vendored dir is written by the same
change that vendors the files, so it cannot vouch for them; this check can
(#584, Codex round 3).

Exit 0 with "PASS" when every file matches. Exit 1 with "FAIL" naming each
difference. When the upstream host cannot be reached, exit 0 with a
"SKIP (<reason>)" summary: nothing was compared, and the line says so. A host
that answers but lacks the commit is a FAIL, not a skip.
"""
import os
import subprocess
import sys
import tempfile

TOP_FILES = ("COPYING", "README")
TIMEOUT = 120


def git(repo, *args, data=None):
    return subprocess.run(["git", "-C", repo, *args], input=data,
                          capture_output=True, timeout=TIMEOUT)


def vendored_files(root):
    out = [name for name in TOP_FILES]
    for base, dirs, files in os.walk(os.path.join(root, "src")):
        dirs.sort()
        for name in sorted(files):
            out.append(os.path.relpath(os.path.join(base, name), root))
    return [p.replace(os.sep, "/") for p in out]


def upstream_blobs(repo, commit, paths):
    """Returns {path: bytes or None} for each path in commit."""
    got = {}
    req = "".join("%s:%s\n" % (commit, p) for p in paths).encode()
    res = git(repo, "cat-file", "--batch", data=req)
    if res.returncode != 0:
        raise RuntimeError(res.stderr.decode(errors="replace").strip())
    buf = res.stdout
    pos = 0
    for p in paths:
        nl = buf.index(b"\n", pos)
        head = buf[pos:nl].split()
        pos = nl + 1
        if len(head) == 2 and head[1] == b"missing":
            got[p] = None
            continue
        if len(head) != 3 or head[1] != b"blob":
            got[p] = None
            continue
        size = int(head[2])
        got[p] = buf[pos:pos + size]
        pos += size + 1
    return got


def main(argv):
    if len(argv) != 4:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    root, url, commit = argv[1], argv[2], argv[3].lower()
    with tempfile.TemporaryDirectory(prefix="lwip-upstream-") as repo:
        env_ok = git(repo, "init", "-q")
        if env_ok.returncode != 0:
            print("lwip-upstream-check: FAIL: git init: %s"
                  % env_ok.stderr.decode(errors="replace").strip())
            return 1
        try:
            reach = git(repo, "ls-remote", "--heads", url)
        except subprocess.TimeoutExpired:
            print("lwip-upstream-check: SKIP (no network: %s timed out after %ds); "
                  "nothing compared" % (url, TIMEOUT))
            return 0
        if reach.returncode != 0:
            why = reach.stderr.decode(errors="replace").strip().splitlines()
            print("lwip-upstream-check: SKIP (no network: %s unreachable: %s); "
                  "nothing compared" % (url, why[-1] if why else "git ls-remote failed"))
            return 0
        fetch = git(repo, "fetch", "-q", "--depth", "1", url, commit)
        if fetch.returncode != 0:
            print("lwip-upstream-check: FAIL: %s has no commit %s: %s" % (
                url, commit, fetch.stderr.decode(errors="replace").strip()))
            return 1
        head = git(repo, "rev-parse", "FETCH_HEAD^{commit}")
        got = head.stdout.decode().strip().lower()
        if head.returncode != 0 or got != commit:
            print("lwip-upstream-check: FAIL: fetched commit %s, want %s" % (got, commit))
            return 1
        paths = vendored_files(root)
        blobs = upstream_blobs(repo, commit, paths)
        bad = []
        for p in paths:
            local = os.path.join(root, p)
            if not os.path.isfile(local):
                bad.append("%s: missing from the vendored tree" % p)
                continue
            with open(local, "rb") as f:
                mine = f.read()
            theirs = blobs.get(p)
            if theirs is None:
                bad.append("%s: not in upstream %s" % (p, commit[:12]))
            elif mine != theirs:
                at = next((i for i, (a, b) in enumerate(zip(mine, theirs)) if a != b),
                          min(len(mine), len(theirs)))
                bad.append("%s: differs from upstream at byte %d" % (p, at))
        for line in bad:
            print("lwip-upstream-check: %s" % line)
        if bad:
            print("lwip-upstream-check: FAIL: %d of %d vendored files differ from %s at %s"
                  % (len(bad), len(paths), url, commit))
            return 1
        print("lwip-upstream-check: PASS: %d vendored files byte-identical to %s at %s"
              % (len(paths), url, commit))
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
