#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Build one image twice, each time from a clean copy of the committed source,
# and fail unless the two are byte-identical. `make reproducible-check` and
# `make bootloader-reproducible` both run it (docs/SPEC.md, row secure-boot:
# the build is reproducible, so the expected firmware hash is computable from
# source).
#
# A clean copy is `git archive HEAD`: tracked files at the commit, so no
# build/, no untracked file and no uncommitted edit reaches it, and an image a
# previous build left behind cannot satisfy the check. SOURCE_DATE_EPOCH is the
# commit time, so anything time-derived is pinned to the source.
#
#   repro_build.py --image REL.hex --dir A --dir B [--src DIR] [--make MAKE] -- ARGS...

import argparse
import os
import shutil
import subprocess
import sys


def commit_epoch(src):
    out = subprocess.run(["git", "-C", src, "log", "-1", "--format=%ct", "HEAD"],
                         capture_output=True, text=True, check=True).stdout.strip()
    if not out.isdigit():
        raise SystemExit(f"repro_build: no commit time for HEAD in {src}: {out!r}")
    return out


def clean_copy(src, dst):
    """Extract `git archive HEAD` of src into an empty dst."""
    if os.path.lexists(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    archive = subprocess.Popen(["git", "-C", src, "archive", "--format=tar", "HEAD"],
                               stdout=subprocess.PIPE)
    subprocess.run(["tar", "-x", "-f", "-", "-C", dst], stdin=archive.stdout, check=True)
    archive.stdout.close()
    if archive.wait() != 0:
        raise SystemExit(f"repro_build: git archive HEAD in {src} exited {archive.returncode}")


def build(make, tree, args, epoch):
    # The sub-make is started here, not by make, so the caller's jobserver
    # descriptors are not inherited: drop the flags that name them.
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
    env["SOURCE_DATE_EPOCH"] = epoch
    run = subprocess.run([make, "-C", tree, "--no-print-directory"] + args,
                         env=env, capture_output=True, text=True, check=False)
    if run.returncode != 0:
        tail = "\n".join((run.stdout + run.stderr).splitlines()[-30:])
        raise SystemExit(f"repro_build: make {' '.join(args)} in {tree} exited {run.returncode}:\n{tail}")


def main(argv):
    p = argparse.ArgumentParser(prog="repro_build.py")
    p.add_argument("--src", default=".")
    p.add_argument("--make", default="make")
    p.add_argument("--image", required=True, help="the image, relative to the tree")
    p.add_argument("--dir", action="append", required=True, help="a build directory (twice)")
    p.add_argument("args", nargs="*", help="make arguments, after --")
    a = p.parse_args(argv)
    if len(a.dir) != 2 or os.path.abspath(a.dir[0]) == os.path.abspath(a.dir[1]):
        p.error("--dir names two different directories")

    epoch = commit_epoch(a.src)
    images = []
    for d in a.dir:
        clean_copy(a.src, d)
        build(a.make, d, a.args, epoch)
        path = os.path.join(d, a.image)
        if not os.path.isfile(path):
            raise SystemExit(f"repro_build: make {' '.join(a.args)} built no {a.image} in {d}")
        with open(path, "rb") as f:
            images.append(f.read())

    if images[0] != images[1]:
        at = next((i for i, (x, y) in enumerate(zip(*images)) if x != y), min(map(len, images)))
        print(f"NOT REPRODUCIBLE: {a.image} differs between {a.dir[0]} and {a.dir[1]} "
              f"from byte {at} (SOURCE_DATE_EPOCH={epoch})")
        return 1
    print(f"reproducible: {a.image} from two clean copies of HEAD is byte-identical "
          f"(SOURCE_DATE_EPOCH={epoch})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
