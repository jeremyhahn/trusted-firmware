# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""scripts/docker-tag.sh run directly, not through make.

test_docker_tag.py drives the script through mk/docker.mk's rules with a stub
whose `tag` always fails, so it never reaches the usage refusal, a clean tag,
docker's literal `<no value>` for an unlabelled image, or the exact text of
the refusal line. These run the script with DOCKER set to
test_docker_tag_stub.sh, or to a stub whose every call succeeds, so no daemon
is needed. `make scripts-test` runs them.
"""

import os
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "docker-tag.sh")
STUB = os.path.join(HERE, "test_docker_tag_stub.sh")
BUILT = "sha256:" + "a4" * 32
OTHER = "sha256:" + "5e" * 32
INPUTS = "fe80181c9f4b1d12"
OTHER_INPUTS = "0123456789abcdef"
DST = "trusted-firmware-x:0123456789abcdef-main"
SRC = DST + "-leg-1"
ALREADY_EXISTS = "AlreadyExists: image \"%s\" already exists" % DST


def refusal(have, have_label, want, want_label):
    """The script's last stderr line when it refuses a lost race."""
    return ("docker-tag: %s resolves to '%s' labelled '%s', not %s's '%s' labelled '%s'"
            % (DST, have, have_label, SRC, want, want_label))


class DockerTagDirect(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = tmp.name
        self.log = os.path.join(self.tmp, "calls")

    def run_script(self, args, **env):
        e = dict(os.environ, DOCKER=STUB, IMAGE_BUILD_ID="leg-1", STUB_LOG=self.log)
        e.update(env)
        r = subprocess.run(["bash", SCRIPT, *args], env=e,
                           capture_output=True, text=True, timeout=30)
        r.calls = ""
        if os.path.exists(self.log):
            with open(self.log) as f:
                r.calls = f.read()
        return r

    def test_a_missing_source_or_target_prints_usage_and_exits_2(self):
        for args in ((), (SRC,)):
            with self.subTest(args=args):
                r = self.run_script(args)
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertTrue(r.stderr.startswith("usage: "), r.stderr)
                self.assertFalse(os.path.exists(self.log), r.calls)

    def test_a_tag_that_succeeds_exits_0_silently(self):
        ok = os.path.join(self.tmp, "docker-ok")
        with open(ok, "w") as f:
            f.write("#!/bin/sh\nexit 0\n")
        os.chmod(ok, os.stat(ok).st_mode | stat.S_IXUSR)
        r = self.run_script((SRC, DST), DOCKER=ok)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, "")
        self.assertEqual(r.stderr, "")

    def test_two_unlabelled_images_of_other_ids_are_not_the_same_inputs(self):
        r = self.run_script((SRC, DST), STUB_BUILT_ID=BUILT, STUB_TARGET_ID=OTHER,
                            STUB_BUILT_LABEL="<no value>", STUB_TARGET_LABEL="<no value>")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn(ALREADY_EXISTS, r.stderr)
        self.assertIn("labelled 'nothing', not", r.stderr)
        self.assertTrue(r.stderr.rstrip("\n").endswith("labelled 'nothing'"), r.stderr)
        self.assertNotIn("kept", r.stdout)

    def test_a_target_of_no_image_is_refused_as_nothing(self):
        r = self.run_script((SRC, DST), STUB_BUILT_ID=BUILT, STUB_TARGET_ID="",
                            STUB_BUILT_LABEL=INPUTS)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertEqual(r.stderr.splitlines()[-1],
                         refusal("nothing", "nothing", BUILT, INPUTS))

    def test_a_target_of_other_inputs_is_refused_with_both_ids_and_labels(self):
        r = self.run_script((SRC, DST), STUB_BUILT_ID=BUILT, STUB_TARGET_ID=OTHER,
                            STUB_BUILT_LABEL=INPUTS, STUB_TARGET_LABEL=OTHER_INPUTS)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn(ALREADY_EXISTS, r.stderr)
        self.assertEqual(r.stderr.splitlines()[-1],
                         refusal(OTHER, OTHER_INPUTS, BUILT, INPUTS))

    def test_any_other_tag_error_is_passed_on_without_an_inspect(self):
        r = self.run_script((SRC, DST), STUB_TAG_ERROR="no space left on device",
                            STUB_BUILT_ID=BUILT, STUB_TARGET_ID=OTHER,
                            STUB_BUILT_LABEL=INPUTS, STUB_TARGET_LABEL=INPUTS)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertEqual(r.stderr, "no space left on device\n")
        self.assertEqual(r.calls, "tag %s %s\n" % (SRC, DST))

    def test_a_lost_race_to_the_same_image_is_kept(self):
        r = self.run_script((SRC, DST), STUB_BUILT_ID=BUILT, STUB_TARGET_ID=BUILT)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout,
                         "==> %s: another leg tagged the same image (%s) first; kept\n"
                         % (DST, BUILT))


if __name__ == "__main__":
    unittest.main()
