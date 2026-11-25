#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Cut and push the next internal alpha: what `make release-alpha` runs
# (handbook release-versioning.md §1a, §2, §3; docs/development/releasing.md).
#
# It refuses unless the checkout is a clean `main` equal to `dev/main`, takes
# the newest vX.Y.Z-alpha tag and increments PATCH only, and refuses unless the
# new tag sorts above every tag of its series. It then cuts the annotated,
# signed tags vX.Y.Z-alpha and sdk/go/vX.Y.Z-alpha on HEAD, with git's
# configured key, and pushes both to `dev`, never forced. The xkms/ module keeps
# its own series and is not tagged here. --dry-run fetches dev's main and tags,
# prints the tags, and cuts and pushes nothing. A failed cut or push deletes the
# tags this run cut, so the next run cuts the same version; it deletes no tag
# another run made, nor one moved since this run cut it.
#
#   release_alpha.py [--repo DIR] [--dry-run]

import argparse
import os
import re
import subprocess
import sys
import tempfile

REMOTE = "dev"
BRANCH = "main"
SDK_PREFIX = "sdk/go/"
SDK_MODULE = "github.com/jeremyhahn/trusted-firmware/sdk/go"

TAG = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?")


def git(repo, *args):
    r = subprocess.run(["git", "-C", repo] + list(args), capture_output=True, text=True, check=False)
    if r.returncode != 0:
        raise SystemExit(f"release-alpha: git {' '.join(args)} failed: {r.stderr.strip()}")
    return r.stdout.strip()


def git_bytes(repo, *args, data=None):
    r = subprocess.run(["git", "-C", repo] + list(args), input=data, capture_output=True, check=False)
    if r.returncode != 0:
        raise SystemExit(f"release-alpha: git {' '.join(args)} failed: "
                         f"{r.stderr.decode(errors='replace').strip()}")
    return r.stdout


def init_scratch(repo, scratch):
    """A private bare repository that reads the checkout's objects, so a tag
    can be made and its id read where no other process can move its ref."""
    git(scratch, "init", "--bare", "-q")
    objects = git(repo, "rev-parse", "--git-path", "objects")
    objects = os.path.join(os.path.abspath(repo), objects)
    with open(os.path.join(scratch, "objects", "info", "alternates"), "w") as f:
        f.write(os.path.abspath(objects) + "\n")


def signing_config(repo):
    """-c k=v for each user.* and gpg.* setting the checkout sees, so the
    scratch repository signs with the checkout's identity and signer."""
    r = subprocess.run(["git", "-C", repo, "config", "--null", "--get-regexp", r"^(user|gpg)\."],
                       capture_output=True, text=True, check=False)
    if r.returncode not in (0, 1):
        raise SystemExit(f"release-alpha: git config failed: {r.stderr.strip()}")
    out = []
    for entry in r.stdout.split("\0"):
        if entry:
            k, _, v = entry.partition("\n")
            out += ["-c", f"{k}={v}"]
    return out


def make_tag(repo, scratch, signer, t, head):
    """Sign tag t on head in scratch and write its object into the checkout.
    The object id, not any ref in the checkout."""
    git(scratch, *signer, "tag", "--sign", "-m", f"trusted-firmware {t}", t, head)
    oid = git(scratch, "rev-parse", f"refs/tags/{t}")
    body = git_bytes(scratch, "cat-file", "tag", oid)
    copied = git_bytes(repo, "hash-object", "-t", "tag", "-w", "--stdin", data=body).decode().strip()
    if copied != oid:
        raise SystemExit(f"release-alpha: tag {t} object {oid} was copied as {copied}")
    return oid


def parse(name):
    """(major, minor, patch, prerelease identifiers) of a vX.Y.Z[-pre] name, or None."""
    m = TAG.fullmatch(name)
    if not m:
        return None
    pre = tuple(m.group(4).split(".")) if m.group(4) else ()
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), pre)


def precedence(v):
    """A sort key in semver 2.0.0 §11 precedence: a release sorts above its
    pre-releases, numeric identifiers below alphanumeric ones."""
    major, minor, patch, pre = v
    ids = tuple((0, int(i), "") if i.isdigit() else (1, 0, i) for i in pre)
    return (major, minor, patch, 0 if pre else 1, ids)


def series(tags, prefix):
    """{name: version} of every tag in the series prefix + vX.Y.Z[-pre]."""
    out = {}
    for t in tags:
        if t.startswith(prefix):
            v = parse(t[len(prefix):])
            if v is not None:
                out[t] = v
    return out


def next_alpha(tags):
    """The version after the newest vX.Y.Z-alpha: PATCH + 1 (§2)."""
    alphas = [v for v in series(tags, "").values() if v[3] == ("alpha",)]
    if not alphas:
        raise SystemExit("release-alpha: no vX.Y.Z-alpha tag to increment; cut the first by hand")
    major, minor, patch, _ = max(alphas, key=precedence)
    return (major, minor, patch + 1, ("alpha",))


def check_monotonic(tags, prefix, new):
    """Refuse unless prefix + new sorts above every tag of its series (§3)."""
    for t, v in sorted(series(tags, prefix).items()):
        if precedence(v) >= precedence(new):
            raise SystemExit(f"release-alpha: {prefix}{name(new)} would not sort above {t}")


def name(v):
    major, minor, patch, pre = v
    return f"v{major}.{minor}.{patch}" + ("-" + ".".join(pre) if pre else "")


def check_checkout(repo):
    """A clean `main` equal to dev/main, after fetching dev's main and tags."""
    branch = git(repo, "symbolic-ref", "--quiet", "--short", "HEAD")
    if branch != BRANCH:
        raise SystemExit(f"release-alpha: on {branch}, not {BRANCH}")
    if git(repo, "status", "--porcelain", "--untracked-files=normal"):
        raise SystemExit("release-alpha: the working tree is not clean")
    git(repo, "fetch", "--quiet", "--tags", REMOTE, BRANCH)
    head = git(repo, "rev-parse", "HEAD")
    upstream = git(repo, "rev-parse", f"refs/remotes/{REMOTE}/{BRANCH}")
    if head != upstream:
        raise SystemExit(f"release-alpha: HEAD {head} is not {REMOTE}/{BRANCH} {upstream}")
    return head


def main(argv):
    p = argparse.ArgumentParser(prog="release_alpha.py")
    p.add_argument("--repo", default=".")
    p.add_argument("--dry-run", action="store_true", help="print the tags, cut and push nothing")
    a = p.parse_args(argv)

    head = check_checkout(a.repo)
    tags = git(a.repo, "tag", "--list").split()
    new = next_alpha(tags)
    check_monotonic(tags, "", new)
    check_monotonic(tags, SDK_PREFIX, new)
    cut = [name(new), SDK_PREFIX + name(new)]

    if a.dry_run:
        print(f"release-alpha: DRY_RUN: would cut {' '.join(cut)} on {head} and push them to {REMOTE}")
    else:
        # (tag, object id) of each tag this run created. Cleanup deletes only
        # these, and only while each still points at this run's tag object, so
        # a competing run that cut the same version keeps its tags. The id is
        # the object this run made, never a reread of the published ref.
        created = []
        try:
            with tempfile.TemporaryDirectory(prefix="release-alpha-") as scratch:
                init_scratch(a.repo, scratch)
                signer = signing_config(a.repo)
                for t in cut:
                    oid = make_tag(a.repo, scratch, signer, t, head)
                    # A zero old value: create only, refused if the tag exists.
                    git(a.repo, "update-ref", f"refs/tags/{t}", oid, "0" * len(oid))
                    created.append((t, oid))
            git(a.repo, "push", "--atomic", REMOTE, *(f"refs/tags/{t}" for t in cut))
        except SystemExit:
            for t, oid in created:
                subprocess.run(["git", "-C", a.repo, "update-ref", "-d", f"refs/tags/{t}", oid],
                               capture_output=True, check=False)
            raise
        print(f"release-alpha: cut {' '.join(cut)} on {head} and pushed them to {REMOTE}")
    print(f"next (coordinator, runbook step 12): consumer-check {SDK_MODULE} {cut[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
