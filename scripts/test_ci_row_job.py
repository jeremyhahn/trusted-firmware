#!/usr/bin/env python3
"""ci-template v1.55: the Row job (one SPEC row per pull request) in ci.yml.

The Tenant Agent's 12:47Z order (2026-11-22, ruling 1136): a pull request
from a row/<n>-* branch passes when issue #n is named on the BASE commit in a
docs/SPEC.md table row (SPEC only, ruling 1143 (3)); a pull request with an
exempt label (ci, main-fix, std) passes; anything else is red. The script is
read out of the "Row check" step's heredoc in .github/workflows/ci.yml and run
against a temporary git repository whose base commit holds the rows and whose
head commit adds one more. The cases port the handbook template's
testdata/row-job-tests.py:

  R0  the job's shape: pull_request only, CI Gate's runner, the base checked
      out, in CI Gate's needs with a `pull` check line.
  R1  the row in a docs/SPEC.md table: green.
  R2  the row in ROADMAP.md only: red (ruling 1143 (3): SPEC only).
  R3  the row absent: red, and the ::error names the rule.
  R4  an exempt label on a non-row branch: green.
  R5  a non-row branch with no label: red, and the ::error names the rule.
  R6  the row only on the head commit: red (the base is read, not the head).
  R7  #n in SPEC prose, not a table row: red.
  R8  #12 does not match #123.
  R9  owner/repo#n does not count.
  R10 a merge-train/ branch: green.
  R11 the labels API unreachable: a ::warning, the event's labels used.
  R12 no base commit: red.

Run: python3 -B -m unittest scripts/test_ci_row_job.py
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CI = os.path.join(ROOT, ".github", "workflows", "ci.yml")

ENV = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": "/nonexistent",
       "GIT_CONFIG_GLOBAL": "/dev/null", "GIT_CONFIG_NOSYSTEM": "1"}


def ci_text():
    with open(CI, encoding="utf-8") as f:
        return f.read()


def job_text(text):
    i = text.find("\n  row:\n")
    if i < 0:
        raise AssertionError("no row job in " + CI)
    m = re.compile(r"\n  [a-z][\w-]*:\n").search(text, i + 1)
    return text[i:m.start() if m else len(text)]


def script(job):
    step = job.find("- name: Row check")
    if step < 0:
        raise AssertionError("no Row check step")
    i = job.find("python3 -I - <<'PY'\n", step)
    j = job.find("\n          PY\n", i)
    if i < 0 or j < 0:
        raise AssertionError("no heredoc in the Row check step")
    body = job[job.index("\n", i) + 1:j + 1]
    return "\n".join(l[10:] for l in body.splitlines()) + "\n"


def git(repo, *args):
    subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True,
                   env={**ENV, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.com",
                        "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.com"})


def make_repo(tmp):
    r = os.path.join(tmp, "r")
    os.makedirs(os.path.join(r, "docs"))
    git(r, "init", "-q")
    with open(os.path.join(r, "docs", "SPEC.md"), "w", encoding="utf-8") as f:
        f.write("# SPEC\n\nProse names #77 but is not a row.\n\n"
                "| Row | Issue | State |\n|---|---|---|\n"
                "| Store | #41 | done |\n| Seal | #123 | open |\n| Ext | other/repo#55 | open |\n")
    with open(os.path.join(r, "ROADMAP.md"), "w", encoding="utf-8") as f:
        f.write("# ROADMAP\n\n## v1.2\n\n- Barrier rework (#88)\n")
    git(r, "add", "-A")
    git(r, "commit", "-qm", "base")
    base = subprocess.run(["git", "-C", r, "rev-parse", "HEAD"], capture_output=True, text=True,
                          env=ENV).stdout.strip()
    with open(os.path.join(r, "docs", "SPEC.md"), "a", encoding="utf-8") as f:
        f.write("| New | #99 | open |\n")
    git(r, "commit", "-qam", "head adds row 99")
    return r, base


class RowJobShape(unittest.TestCase):
    """R0: the job's shape and its CI Gate wiring."""

    def setUp(self):
        self.text = ci_text()
        self.job = job_text(self.text)
        self.gate = self.text[self.text.find("\n  ci-gate:\n"):]

    def test_r0_ready_pull_request_only(self):
        # The template's condition, plus this file's schedule clause (a no-op
        # on pull_request, required by scripts/test_schedule_guard.py).
        self.assertIn("if: ${{ github.event_name == 'pull_request' && !github.event.pull_request.draft && "
                      "(github.event_name != 'schedule' || needs.preflight.outputs.verdict == 'run') }}", self.job)

    def test_r0_ci_gate_runner(self):
        self.assertIn("fromJSON(needs.preflight.outputs.runner || '\"ubuntu-latest\"')", self.job)
        self.assertIn("needs: [preflight]", self.job)

    def test_r0_base_checked_out_without_credentials(self):
        self.assertIn("ref: ${{ github.event.pull_request.base.sha }}", self.job)
        self.assertIn("persist-credentials: false", self.job)

    def test_r0_exempt_labels_env(self):
        self.assertIn("ROW_EXEMPT_LABELS: 'ci main-fix std'", self.job)

    def test_r0_read_only_token(self):
        self.assertRegex(self.job, r"permissions:\n\s+contents: read\n\s+pull-requests: read\n")

    def test_r0_row_in_ci_gate_needs(self):
        needs = re.search(r"needs: \[([^\]]*)\]", self.gate)
        self.assertIsNotNone(needs)
        self.assertIn("row", [s.strip() for s in needs[1].split(",")])

    def test_r0_ci_gate_checks_row_as_pull(self):
        self.assertRegex(self.gate, r'check row +"\$\{\{ needs\.row\.result \}\}" pull\n')
        self.assertIn('[ "$3" = pull ] && [ "${PULL}" != true ]', self.gate)
        self.assertIn("PULL: ${{ github.event_name == 'pull_request' }}", self.gate)


@unittest.skipUnless(shutil.which("git") and shutil.which("python3"), "needs git and python3")
class RowCheck(unittest.TestCase):
    """R1-R12: the Row check script against a temporary base and head."""

    @classmethod
    def setUpClass(cls):
        cls.body = script(job_text(ci_text()))
        cls.tmp = tempfile.TemporaryDirectory()
        cls.repo, cls.base = make_repo(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_check(self, head, base=None, labels="[]", url=""):
        env = {**ENV, "HEAD_REF": head, "BASE_SHA": self.base if base is None else base,
               "EVENT_LABELS": labels, "LABELS_URL": url,
               "ROW_EXEMPT_LABELS": "ci main-fix std", "ROW_EXEMPT_BRANCHES": "merge-train/"}
        if url:
            env["JOB_" + "TOKEN"] = "placeholder"  # a dummy: the URL never answers
        r = subprocess.run(["python3", "-I", "-"], input=self.body, cwd=self.repo, env=env,
                           capture_output=True, text=True, timeout=60)
        return r.returncode, r.stdout + r.stderr

    def test_r1_row_in_spec_table_green(self):
        rc, out = self.run_check("row/41-store")
        self.assertEqual(rc, 0, out)
        self.assertIn("docs/SPEC.md", out)

    def test_r2_row_in_roadmap_only_red(self):
        rc, out = self.run_check("row/88-barrier")
        self.assertEqual(rc, 1, out)
        self.assertIn("::error::Row: issue #88", out)
        self.assertIn("ROADMAP.md does not count", out)

    def test_r3_row_absent_red_names_rule(self):
        rc, out = self.run_check("row/5-missing")
        self.assertEqual(rc, 1, out)
        self.assertIn("::error::Row: issue #5", out)
        self.assertIn("one SPEC row", out)

    def test_r4_exempt_label_green(self):
        rc, out = self.run_check("fix/lint", labels='["bug", "main-fix"]')
        self.assertEqual(rc, 0, out)
        self.assertIn("exempt label 'main-fix'", out)

    def test_r5_non_row_branch_no_label_red(self):
        rc, out = self.run_check("fix/lint", labels='["bug"]')
        self.assertEqual(rc, 1, out)
        self.assertIn("::error::Row: head branch 'fix/lint' is not row/<n>-*", out)
        self.assertIn("one SPEC row", out)

    def test_r6_row_only_on_head_red(self):
        rc, out = self.run_check("row/99-new")
        self.assertEqual(rc, 1, out)
        self.assertIn("::error::Row: issue #99", out)

    def test_r7_spec_prose_not_a_row_red(self):
        rc, out = self.run_check("row/77-prose")
        self.assertEqual(rc, 1, out)

    def test_r8_short_number_does_not_match_longer(self):
        rc, out = self.run_check("row/12-short")
        self.assertEqual(rc, 1, out)

    def test_r9_other_repo_reference_does_not_count(self):
        rc, out = self.run_check("row/55-other")
        self.assertEqual(rc, 1, out)

    def test_r10_merge_train_branch_green(self):
        rc, out = self.run_check("merge-train/2026-11-22")
        self.assertEqual(rc, 0, out)
        self.assertIn("exempt branch prefix", out)

    def test_r11_labels_api_unreachable_uses_event_labels(self):
        rc, out = self.run_check("chore/x", labels='["std"]', url="http://localhost:9/labels")
        self.assertEqual(rc, 0, out)
        self.assertIn("::warning::Row: the live labels could not be read", out)
        self.assertIn("exempt label 'std'", out)

    def test_r12_no_base_commit_red(self):
        rc, out = self.run_check("row/41-store", base="")
        self.assertEqual(rc, 1, out)
        self.assertIn("::error::Row: no base commit", out)


if __name__ == "__main__":
    unittest.main()
