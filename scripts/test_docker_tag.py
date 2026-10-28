# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The docker-build-* rules against a daemon where another leg won the tag.

Run 70520: two legs on one daemon built the same content-addressed image at
once, and the second `docker tag` to the shared name failed with
AlreadyExists. The name then held the very image that leg built. These drive
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

# Each rule, and the variable naming its image.
RULES = (
    ("docker-build-toolchain", "TOOLCHAIN_IMAGE"),
    ("docker-build-esp32", "ESP32_IMAGE"),
    ("docker-build-optee", "OPTEE_IMAGE"),
)


class DockerTagRace(unittest.TestCase):
    def build(self, rule, var, target_id, tag_error=None):
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ, IMAGE_BUILD_ID="leg-1",
                       STUB_BUILT_ID=BUILT, STUB_TARGET_ID=target_id,
                       STUB_LOG=os.path.join(tmp, "calls"))
            if tag_error is not None:
                env["STUB_TAG_ERROR"] = tag_error
            return subprocess.run(
                ["make", "--no-print-directory", "-C", ROOT, rule,
                 "DOCKER=" + STUB, var + "=trusted-firmware-x:0123456789abcdef-main",
                 "IMAGE_BUILD_ID=leg-1"],
                env=env, capture_output=True, text=True, timeout=120)

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
