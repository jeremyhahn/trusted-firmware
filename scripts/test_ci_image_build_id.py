#!/usr/bin/env python3
"""Every ci.yml job that builds the integration image builds it under its own
temporary tag (#613, Codex round 4).

mk/docker.mk builds to <IT_IMAGE>-$(IMAGE_BUILD_ID), tags that <IT_IMAGE> and
removes the temporary tag. IMAGE_BUILD_ID defaults to `build`, so two jobs on
one shared daemon that leave it unset share the temporary tag and one can
remove it before the other tags it. Integration and Soak run on the shared
Docker pools and both build the image on a schedule or dispatch run.

    python3 scripts/test_ci_image_build_id.py
"""

import os
import re
import unittest

from test_ci_workflow_text import load

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CI = os.path.join(REPO, ".github", "workflows", "ci.yml")
DOCKER_MK = os.path.join(REPO, "mk", "docker.mk")


def builds_it(job):
    return any(re.search(r"\bmake\b[^\n]*\bdocker-build-it\b", s.get("run", ""))
               for s in job.get("steps", []))


def render(value, key):
    """The value a job's IMAGE_BUILD_ID takes in run R, attempt A."""
    subs = {"github.run_id": "R", "github.run_attempt": "A", "github.job": key, "matrix.leg": "L"}

    def one(m):
        expr = m.group(1).strip()
        if expr not in subs:
            raise AssertionError("%s: IMAGE_BUILD_ID reads %r, which this test cannot render" % (key, expr))
        return subs[expr]

    return re.sub(r"\$\{\{(.*?)\}\}", one, str(value))


class ImageBuildId(unittest.TestCase):
    def setUp(self):
        self.jobs = load(CI)["jobs"]
        self.it = {k: j for k, j in self.jobs.items() if builds_it(j)}

    def build_id(self, key):
        return (self.jobs[key].get("env") or {}).get("IMAGE_BUILD_ID")

    def test_integration_and_soak_build_the_image(self):
        self.assertIn("integration", self.it)
        self.assertIn("soak", self.it)

    def test_docker_mk_default_is_shared(self):
        with open(DOCKER_MK, encoding="utf-8") as f:
            self.assertRegex(f.read(), r"(?m)^IMAGE_BUILD_ID\s*\?=\s*build\s*$")

    def test_integration_and_soak_set_a_build_id(self):
        for key in ("integration", "soak"):
            with self.subTest(job=key):
                v = self.build_id(key)
                self.assertIsNotNone(v, "%s leaves IMAGE_BUILD_ID at docker.mk's shared `build`" % key)
                for part in ("github.run_id", "github.run_attempt", "github.job"):
                    self.assertIn(part, v, "%s: IMAGE_BUILD_ID does not name %s" % (key, part))

    def test_build_ids_are_distinct(self):
        seen = {}
        for key in self.it:
            v = self.build_id(key)
            self.assertIsNotNone(v, "%s builds the integration image without IMAGE_BUILD_ID" % key)
            r = render(v, key)
            self.assertNotEqual(r, "build", key)
            self.assertNotIn(r, seen, "%s and %s share the temporary tag -%s" % (seen.get(r), key, r))
            seen[r] = key


if __name__ == "__main__":
    unittest.main()
