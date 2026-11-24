"""Runs the code-review template's v5.12.13 and v5.12.14 suites against .github/workflows/code-review.yml."""
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


class CodeReviewWorkflowTests(unittest.TestCase):
    def run_suite(self, name):
        suite = ROOT / "scripts" / "testdata" / name
        r = subprocess.run([sys.executable, "-I", str(suite), str(ROOT / ".github" / "workflows" / "code-review.yml")],
                           capture_output=True, text=True, timeout=300)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ALL PASS", r.stdout)

    def test_drafts_are_not_reviewed(self):
        self.run_suite("code-review-v51213-tests.py")

    def test_only_code_on_a_row_pr_is_reviewed(self):
        self.run_suite("code-review-v51214-tests.py")


if __name__ == "__main__":
    unittest.main()
