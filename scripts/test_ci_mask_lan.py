#!/usr/bin/env python3
"""scripts/ci-mask-lan.sh takes a registry from a secret only (#611).

The handbook's public-hygiene rule: an internal value is a configuration
input, and a private one comes from the host env file or an Actions secret,
never a repository variable (a variable is printed in run logs). The mask
step reads SECRETS_<NAME> from secrets.<NAME>; the script masks the value, and
fails with an ::error naming the secret when the job requires it and it is
unset.

    M1  a secret-named registry is masked, its host:port and host too
    M2  a required secret that is unset fails with an ::error naming it
    M3  ci.yml reads no vars.*_REGISTRY, and each mask step reads the secret
    M4  a host name is matched case-insensitively with a terminal dot ignored;
        the masks keep the entry's own spelling

    python3 scripts/test_ci_mask_lan.py
"""

import os
import re
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.path.join(HERE, "ci-mask-lan.sh")
CI = os.path.join(REPO, ".github", "workflows", "ci.yml")


def mask(**env):
    env.setdefault("PATH", os.environ.get("PATH", "/usr/bin:/bin"))
    return subprocess.run(["bash", SCRIPT], env=env, capture_output=True, text=True)


class MaskLan(unittest.TestCase):
    def test_m1_secret_registry_is_masked(self):
        r = mask(SECRETS_CI_IMAGE_REGISTRY="registry.example.com:5000/team")
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = r.stdout.splitlines()
        for want in ("registry.example.com:5000/team", "registry.example.com:5000", "registry.example.com"):
            self.assertIn("::add-mask::" + want, lines)

    def test_m1_private_registry_still_masked(self):
        r = mask(CI_REGISTRY="10.0.0.5:5000")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("::add-mask::10.0.0.5", r.stdout.splitlines())

    def test_m2_required_secret_unset_fails(self):
        for env in ({"SECRETS_CI_IMAGE_REGISTRY": ""}, {}):
            with self.subTest(env=env):
                r = mask(CI_MASK_REQUIRE="SECRETS_CI_IMAGE_REGISTRY", **env)
                self.assertNotEqual(r.returncode, 0)
                self.assertRegex(r.stdout, r"(?m)^::error::.*\bCI_IMAGE_REGISTRY\b")
                self.assertNotIn("::add-mask::", r.stdout)

    def test_m2_unrequired_secret_unset_passes(self):
        r = mask(SECRETS_CI_IMAGE_REGISTRY="")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, "")

    def test_m4_host_case_and_terminal_dot(self):
        cases = (
            ({"DEVCACHE_GO": "http://cache.INTERNAL:3000/go"},
             {"http://cache.INTERNAL:3000/go", "cache.INTERNAL:3000", "cache.INTERNAL"}),
            ({"CI_IMAGE_REGISTRY": "registry.lan.:5000"}, {"registry.lan.:5000", "registry.lan."}),
            ({"RELEASE_REGISTRY": "Pkgs.Home.Arpa."}, {"Pkgs.Home.Arpa."}),
            ({"GOPROXY": "http://buildcache.:3000"},
             {"http://buildcache.:3000", "buildcache.:3000", "buildcache."}),
            ({"GOPROXY": "https://Proxy.Golang.Org.,DIRECT"}, set()),
            ({"GITHUB_SERVER_URL": "https://github.com"}, set()),
        )
        for env, want in cases:
            with self.subTest(env=env):
                r = mask(**env)
                self.assertEqual(r.returncode, 0, r.stderr)
                got = {l[len("::add-mask::"):] for l in r.stdout.splitlines() if l.startswith("::add-mask::")}
                self.assertEqual(got, want)

    def test_m3_ci_reads_registry_from_secrets_only(self):
        with open(CI, encoding="utf-8") as f:
            text = f.read()
        self.assertNotRegex(text, r"vars\.\w*_REGISTRY")
        steps = re.findall(r"(?ms)^ *- name: Mask the LAN addresses\n(.*?)^ *run: scripts/ci-mask-lan\.sh", text)
        self.assertTrue(steps, "no mask step in ci.yml")
        for body in steps:
            self.assertRegex(body, r"(?m)^ *SECRETS_CI_IMAGE_REGISTRY: \$\{\{ secrets\.CI_IMAGE_REGISTRY \}\}$")


if __name__ == "__main__":
    unittest.main()
