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
3. ci-template v1.49 (ruling 1051, handbook #83): ci.yml's preflight job
   alone may read vars.CI_PREFLIGHT_RUNS_ON. Its runs-on is checked unset,
   where exactly [self-hosted, linux, x64, bigdisk] passes too, and set to
   the fallback `"ubuntu-latest"`, where it must give a standard image; the
   schedule guard's AGENT_TOKEN is passed off github.com only.
4. ci-template v1.50 and v1.51 (DS-9792): a short queued list is a lower
   bound for the overflow, and every runs-on is also read under
   pull_request_target.
5. ci-template v1.52 (DS-9857, DS-9862, DS-9887): a
   fork's pull request, under pull_request as under pull_request_target,
   never reaches a self-hosted runner, so preflight's runs-on names the
   fork clause before the variable.
6. ci-template v1.53 (ruling 1087): on main the concurrency group is
   `main-{event}`, without the commit sha, so a newer main run cancels the
   older one.
7. ci-template v1.54 (ruling 1137; DS-10280 / ruling 1149: v1.54 ported):
   preflight's `Balanced runner pool (ruling 1137)` step sends a run hosted
   with a ::warning on a short listing or a reply that is not 200, and a
   fork's head to the hosted default with a ::notice.

Run: python3 scripts/test_ci_runner_expr.py
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest

from test_ci_workflow_text import load

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CI = os.path.join(REPO, ".github", "workflows", "ci.yml")
NIGHTLY = os.path.join(REPO, ".github", "workflows", "nightly.yml")
OPEN = "$" "{{"


def policy_script():
    found = [s for job in load(CI)["jobs"].values() for s in job.get("steps", [])
             if s.get("name") == "Runner labels (ruling 633)"]
    if len(found) != 1:
        raise AssertionError("ci.yml has %d runner-label steps" % len(found))
    lines = found[0]["run"].splitlines()
    start = lines.index("python3 -I - <<'PY'")
    return "\n".join(lines[start + 1:lines.index("PY", start)]) + "\n"


def run_policy(workflow, name="x.yml"):
    """Run the runner-label step on a repository holding only this workflow."""
    with tempfile.TemporaryDirectory() as d:
        wf = os.path.join(d, ".github", "workflows")
        os.makedirs(wf)
        with open(os.path.join(wf, name), "w", encoding="utf-8") as f:
            f.write(workflow)
        return subprocess.run(["python3", "-I", "-"], input=policy_script(), cwd=d,
                              capture_output=True, text=True, timeout=60)


def job(runs_on, cond=None, name="j"):
    text = "on:\n  push:\n  pull_request:\njobs:\n  %s:\n    runs-on: %s\n" % (name, runs_on)
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

    def test_waived_evaluated_release_is_checked_as_release(self):
        # #613 round 4: format() evaluates to `release` though its text names none.
        ro = OPEN + " format('{0}{1}', 're', 'lease') }}   # self-hosted: hardware: bench"
        r = run_policy(job(ro))
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("names `release`", r.stdout)
        r = run_policy(job(ro, OPEN + " github.event_name == 'pull_request' }}"))
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("(a)", r.stdout)

    def test_waived_evaluated_release_passes_when_restricted(self):
        ro = OPEN + " format('{0}{1}', 're', 'lease') }}   # self-hosted: hardware: bench"
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


RELEASE_RO = "[self-hosted, release]   # self-hosted: hardware: bench"


class ReleaseGuardBranches(unittest.TestCase):
    """#613 round 5: a release job's `if` is tried under every branch (and, on
    workflow_dispatch, every tag) it names, and under one it does not name."""

    def test_branches_outside_ctx_are_rejected(self):
        for cond in ("github.event_name == 'push' && github.ref == 'refs/heads/develop'",
                     "github.event_name == 'push' && github.ref_name == 'develop'",
                     "github.event_name == 'push' && github.event.ref == 'refs/heads/develop'",
                     "github.event_name == 'push' && github.ref == 'refs/heads/Develop'",
                     "github.event_name == 'push' && github.ref == 'refs/heads/release/1.x'",
                     "github.event_name == 'push' && github.ref_name == 'release/1.x'",
                     "github.event_name == 'push' && startsWith(github.ref, 'refs/heads/rel')",
                     "github.event_name == 'push' && startsWith(github.ref_name, 'rel')",
                     "github.event_name == 'push' && contains(github.ref, 'dev')",
                     "github.event_name == 'push' && endsWith(github.ref_name, '1.x')",
                     "github.event_name == 'push' && contains(github.ref_name, 'a') && contains(github.ref_name, 'b')",
                     "github.event_name == 'push' && github.ref == format('refs/heads/{0}', 'develop')",
                     "github.event_name == 'push' && contains(fromJSON('[\"refs/heads/main\",\"refs/heads/develop\"]'), github.ref)",
                     "github.event_name == 'push' && github.ref_name == 1",
                     "github.event_name == 'push' && github.ref > 'refs/heads/m'",
                     "(github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop') && github.event_name == 'push'",
                     "github.event_name == 'push' && github.ref != 'refs/heads/feature' && github.ref != 'refs/heads/main'",
                     "github.event_name == 'workflow_dispatch' && github.ref == 'refs/heads/develop'",
                     "github.event_name == 'workflow_dispatch' && github.ref_name == 'develop'",
                     "github.event_name == 'workflow_dispatch' && github.ref == 'refs/tags/v2.0.0'"):
            with self.subTest(cond=cond):
                r = run_policy(job(RELEASE_RO, cond))
                self.assertNotEqual(r.returncode, 0, r.stdout)
                self.assertIn("(b) names `release`", r.stdout)

    def test_main_tags_and_dispatch_on_main_pass(self):
        for cond in ("github.event_name == 'push' && github.ref == 'refs/heads/main'",
                     "github.event_name == 'push' && github.ref_name == 'main' && github.ref_type == 'branch'",
                     "github.event_name == 'push' && startsWith(github.ref, 'refs/tags/')",
                     "github.event_name == 'push' && (github.ref == 'refs/heads/main' || startsWith(github.ref, 'refs/tags/v'))",
                     "github.event_name == 'push' && github.ref == 'refs/heads/main' && github.ref != 'refs/heads/develop'",
                     "github.event_name == 'workflow_dispatch' && github.ref == 'refs/heads/main'",
                     "(github.event_name == 'push' || github.event_name == 'workflow_dispatch') && github.ref == 'refs/heads/main'"):
            with self.subTest(cond=cond):
                r = run_policy(job(RELEASE_RO, cond))
                self.assertEqual(r.returncode, 0, r.stdout)

    def test_waived_runs_on_is_tried_on_the_branches_it_names(self):
        # #613 round 6 sweep: a waived runs-on is evaluated on every branch or
        # tag it names, not only on main and one fresh branch.
        for test in ("github.ref == 'refs/heads/develop'", "github.ref_name == 'develop'",
                     "startsWith(github.ref, 'refs/heads/rel')", "github.ref == 'refs/tags/v1'"):
            with self.subTest(test=test):
                ro = OPEN + f" {test} && format('{{0}}{{1}}', 're', 'lease') || 'ubuntu-latest' }}}}   # self-hosted: hardware: bench"
                r = run_policy(job(ro))
                self.assertNotEqual(r.returncode, 0, r.stdout)
                self.assertIn("release", r.stdout)

    def test_review_label_is_tried_on_the_branches_it_names(self):
        ro = OPEN + " github.ref_name == 'develop' && format('{0}{1}', 'co', 'dex') || 'ubuntu-latest' }}"
        r = run_policy(job(ro))
        self.assertNotEqual(r.returncode, 0, r.stdout)


PRE_RO = (OPEN + " github.server_url == 'https://github.com' && (!startsWith(github.event_name, 'pull_request')"
          " || github.event.pull_request.head.repo.full_name == github.repository)"
          " && fromJSON(vars.CI_PREFLIGHT_RUNS_ON"
          " || '[\"self-hosted\",\"linux\",\"x64\",\"bigdisk\"]') || 'ubuntu-latest' }}")
# v1.51's runs-on: no fork clause, so a fork's pull request took the Dells.
PRE_RO_V151 = (OPEN + " github.server_url == 'https://github.com' && fromJSON(vars.CI_PREFLIGHT_RUNS_ON"
               " || '[\"self-hosted\",\"linux\",\"x64\",\"bigdisk\"]') || 'ubuntu-latest' }}")


class PreflightRunsOn(unittest.TestCase):
    """The template's testdata/runner-labels-tests.py part F, mirrored."""

    def test_preflight_bigdisk_or_fallback_passes(self):
        r = run_policy(job(PRE_RO, name="preflight"), "ci.yml")
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_v1_51_preflight_fails_for_a_fork(self):
        # v1.52: without the fork clause a fork's head under pull_request
        # reaches the bigdisk pool, and the check names it.
        r = run_policy(job(PRE_RO_V151, name="preflight"), "ci.yml")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("pull_request: a fork never reaches a self-hosted runner", r.stdout)

    def test_other_job_reading_the_variable_reads_an_unknown(self):
        r = run_policy(job(PRE_RO, name="lint"), "ci.yml")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("(c)", r.stdout)

    def test_preflight_outside_ci_yml_reads_an_unknown(self):
        r = run_policy(job(PRE_RO, name="preflight"), "other.yml")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("(c)", r.stdout)

    def test_unset_must_be_exactly_bigdisk(self):
        ro = PRE_RO.replace('"bigdisk"', '"bigdisk-docker"')
        r = run_policy(job(ro, name="preflight"), "ci.yml")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("CI_PREFLIGHT_RUNS_ON unset", r.stdout)

    def test_set_must_give_a_standard_image(self):
        ro = (OPEN + " fromJSON(vars.CI_PREFLIGHT_RUNS_ON && '\"bigdisk\"'"
              " || '[\"self-hosted\",\"linux\",\"x64\",\"bigdisk\"]') }}")
        r = run_policy(job(ro, name="preflight"), "ci.yml")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn('CI_PREFLIGHT_RUNS_ON set to "ubuntu-latest"', r.stdout)

    def test_repository_preflight_reads_the_variable(self):
        jobs = load(CI)["jobs"]
        self.assertEqual(jobs["preflight"]["runs-on"], PRE_RO)
        bots = [s["env"]["BOT"] for s in jobs["preflight"]["steps"] if "BOT" in s.get("env", {})]
        self.assertEqual(len(bots), 1)
        self.assertTrue(bots[0].startswith("${{ github.server_url != 'https://github.com' && "), bots[0])

    def test_header_names_v1_54(self):
        with open(CI, encoding="utf-8") as f:
            self.assertEqual(f.readline(), "# ci-template: v1.54 (customized)\n")

    def test_main_group_is_per_event(self):
        group = load(CI)["concurrency"]["group"]
        self.assertIn("github.ref == 'refs/heads/main' && format('main-{0}', github.event_name) ||", group)
        self.assertNotIn("github.sha", group)
        self.assertEqual(load(CI)["concurrency"]["cancel-in-progress"], "true")


def step_run(path, name):
    found = [s for job in load(path)["jobs"].values() for s in job.get("steps", [])
             if s.get("name") == name]
    if len(found) != 1:
        raise AssertionError("%s has %d steps named %r" % (path, len(found), name))
    return found[0]["run"]


FAKE_CURL = """#!/bin/sh
# Writes $FAKE_BODY to the -o file and prints $FAKE_CODE, as curl -w '%{http_code}' does.
while [ $# -gt 0 ]; do
  [ "$1" = -o ] && { shift; printf '%s' "$FAKE_BODY" > "$1"; }
  shift
done
printf '%s' "$FAKE_CODE"
"""


@unittest.skipUnless(shutil.which("bash") and shutil.which("jq"), "needs bash and jq")
class OverflowShortList(unittest.TestCase):
    """ci-template v1.54 (ruling 1137): the balanced runner pool falls back to
    the hosted default, with a ::warning on a short listing or a reply that is
    not 200, and with a ::notice for a fork's head."""

    def select(self, event="push", head="", code="200", body='{"total_count": 0, "workflow_runs": []}'):
        run = step_run(CI, "Balanced runner pool (ruling 1137)")
        with tempfile.TemporaryDirectory() as d:
            bindir = os.path.join(d, "bin")
            os.makedirs(bindir)
            curl = os.path.join(bindir, "curl")
            with open(curl, "w", encoding="utf-8") as f:
                f.write(FAKE_CURL)
            os.chmod(curl, 0o755)
            out = os.path.join(d, "out")
            env = dict(os.environ, PATH=bindir + os.pathsep + os.environ.get("PATH", ""),
                       SERVER="https://github.com", EVENT=event, HEAD_REPO=head, REPO="o/r",
                       API="https://api.github.com/repos/o/r/actions", RUN_ID="7", JOB_TOKEN="t",
                       HS="12", C1="2", C2="1", RUNNER_TEMP=d, GITHUB_OUTPUT=out,
                       FAKE_CODE=code, FAKE_BODY=body)
            r = subprocess.run(["bash", "-c", run], env=env, capture_output=True, text=True, timeout=60)
            with open(out, encoding="utf-8") as f:
                outputs = f.read()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return r.stdout, outputs

    def assert_hosted(self, outputs):
        self.assertEqual(outputs, 'runner="ubuntu-latest"\nrunner_docker="ubuntu-latest"\n')

    def test_short_listing_goes_hosted_with_warning(self):
        out, outputs = self.select(body='{"total_count": 3, "workflow_runs": []}')
        self.assertIn("::warning::runner selection: the Actions API gave no complete run list", out)
        self.assertIn("0 of 3 entries", out)
        self.assert_hosted(outputs)

    def test_reply_not_200_goes_hosted_with_warning(self):
        out, outputs = self.select(code="503")
        self.assertIn("::warning::runner selection: the Actions API gave no complete run list", out)
        self.assertIn(": 503)", out)
        self.assert_hosted(outputs)

    def test_fork_head_takes_hosted_default_with_notice(self):
        for event in ("pull_request", "pull_request_target"):
            out, outputs = self.select(event=event, head="fork/r", code="503")
            self.assertIn("::notice::runner selection: this pull request's head repository is not this repository (a fork)", out)
            self.assertNotIn("::warning::", out)
            self.assert_hosted(outputs)

    def test_same_repository_head_reads_the_pool(self):
        out, outputs = self.select(event="pull_request", head="o/r")
        self.assertNotIn("::warning::", out)
        self.assertIn("::notice::runner selection: use hosted 0/12", out)
        self.assert_hosted(outputs)


class PullRequestTarget(unittest.TestCase):
    """ci-template v1.51: every runs-on is also read under pull_request_target."""

    def test_self_hosted_only_under_pull_request_target_is_found(self):
        ro = OPEN + " github.event_name == 'pull_request_target' && 'bigdisk' || 'ubuntu-latest' }}"
        wf = job(ro).replace("on:\n  push:\n  pull_request:\n", "on:\n  pull_request_target:\n")
        r = run_policy(wf)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("pull_request_target", r.stdout)

    def test_hosted_under_pull_request_target_passes(self):
        wf = job("ubuntu-latest").replace("on:\n  push:\n  pull_request:\n", "on:\n  pull_request_target:\n")
        r = run_policy(wf)
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
