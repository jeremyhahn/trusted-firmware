# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# scripts/release_alpha.py is what `make release-alpha` runs
# (docs/development/releasing.md). These tests run it on a throwaway git
# repository whose `dev` remote is a bare repository beside it, so no network is
# touched. Signing in the scratch repository goes to a stand-in signer named by
# its own gpg.program; nothing outside the temp directory is configured.

import contextlib
import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "release_alpha.py")

_spec = importlib.util.spec_from_file_location("release_alpha", SCRIPT)
release_alpha = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(release_alpha)

SERIES = ["v0.3.0-alpha", "v0.4.0-alpha", "sdk/go/v0.4.0-alpha", "v0.4.1-alpha",
          "sdk/go/v0.4.1-alpha", "xkms/v0.3.0-alpha", "xkms/v0.9.0-alpha"]

# What git asks of gpg: a detached signature on stdout, SIG_CREATED on stderr.
FAKE_SIGNER = f"""#!{sys.executable}
import sys
sys.stdin.read()
sys.stderr.write("\\n[GNUPG:] SIG_CREATED D 1 8 00 0 0\\n")
sys.stdout.write("-----BEGIN PGP SIGNATURE-----\\n\\nstand-in\\n-----END PGP SIGNATURE-----\\n")
"""


def git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args), check=True,
                          capture_output=True, text=True).stdout.strip()


def write(path, text):
    with open(path, "w") as f:
        f.write(text)


class ReleaseAlpha(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="tfw-release-alpha-")
        self.addCleanup(self.tmp.cleanup)
        t = self.tmp.name
        self.dev = os.path.join(t, "dev.git")
        self.repo = os.path.join(t, "repo")
        signer = os.path.join(t, "fake-gpg")
        write(signer, FAKE_SIGNER)
        os.chmod(signer, 0o755)
        subprocess.run(["git", "init", "-q", "--bare", "-b", "main", self.dev], check=True)
        subprocess.run(["git", "init", "-q", "-b", "main", self.repo], check=True)
        for k, v in (("user.name", "t"), ("user.email", "t@t"), ("commit.gpgsign", "false"),
                     ("gpg.format", "openpgp"), ("gpg.program", signer), ("user.signingkey", "T")):
            git(self.repo, "config", k, v)
        git(self.repo, "remote", "add", "dev", self.dev)
        write(os.path.join(self.repo, "VERSION"), "0.4.1-alpha\n")
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "first")
        self.tag(*SERIES)
        git(self.repo, "commit", "-q", "--allow-empty", "-m", "second")
        git(self.repo, "push", "-q", "dev", "main", "--tags")

    def tag(self, *names):
        for n in names:
            git(self.repo, "tag", n)

    def tags(self, repo):
        return sorted(git(repo, "tag", "--list").split())

    def run_main(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = release_alpha.main(["--repo", self.repo] + list(args))
        return rc, out.getvalue()

    def refused(self, *args):
        with self.assertRaises(SystemExit) as e, contextlib.redirect_stdout(io.StringIO()):
            release_alpha.main(["--repo", self.repo] + list(args))
        return str(e.exception.code)

    def test_a_tag_series_dry_run_prints_the_next_patch_and_cuts_nothing(self):
        before, dev_before = self.tags(self.repo), self.tags(self.dev)
        rc, out = self.run_main("--dry-run")
        self.assertEqual(rc, 0)
        head = git(self.repo, "rev-parse", "HEAD")
        self.assertIn(f"would cut v0.4.2-alpha sdk/go/v0.4.2-alpha on {head}", out)
        self.assertIn("consumer-check github.com/jeremyhahn/trusted-firmware/sdk/go "
                      "sdk/go/v0.4.2-alpha", out)
        self.assertNotIn("xkms/", out)
        self.assertEqual(self.tags(self.repo), before)
        self.assertEqual(self.tags(self.dev), dev_before)

    def test_the_script_runs_as_make_runs_it(self):
        r = subprocess.run([sys.executable, "-B", SCRIPT, "--dry-run"], cwd=self.repo,
                           capture_output=True, text=True, check=False)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("sdk/go/v0.4.2-alpha", r.stdout)

    def test_a_dirty_tree_is_refused(self):
        for what, path, text in (("tracked", "VERSION", "changed\n"),
                                 ("untracked", "stray.c", "int stray;\n")):
            with self.subTest(what):
                write(os.path.join(self.repo, path), text)
                self.assertIn("not clean", self.refused("--dry-run"))
                git(self.repo, "checkout", "-q", "--", ".")
                git(self.repo, "clean", "-qfd")

    def test_a_branch_that_is_not_main_is_refused(self):
        git(self.repo, "checkout", "-q", "-b", "feature")
        self.assertIn("on feature, not main", self.refused("--dry-run"))
        git(self.repo, "checkout", "-q", "--detach", "main")
        self.assertIn("symbolic-ref", self.refused("--dry-run"))

    def test_a_non_monotonic_series_is_refused(self):
        for planted in ("v0.4.2-rc.1", "v0.4.2", "sdk/go/v0.4.2-alpha", "sdk/go/v0.5.0-alpha"):
            with self.subTest(planted):
                self.tag(planted)
                self.assertIn(f"would not sort above {planted}", self.refused("--dry-run"))
                git(self.repo, "tag", "-d", planted)
        self.assertEqual(self.tags(self.dev), sorted(SERIES))

    def test_a_head_behind_dev_main_is_refused(self):
        other = os.path.join(self.tmp.name, "other")
        subprocess.run(["git", "clone", "-q", self.dev, other], check=True)
        git(other, "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false",
            "commit", "-q", "--allow-empty", "-m", "third")
        git(other, "push", "-q", "origin", "main")
        self.assertIn("is not dev/main", self.refused("--dry-run"))

    def test_no_alpha_tag_is_refused(self):
        for t in SERIES:
            git(self.repo, "tag", "-d", t)
            git(self.dev, "tag", "-d", t)
        self.tag("v0.1.0")
        self.assertIn("no vX.Y.Z-alpha tag", self.refused("--dry-run"))

    def test_a_cut_pushes_both_signed_annotated_tags_to_dev(self):
        rc, out = self.run_main()
        self.assertEqual(rc, 0)
        self.assertIn("pushed them to dev", out)
        head = git(self.repo, "rev-parse", "HEAD")
        for t in ("v0.4.2-alpha", "sdk/go/v0.4.2-alpha"):
            with self.subTest(t):
                self.assertEqual(git(self.dev, "cat-file", "-t", t), "tag")
                self.assertEqual(git(self.dev, "rev-parse", f"{t}^{{commit}}"), head)
                self.assertIn("-----BEGIN PGP SIGNATURE-----", git(self.dev, "cat-file", "tag", t))
        self.assertNotIn("xkms/v0.9.1-alpha", self.tags(self.dev))
        # The next run increments past the tags it just cut.
        _, out = self.run_main("--dry-run")
        self.assertIn("would cut v0.4.3-alpha sdk/go/v0.4.3-alpha", out)


class Precedence(unittest.TestCase):
    def test_semver_precedence(self):
        order = ["v0.4.1", "v0.4.2-1", "v0.4.2-alpha", "v0.4.2-alpha.1", "v0.4.2-alpha.beta",
                 "v0.4.2-rc.1", "v0.4.2-rc.2", "v0.4.2-rc.10", "v0.4.2", "v0.10.0-alpha"]
        keys = [release_alpha.precedence(release_alpha.parse(n)) for n in order]
        self.assertEqual(keys, sorted(keys))
        self.assertEqual(len(set(keys)), len(keys))

    def test_names_that_are_not_versions_are_outside_every_series(self):
        for n in ("v01.2.3", "v1.2", "1.2.3", "v1.2.3-", "vX.Y.Z-alpha"):
            with self.subTest(n):
                self.assertIsNone(release_alpha.parse(n))
        self.assertEqual(release_alpha.name(release_alpha.parse("v1.2.3")), "v1.2.3")


if __name__ == "__main__":
    unittest.main()
