# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The docker-build-* rules against a daemon where another leg won the tag.

Run 70520: two legs on one daemon built the same content-addressed image at
once, and the second `docker tag` to the shared name failed with
AlreadyExists. Run 75060: the winner's image had the same inputs but another
id, since buildkit writes run-specific metadata, so the rules label each image
with its input hash and a lost race passes on equal labels. These drive
mk/docker.mk's rules through make with DOCKER set to test_docker_tag_stub.sh,
so no daemon is needed.
"""

import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STUB = os.path.join(HERE, "test_docker_tag_stub.sh")
BUILT = "sha256:" + "a4" * 32
OTHER = "sha256:" + "5e" * 32
INPUTS = "fe80181c9f4b1d12"
OTHER_INPUTS = "0123456789abcdef"

# Each rule, and the variable naming its image.
RULES = (
    ("docker-build-toolchain", "TOOLCHAIN_IMAGE"),
    ("docker-build-esp32", "ESP32_IMAGE"),
    ("docker-build-optee", "OPTEE_IMAGE"),
)


class DockerTagRace(unittest.TestCase):
    def build(self, rule, var, target_id, tag_error=None,
              built_label="", target_label=""):
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, "calls")
            env = dict(os.environ, IMAGE_BUILD_ID="leg-1",
                       STUB_BUILT_ID=BUILT, STUB_TARGET_ID=target_id,
                       STUB_BUILT_LABEL=built_label,
                       STUB_TARGET_LABEL=target_label, STUB_LOG=log)
            if tag_error is not None:
                env["STUB_TAG_ERROR"] = tag_error
            r = subprocess.run(
                ["make", "--no-print-directory", "-C", ROOT, rule,
                 "DOCKER=" + STUB, var + "=trusted-firmware-x:0123456789abcdef-main",
                 "IMAGE_BUILD_ID=leg-1"],
                env=env, capture_output=True, text=True, timeout=120)
            r.calls = ""
            if os.path.exists(log):
                with open(log) as f:
                    r.calls = f.read()
            return r

    def test_each_rule_labels_its_image_with_its_input_hash(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, BUILT)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertRegex(r.calls, r"(?m)^build .*--label tfw\.image\.inputs=[0-9a-f]{16} ")

    def test_a_lost_tag_race_to_an_image_of_the_same_inputs_passes(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, OTHER, built_label=INPUTS, target_label=INPUTS)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn(INPUTS, r.stdout)

    def test_a_lost_tag_race_to_an_image_of_other_inputs_fails(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, OTHER, built_label=INPUTS, target_label=OTHER_INPUTS)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn("AlreadyExists", r.stderr)
                self.assertIn(OTHER_INPUTS, r.stderr)

    def test_a_lost_tag_race_to_an_unlabelled_image_fails(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, OTHER, built_label=INPUTS)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn("AlreadyExists", r.stderr)

    def test_any_other_tag_error_fails_when_the_labels_match(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, OTHER, "Error response from daemon: no space left on device",
                               built_label=INPUTS, target_label=INPUTS)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn("no space left on device", r.stderr)

    def test_a_lost_tag_race_to_the_same_image_passes(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, BUILT)
                self.assertEqual(r.returncode, 0, r.stderr)

    def test_a_name_that_holds_another_image_fails(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, OTHER)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn("AlreadyExists", r.stderr)

    def test_a_name_that_holds_no_image_fails(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, "")
                self.assertNotEqual(r.returncode, 0)

    def test_any_other_tag_error_fails(self):
        for rule, var in RULES:
            with self.subTest(rule=rule):
                r = self.build(rule, var, BUILT, "Error response from daemon: no space left on device")
                self.assertNotEqual(r.returncode, 0)
                self.assertIn("no space left on device", r.stderr)


if __name__ == "__main__":
    unittest.main()
