#!/usr/bin/env python3
"""Checks on the runner-label policy and the nightly misconfiguration scans
(#613, Codex round 3).

1. The `Runner labels (ruling 633)` step keeps an unknown left operand of
   `||`: GitHub returns the left operand when it is truthy, so
   `vars.CI_RUNNER || 'ubuntu-latest'` may select any runner. Such a runs-on
   is not a standard hosted image (c), and with a waiver it is still checked
   as a possible `release` job.
2. In nightly.yml, both misconfiguration-scan steps after the Trivy
   vulnerability gate carry a status function (`!cancelled()`), so a failed
   gate does not skip them, and each checks its scanner prerequisite.

Run: python3 scripts/test_ci_runner_expr.py
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
OPEN = "$" "{{"


def load(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def policy_script():
    found = [s for job in load(CI)["jobs"].values() for s in job.get("steps", [])
             if s.get("name") == "Runner labels (ruling 633)"]
    if len(found) != 1:
        raise AssertionError("ci.yml has %d runner-label steps" % len(found))
    lines = found[0]["run"].splitlines()
    start = lines.index("python3 -I - <<'PY'")
    return "\n".join(lines[start + 1:lines.index("PY", start)]) + "\n"


def run_policy(workflow):
    """Run the runner-label step on a repository holding only this workflow."""
    with tempfile.TemporaryDirectory() as d:
        wf = os.path.join(d, ".github", "workflows")
        os.makedirs(wf)
        with open(os.path.join(wf, "x.yml"), "w", encoding="utf-8") as f:
            f.write(workflow)
        return subprocess.run(["python3", "-I", "-"], input=policy_script(), cwd=d,
                              capture_output=True, text=True, timeout=60)


def job(runs_on, cond=None):
    text = "on:\n  push:\n  pull_request:\njobs:\n  j:\n    runs-on: %s\n" % runs_on
    if cond:
        text += "    if: %s\n" % cond
    return text + "    steps:\n      - run: true\n"


class RunnerExpression(unittest.TestCase):
    def test_variable_fallback_is_not_hosted(self):
        r = run_policy(job(OPEN + " vars.CI_RUNNER || 'ubuntu-latest' }}"))
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("(c)", r.stdout)

    def test_parenthesised_fallback_is_not_hosted(self):
        r = run_policy(job(OPEN + " (vars.CI_RUNNER) || ('ubuntu-latest') }}"))
        self.assertNotEqual(r.returncode, 0, r.stdout)

    def test_waived_variable_fallback_is_checked_as_release(self):
        ro = OPEN + " vars.CI_RUNNER || 'ubuntu-latest' }}   # self-hosted: hardware: bench"
        r = run_policy(job(ro))
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("release", r.stdout)
        r = run_policy(job(ro, OPEN + " github.event_name == 'pull_request' }}"))
        self.assertNotEqual(r.returncode, 0, r.stdout)

    def test_waived_variable_fallback_passes_when_restricted(self):
        ro = OPEN + " vars.CI_RUNNER || 'ubuntu-latest' }}   # self-hosted: hardware: bench"
        cond = "github.event_name == 'push' && github.ref == 'refs/heads/main'"
        r = run_policy(job(ro, cond))
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_known_operands_still_pass(self):
        for ro in ("ubuntu-latest",
                   OPEN + " github.server_url == 'https://github.com' && 'ubuntu-latest' || 'bigdisk' }}"):
            with self.subTest(runs_on=ro):
                r = run_policy(job(ro))
                self.assertEqual(r.returncode, 0, r.stdout)

    def test_repository_passes(self):
        r = subprocess.run(["python3", "-I", "-"], input=policy_script(), cwd=REPO,
                           capture_output=True, text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stdout)


class NightlyMisconfigAfterGate(unittest.TestCase):
    def steps(self):
        for job in load(NIGHTLY)["jobs"].values():
            steps = job.get("steps", [])
            if any(s.get("name", "").startswith("Trivy misconfiguration scan") for s in steps):
                return steps
        raise AssertionError("nightly.yml has no misconfiguration scan")

    def test_scans_run_after_a_failed_gate(self):
        steps = self.steps()
        gates = [i for i, s in enumerate(steps) if s.get("name", "").startswith("Trivy gate")]
        self.assertTrue(gates)
        scans = [s for s in steps[max(gates) + 1:] if s.get("name", "").startswith("Trivy misconfiguration scan")]
        self.assertEqual(len(scans), 2)
        ids = {s.get("id") for s in steps}
        for s in scans:
            with self.subTest(step=s["name"]):
                cond = str(s.get("if", ""))
                self.assertIn("!cancelled()", cond)
                needed = re.findall(r"steps\.([A-Za-z0-9_-]+)\.outcome == 'success'", cond)
                self.assertTrue(needed, "no scanner prerequisite in %r" % cond)
                for n in needed:
                    self.assertIn(n, ids)

    def test_gates_still_fail_the_job(self):
        for s in self.steps():
            if s.get("name", "").startswith("Trivy gate"):
                with self.subTest(step=s["name"]):
                    self.assertNotIn("continue-on-error", s)
                    self.assertNotIn("cancelled()", str(s.get("if", "")))
                    self.assertNotIn("always()", str(s.get("if", "")))


if __name__ == "__main__":
    unittest.main()
