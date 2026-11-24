"""Runs the code-review template's v5.12.13 suite (drafts are not reviewed) against .github/workflows/code-review.yml."""
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


class CodeReviewDraftTests(unittest.TestCase):
    def test_drafts_are_not_reviewed(self):
        suite = ROOT / "scripts" / "testdata" / "code-review-v51213-tests.py"
        r = subprocess.run([sys.executable, "-I", str(suite), str(ROOT / ".github" / "workflows" / "code-review.yml")],
                           capture_output=True, text=True, timeout=300)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ALL PASS", r.stdout)


if __name__ == "__main__":
    unittest.main()
