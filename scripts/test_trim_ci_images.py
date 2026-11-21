# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests for scripts/trim-ci-images.sh, the CI image tag trimmer.

Each case runs the real script with bash and a fake `docker` first on PATH (no
Docker host is touched). The fake answers `image ls` from a table of tags per
repository (newest first), `ps -aq --filter ancestor=<ref>` with an id for the
refs marked in use, and `image rm <ref>` by logging the ref and failing for the
refs marked as refusing. The cases assert on that log and on the script's
output lines: the newest CI_IMAGE_KEEP hash tags kept, the image named on the
command line and any image a container uses never removed, REAP_DRY_RUN=1
removing nothing, and a refused removal never failing the job. The fake
answers only that exact `ps -aq --filter ancestor=<ref>` form (without `-a`
Docker omits stopped containers); any other invocation is logged to other.log
and fails, and every case that reaches the summary asserts other.log is empty.

TRIM_CI_IMAGES_SCRIPT points the suite at another copy of the script; it is
set only to prove the suite fails on a mutated copy.
"""

import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.environ.get("TRIM_CI_IMAGES_SCRIPT", os.path.join(HERE, "trim-ci-images.sh"))

REPO = "ghcr.io/example/toolchain"
OTHER = "ghcr.io/example/esp32"

# Five hash tags, newest first; H3 carries an architecture suffix.
H1 = "0123456789abcdef"
H2 = "1111111111111111"
H3 = "2222222222222222-arm"
H4 = "3333333333333333"
H5 = "4444444444444444"
# Tags that are not hash tags: the trimmer must leave every one of them.
NOT_HASH = ["latest", "main", "abcdefabcdefabc", "ABCDEFABCDEFABCD"]
MIXED = [H1, "latest", H2, "main", H3, "abcdefabcdefabc", H4, "ABCDEFABCDEFABCD", H5]

FAKE_DOCKER = """#!{python}
import json, os, sys
d = {state!r}
def lines(name):
    try:
        with open(os.path.join(d, name)) as f:
            return [l.strip() for l in f if l.strip()]
    except FileNotFoundError:
        return []
a = sys.argv[1:]
if a[:2] == ["image", "ls"]:
    with open(os.path.join(d, "tags.json")) as f:
        tags = json.load(f)
    for t in tags.get(a[-1], []):
        print(t)
    sys.exit(0)
if len(a) == 4 and a[:3] == ["ps", "-aq", "--filter"] and a[3].startswith("ancestor="):
    if a[3][len("ancestor="):] in lines("inuse"):
        print("c0ffee000001")
    sys.exit(0)
if a[:2] == ["image", "rm"]:
    with open(os.path.join(d, "rm.log"), "a") as f:
        f.write(a[2] + "\\n")
    sys.exit(1 if a[2] in lines("refuse") else 0)
with open(os.path.join(d, "other.log"), "a") as f:
    f.write(" ".join(a) + "\\n")
sys.exit(1)
"""


def ref(tag, repo=REPO):
    return repo + ":" + tag


class Trim(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.state = os.path.join(self.tmp.name, "state")
        self.bin = os.path.join(self.tmp.name, "bin")
        os.mkdir(self.state)
        os.mkdir(self.bin)
        docker = os.path.join(self.bin, "docker")
        with open(docker, "w") as f:
            f.write(FAKE_DOCKER.format(python=sys.executable, state=self.state))
        os.chmod(docker, os.stat(docker).st_mode | stat.S_IXUSR)
        self.tags({REPO: MIXED})

    def tags(self, table):
        with open(os.path.join(self.state, "tags.json"), "w") as f:
            json.dump(table, f)

    def mark(self, name, refs):
        with open(os.path.join(self.state, name), "w") as f:
            f.write("".join(r + "\n" for r in refs))

    def log(self, name="rm.log"):
        try:
            with open(os.path.join(self.state, name)) as f:
                return f.read().split()
        except FileNotFoundError:
            return []

    def run_trim(self, *images, **env):
        full = {"PATH": self.bin + os.pathsep + os.environ["PATH"]}
        full.update(env)
        p = subprocess.run(["bash", SCRIPT, *images], env=full, capture_output=True,
                           text=True, timeout=30)
        return p.returncode, p.stdout.splitlines(), p.stderr

    def assert_summary(self, out, removed, keep=3):
        self.assertEqual(out[-1], "trim: %d image tag(s) removed, the newest %d kept "
                         "per repository" % (removed, keep))
        # An unexpected docker invocation must fail the case, not read as
        # "not in use".
        self.assertEqual(self.log("other.log"), [])

    def test_no_argument_is_a_usage_error(self):
        rc, out, err = self.run_trim()
        self.assertEqual(rc, 2)
        self.assertIn("usage:", err)
        self.assertEqual(self.log(), [])

    def test_a_non_numeric_keep_is_a_usage_error(self):
        for keep in ("x", "3x", "-1", " 3"):
            with self.subTest(keep=keep):
                rc, out, err = self.run_trim(ref(H1), CI_IMAGE_KEEP=keep)
                self.assertEqual(rc, 2)
                self.assertIn("CI_IMAGE_KEEP must be a number", err)
                self.assertEqual(self.log(), [])

    def test_an_empty_keep_is_the_default_three(self):
        # ${CI_IMAGE_KEEP:-3}: an empty value falls back to the default.
        rc, out, _ = self.run_trim(ref(H1), CI_IMAGE_KEEP="")
        self.assertEqual(rc, 0)
        self.assertEqual(self.log(), [ref(H4), ref(H5)])
        self.assert_summary(out, 2)

    def test_the_default_keeps_the_three_newest_hash_tags(self):
        rc, out, _ = self.run_trim(ref(H1))
        self.assertEqual(rc, 0)
        self.assertEqual(self.log(), [ref(H4), ref(H5)])
        self.assertEqual(out[:-1], ["trim  " + ref(H4), "trim  " + ref(H5)])
        self.assert_summary(out, 2)
        for tag in NOT_HASH:
            self.assertNotIn(ref(tag), self.log())

    def test_keep_moves_the_cut(self):
        for keep, gone in (("0", [H2, H3, H4, H5]), ("1", [H2, H3, H4, H5]),
                           ("2", [H3, H4, H5]), ("5", [])):
            with self.subTest(keep=keep):
                self.mark("rm.log", [])
                rc, out, _ = self.run_trim(ref(H1), CI_IMAGE_KEEP=keep)
                self.assertEqual(rc, 0)
                self.assertEqual(self.log(), [ref(t) for t in gone])
                self.assert_summary(out, len(gone), int(keep))

    def test_keep_zero_removes_every_hash_tag_but_the_named_one(self):
        rc, out, _ = self.run_trim(ref(H3), CI_IMAGE_KEEP="0")
        self.assertEqual(rc, 0)
        self.assertEqual(self.log(), [ref(H1), ref(H2), ref(H4), ref(H5)])
        self.assert_summary(out, 4, 0)

    def test_the_named_image_past_the_cut_is_never_removed(self):
        rc, out, _ = self.run_trim(ref(H5))
        self.assertEqual(rc, 0)
        self.assertNotIn(ref(H5), self.log())
        self.assertEqual(self.log(), [ref(H4)])
        self.assert_summary(out, 1)

    def test_an_image_in_use_past_the_cut_is_kept(self):
        self.mark("inuse", [ref(H4)])
        rc, out, _ = self.run_trim(ref(H1))
        self.assertEqual(rc, 0)
        self.assertEqual(self.log(), [ref(H5)])
        self.assertIn("keep  %s (in use)" % ref(H4), out)
        self.assert_summary(out, 1)

    def test_a_dry_run_removes_nothing(self):
        rc, out, _ = self.run_trim(ref(H1), REAP_DRY_RUN="1")
        self.assertEqual(rc, 0)
        self.assertEqual(self.log(), [])
        self.assertEqual(out[:-1], ["dry   docker image rm " + ref(H4),
                                    "dry   docker image rm " + ref(H5)])
        self.assert_summary(out, 0)

    def test_a_refused_removal_warns_and_is_not_counted(self):
        self.mark("refuse", [ref(H4)])
        rc, out, _ = self.run_trim(ref(H1))
        self.assertEqual(rc, 0)
        self.assertEqual(self.log(), [ref(H4), ref(H5)])
        self.assertEqual(out[:-1], ["warn  %s not removed" % ref(H4), "trim  " + ref(H5)])
        self.assert_summary(out, 1)

    def test_every_refused_removal_still_exits_zero(self):
        self.mark("refuse", [ref(H4), ref(H5)])
        rc, out, _ = self.run_trim(ref(H1))
        self.assertEqual(rc, 0)
        self.assert_summary(out, 0)

    def test_two_images_are_cut_per_repository(self):
        self.tags({REPO: MIXED, OTHER: [H5, H4, "latest", H3, H2, H1]})
        rc, out, _ = self.run_trim(ref(H1), ref(H5, OTHER))
        self.assertEqual(rc, 0)
        self.assertEqual(self.log(), [ref(H4), ref(H5), ref(H2, OTHER), ref(H1, OTHER)])
        self.assert_summary(out, 4)


if __name__ == "__main__":
    unittest.main()
