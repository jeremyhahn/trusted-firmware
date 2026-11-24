"""Executable tests of code-review.yml's review gate and newer-gate handoff.

Codex round 1 P1 on #1046: "The new review-gate behavior lacks executable
regression coverage."

The handbook's own template tests run unchanged against the repository's
.github/workflows/code-review.yml (byte-identical to the handbook template,
v5.12.12). Each extracts the gate and handoff steps and runs them in bash
against a fake GitHub Actions API.

scripts/testdata/code-review-v51212-tests.py:
  W  the gate fails closed while a twin review runs, approval or not
  C  a cancelled review is no review
  P  pagination: an API error on page 2 is a warning, nothing is cancelled
  H  the handoff executed: a newer run's red or cancelled gate is re-run
     once, bound exit 5, an API error is a warning
scripts/testdata/code-review-v51211-tests.py:
  A  the scope guard runs wherever the gate runs; a failed or cancelled
     scope fails the gate closed
  P  the handoff waits for a newer run to end, re-runs its red gate once,
     fails at the bound
  H  the hold: needs-owner or needs-coordinator on the live PR refuses an
     approved head
  (also S, G get_all pagination, B same_base, N controls)
scripts/testdata/code-review-v51210-tests.py:
  the gate runs as `Code Review (codex)` on every non-draft event (S, G,
  K, B, P, N); code-review-v51211-tests.py loads its expression evaluator
  from this file by path
"""

import os
import shutil
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "code-review.yml")
TESTDATA = os.path.join(ROOT, "scripts", "testdata")


def _missing_tool():
    for tool in ("bash", "jq"):
        if shutil.which(tool) is None:
            return tool
    return None


MISSING = _missing_tool()


@unittest.skipUnless(MISSING is None, f"{MISSING} not on PATH")
class CodeReviewGate(unittest.TestCase):
    def _run(self, name):
        script = os.path.join(TESTDATA, name)
        proc = subprocess.run(
            [sys.executable, "-I", script, WORKFLOW],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=300,
            check=False,
        )
        tail = "\n".join((proc.stdout + proc.stderr).splitlines()[-40:])
        self.assertEqual(proc.returncode, 0, f"{name} rc={proc.returncode}\n{tail}")
        self.assertIn("ALL PASS", proc.stdout, f"{name}\n{tail}")

    def test_v51210(self):
        self._run("code-review-v51210-tests.py")

    def test_v51211(self):
        self._run("code-review-v51211-tests.py")

    def test_v51212(self):
        self._run("code-review-v51212-tests.py")


if __name__ == "__main__":
    unittest.main()
