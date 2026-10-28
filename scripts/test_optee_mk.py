# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests for mk/optee.mk's make logic (make scripts-test).

Through the make targets themselves, without fetching or building: the pin is
a release tag and the commit it must resolve to, the cache is outside the
checkout, and each refusal of `make optee-run` and `make optee-tools` comes
before anything is fetched, built or written. Every cache a test names is a
fresh temporary directory, removed by the test; the shared upstream cache is
never touched."""

import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PRINT = "tfw-print-%: ; @echo $($*)"


def make(*args, env=None):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        env=env, check=False)
    return p.returncode, p.stdout


def var(name, *args, env=None):
    rc, out = make("--eval", PRINT, "tfw-print-" + name, *args, env=env)
    if rc != 0:
        raise AssertionError(f"make print {name}: {out}")
    return out.strip()


class OpteePin(unittest.TestCase):

    def test_the_pin_is_one_release_tag_and_the_commit_it_resolves_to(self):
        self.assertRegex(var("OPTEE_RELEASE"), r"^\d+\.\d+\.\d+$")
        self.assertRegex(var("OPTEE_MANIFEST_COMMIT"), r"^[0-9a-f]{40}$")
        self.assertEqual(var("OPTEE_MANIFEST_FILE"), "qemu_v8.xml")
        self.assertRegex(var("OPTEE_REPO_TAG"), r"^v\d+\.\d+$")
        self.assertRegex(var("OPTEE_REPO_COMMIT"), r"^[0-9a-f]{40}$")
        self.assertRegex(var("OPTEE_GP_CORE_API"), r"^\d+\.\d+\.\d+$")

    def test_the_fetch_asks_for_the_tag_and_checks_both_commits(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = os.path.join(tmp, "cache")
            release = var("OPTEE_RELEASE")
            stamp = os.path.join(cache, release, ".tfw-synced")
            rc, out = make("-n", "OPTEE_CACHE=" + cache, stamp)
            self.assertEqual(rc, 0, out)
            self.assertIn("-b refs/tags/" + release + " -m qemu_v8.xml", out)
            self.assertIn("--branch " + var("OPTEE_REPO_TAG"), out)
            self.assertIn(var("OPTEE_MANIFEST_COMMIT"), out)
            self.assertIn(var("OPTEE_REPO_COMMIT"), out)
            self.assertNotRegex(out, r"-b (master|main)\b")
            self.assertFalse(os.path.exists(cache), "make -n wrote the cache")


class OpteeCache(unittest.TestCase):

    def test_the_default_cache_is_under_home_and_outside_the_checkout(self):
        with tempfile.TemporaryDirectory() as home:
            env = dict(os.environ, HOME=home)
            cache = var("OPTEE_CACHE", env=env)
            self.assertTrue(cache.startswith(home + os.sep), cache)
            self.assertFalse(os.path.abspath(cache).startswith(REPO + os.sep), cache)

    def test_a_cache_inside_the_checkout_is_refused_before_anything_is_made(self):
        for name in ("optee-tools", "optee-qemu", "optee-run", "integration-test-optee"):
            with self.subTest(target=name):
                inside = tempfile.mkdtemp(prefix="optee-cache-", dir=os.path.join(REPO, "build"))
                os.rmdir(inside)
                try:
                    for cache in (inside, os.path.relpath(inside, REPO)):
                        rc, out = make(name, "OPTEE_CACHE=" + cache, "OPTEE_GUEST_CMD=true")
                        self.assertNotEqual(rc, 0, out)
                        self.assertIn("is inside the checkout", out)
                        self.assertFalse(os.path.exists(inside), "the refused cache was created")
                finally:
                    shutil.rmtree(inside, ignore_errors=True)


class OpteeRunRefusals(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.cache = os.path.join(self.tmp, "cache")

    def refused(self, *args):
        rc, out = make("optee-run", "OPTEE_CACHE=" + self.cache, *args)
        self.assertNotEqual(rc, 0, out)
        self.assertFalse(os.path.exists(self.cache), "a refused run wrote the cache")
        return out

    def test_a_missing_share_directory_is_refused(self):
        missing = os.path.join(self.tmp, "no-such-share")
        out = self.refused("OPTEE_SHARE=" + missing, "OPTEE_GUEST_CMD=true")
        self.assertIn(missing + " is not a directory", out)
        self.assertFalse(os.path.exists(missing))

    def test_a_share_qemu_cannot_mount_is_refused(self):
        share = os.path.join(self.tmp, "a,b")
        os.mkdir(share)
        out = self.refused("OPTEE_SHARE=" + share, "OPTEE_GUEST_CMD=true")
        self.assertIn("has a comma or a space", out)

    def test_no_guest_command_is_refused(self):
        out = self.refused("OPTEE_SHARE=" + self.tmp)
        self.assertIn("OPTEE_GUEST_CMD names nothing to run", out)

    def test_the_guest_command_reaches_the_recipe_as_written(self):
        cmd = """xtest -t regression "1001 1002" 'a b' $HOME"""
        rc, out = make("--eval", 'tfw-env: ; @printf "%s\\n" "$$OPTEE_GUEST_CMD"', "tfw-env",
                       "OPTEE_GUEST_CMD=" + cmd)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out, cmd + "\n")
        rc, out = make("-n", "optee-run", "OPTEE_CACHE=" + self.cache, "OPTEE_GUEST_CMD=" + cmd)
        self.assertEqual(rc, 0, out)
        self.assertIn('--cmd "$OPTEE_GUEST_CMD"', out)
        self.assertFalse(os.path.exists(self.cache))


class OpteeTa(unittest.TestCase):
    """The TA's UUID is one literal everywhere it is a file name, and the build
    puts the TA and tfw-ta-check in the guest through a second overlay."""

    UUID = "23751b49-3a76-4e49-b63c-e5853742fa1f"

    def read(self, *path):
        with open(os.path.join(REPO, *path), encoding="utf-8") as f:
            return f.read()

    def test_the_uuid_is_the_literal_in_the_make_the_ta_and_its_header(self):
        self.assertEqual(var("OPTEE_TA_UUID"), self.UUID)
        self.assertIn("BINARY = " + self.UUID + "\n", self.read("optee", "ta", "Makefile"))
        hexes = self.UUID.replace("-", "")
        fields = [hexes[0:8], hexes[8:12], hexes[12:16]]
        node = ",".join("0x%su" % hexes[i:i + 2] for i in range(16, 32, 2))
        want = "{%s,{%s}}" % (",".join("0x%su" % f for f in fields), node)
        header = self.read("optee", "ta", "include", "tfw_ta.h")
        m = re.search(r"#define TFW_TA_UUID((?:[^\n]*\\\n)*[^\n]*)", header)
        self.assertIsNotNone(m, "tfw_ta.h defines no TFW_TA_UUID")
        self.assertEqual(re.sub(r"[\s\\]", "", m.group(1)), want)

    def test_upstreams_overlay_comes_first_and_the_tas_second(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = os.path.join(tmp, "cache")
            flags = var("OPTEE_BUILD_FLAGS", "OPTEE_CACHE=" + cache)
            root = os.path.join(cache, var("OPTEE_RELEASE"))
            # The recipe's echo reads the shell quotes that keep it one argument.
            self.assertIn("BR2_ROOTFS_OVERLAY=" + root + "/build/br-ext/board/qemu/overlay "
                          + root + "/tfw/overlay", flags)
            self.assertFalse(os.path.exists(cache))

    def test_the_ta_and_the_check_are_built_between_two_passes_and_installed(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = os.path.join(tmp, "cache")
            root = os.path.join(cache, var("OPTEE_RELEASE"))
            rc, out = make("-n", "--assume-old=optee-tools", "OPTEE_CACHE=" + cache, "optee-qemu")
            self.assertEqual(rc, 0, out)
            ta = out.index("-C " + REPO + "/optee/ta ")
            check = out.index("-C " + REPO + "/optee/check ")
            self.assertLess(out.index(" all"), ta)
            self.assertLess(ta, check)
            self.assertIn(" all", out[check:])
            self.assertIn(root + "/tfw/overlay/lib/optee_armtz/" + self.UUID + ".ta", out)
            self.assertIn(root + "/tfw/overlay/usr/bin/tfw-ta-check", out)
            self.assertFalse(os.path.exists(cache), "make -n wrote the cache")

    def test_the_integration_test_runs_xtest_then_the_check_and_reads_both_commands(self):
        mk = self.read("mk", "optee.mk")
        self.assertIn("OPTEE_GUEST_CMD='xtest -l 0 && tfw-ta-check'", mk)
        for line in ("tfw-ta-check: ok get-info", "tfw-ta-check: ok get-random",
                     "tfw-ta-check: PASS, 14 of 14 passed"):
            self.assertIn("'" + line + "'", mk)


class OpteeNotInCi(unittest.TestCase):

    def test_no_stage_names_an_optee_target(self):
        with open(os.path.join(REPO, "mk", "stages.mk"), encoding="utf-8") as f:
            stages = f.read()
        self.assertIsNone(re.search(r"\boptee-", stages))


if __name__ == "__main__":
    unittest.main()
