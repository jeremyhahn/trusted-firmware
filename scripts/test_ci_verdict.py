#!/usr/bin/env python3
"""Checks on the CI verdict handling and nightly setup-go (#613, Codex round 2).

1. Every actions/setup-go step in nightly.yml names the nested lock files
   (xkms/go.sum, sdk/go/go.sum) as cache-dependency-path, or disables the
   cache: the repository has no root go.sum.
2. CI Gate fails on the preflight's `refused` verdict on every event, and the
   image, lint-gates and integration jobs start only on a `run` verdict.
3. The preflight guard step defines no function that nothing calls, and
   publishes no `proof` output.

Run: python3 scripts/test_ci_verdict.py
"""

import os
import re
import subprocess
import tempfile
import unittest

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CI = os.path.join(REPO, ".github", "workflows", "ci.yml")
NIGHTLY = os.path.join(REPO, ".github", "workflows", "nightly.yml")
LOCKS = ("xkms/go.sum", "sdk/go/go.sum")


def load(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def gate_script():
    steps = load(CI)["jobs"]["ci-gate"]["steps"]
    found = [s for s in steps if s.get("name") == "Fail closed on any non-success"]
    if len(found) != 1:
        raise AssertionError("CI Gate has %d fail-closed steps" % len(found))
    return found[0]["run"]


def run_gate(verdict, schedule="false", docs_only="false"):
    """Run CI Gate's script with every needed job green."""
    script = re.sub(r"\$\{\{ needs\.[a-z0-9-]+\.result \}\}", "success", gate_script())
    if "${{" in script:
        raise AssertionError("gate script has an unhandled expression")
    env = dict(os.environ, FULL_TIER="true", NIGHTLY="false", DRAFT="false",
               SCHEDULE=schedule, SCHEDULE_GUARD=verdict, DOCS_ONLY=docs_only,
               ESP32_CHANGED="false", ESP32_PR="true", OPTEE_CHANGED="false")
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "gate.sh")
        with open(path, "w", encoding="utf-8") as f:
            f.write(script)
        return subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", path],
                              cwd=d, env=env, capture_output=True, text=True, timeout=60)


class NightlySetupGoCache(unittest.TestCase):
    def test_every_setup_go_names_the_lock_files(self):
        seen = 0
        for name, job in load(NIGHTLY)["jobs"].items():
            for s in job.get("steps", []):
                if not str(s.get("uses", "")).startswith("actions/setup-go@"):
                    continue
                seen += 1
                with self.subTest(job=name):
                    w = s.get("with") or {}
                    if w.get("cache") is False or str(w.get("cache")).lower() == "false":
                        continue
                    paths = str(w.get("cache-dependency-path", "")).split()
                    for lock in LOCKS:
                        self.assertIn(lock, paths)
                        self.assertTrue(os.path.isfile(os.path.join(REPO, lock)), lock)
        self.assertGreater(seen, 0, "nightly.yml has no setup-go step")


class GateRejectsRefused(unittest.TestCase):
    def test_refused_fails_on_every_event(self):
        for schedule in ("false", "true"):
            for docs_only in ("false", "true"):
                with self.subTest(schedule=schedule, docs_only=docs_only):
                    r = run_gate("refused", schedule, docs_only)
                    self.assertNotEqual(r.returncode, 0, r.stdout)
                    self.assertIn("refused", r.stdout)

    def test_run_passes(self):
        for schedule in ("false", "true"):
            with self.subTest(schedule=schedule):
                r = run_gate("run", schedule)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_jobs_start_only_on_run(self):
        jobs = load(CI)["jobs"]
        for name in ("image", "lint-gates", "integration"):
            with self.subTest(job=name):
                cond = jobs[name]["if"]
                self.assertIn("needs.preflight.outputs.verdict == 'run'", cond)
                self.assertNotIn("github.event_name != 'schedule' || needs.preflight.outputs.verdict", cond)


class GuardHasNoDeadCode(unittest.TestCase):
    def guard(self):
        steps = load(CI)["jobs"]["preflight"]["steps"]
        found = [s for s in steps if s.get("id") == "guard"]
        self.assertEqual(len(found), 1)
        return found[0]["run"]

    def test_every_function_is_called(self):
        script = self.guard()
        code = "\n".join(l for l in script.splitlines() if not l.lstrip().startswith("#"))
        for fn in re.findall(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\(\) \{", code, re.MULTILINE):
            with self.subTest(function=fn):
                calls = re.findall(r"(?<![A-Za-z0-9_$])%s(?![A-Za-z0-9_(])" % fn, code)
                self.assertGreater(len(calls), 0, "%s() is never called" % fn)

    def test_no_proof_output(self):
        outputs = load(CI)["jobs"]["preflight"]["outputs"]
        self.assertNotIn("proof", outputs)
        self.assertNotIn("proven", self.guard())


if __name__ == "__main__":
    unittest.main()
