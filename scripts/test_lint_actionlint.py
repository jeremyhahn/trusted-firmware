"""Tests for `make lint-actionlint` (#615 slice 1).

`make lint` runs actionlint with shellcheck over every workflow
(devops.md section 4), and .github/actionlint.yaml names every self-hosted
label a workflow uses, including the expression fallbacks `bigdisk` and the
publish label `release`.
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HOST_MK = REPO / "mk" / "host.mk"
ALLOWLIST = REPO / ".github" / "actionlint.yaml"
WORKFLOWS = REPO / ".github" / "workflows"

# The quoted label after the last `||` of a runs-on expression: the value a
# job runs on when the expression's first branch is false.
FALLBACK = re.compile(r"\|\|\s*'([^']+)'\s*\}\}")


def allowlist_labels(text):
    """Return the labels under self-hosted-runner.labels in actionlint.yaml."""
    labels = []
    in_labels = False
    for line in text.splitlines():
        stripped = line.split("#", 1)[0].rstrip()
        if not stripped:
            continue
        if stripped.strip() == "labels:":
            in_labels = True
            continue
        if in_labels:
            m = re.match(r"^\s+-\s+(\S+)$", stripped)
            if m:
                labels.append(m.group(1).strip("'\""))
            elif not stripped.startswith(" "):
                in_labels = False
    return labels


def make_rule(text, target):
    """Return a target's prerequisites, with backslash continuations joined."""
    joined = text.replace("\\\n", " ")
    for line in joined.splitlines():
        if line.startswith(target + ":") and not line.startswith(target + ":="):
            return line[len(target) + 1:].split()
    return None


class StructureTest(unittest.TestCase):
    """The make wiring and the allowlist, checked without actionlint."""

    @classmethod
    def setUpClass(cls):
        cls.mk = HOST_MK.read_text(encoding="utf-8")
        cls.labels = allowlist_labels(ALLOWLIST.read_text(encoding="utf-8"))

    def test_lint_runs_actionlint(self):
        prereqs = make_rule(self.mk, "lint")
        self.assertIsNotNone(prereqs, "mk/host.mk has no lint: rule")
        self.assertIn("lint-actionlint", prereqs)

    def test_lint_actionlint_rule_exists(self):
        self.assertIsNotNone(make_rule(self.mk, "lint-actionlint"))

    def test_phony_names_lint_actionlint(self):
        joined = self.mk.replace("\\\n", " ")
        phony = [
            name
            for line in joined.splitlines()
            if line.startswith(".PHONY:")
            for name in line[len(".PHONY:"):].split()
        ]
        self.assertIn("lint-actionlint", phony)

    def test_allowlist_names_bigdisk_and_release(self):
        self.assertIn("bigdisk", self.labels)
        self.assertIn("release", self.labels)

    def test_every_runs_on_fallback_is_allowed(self):
        files = sorted(WORKFLOWS.glob("*.yml"))
        self.assertTrue(files, "no workflows found")
        missing = []
        for wf in files:
            for n, line in enumerate(wf.read_text(encoding="utf-8").splitlines(), 1):
                if "runs-on:" not in line or "${{" not in line:
                    continue
                for label in FALLBACK.findall(line):
                    if label not in self.labels:
                        missing.append(f"{wf.name}:{n}: {label}")
        self.assertEqual(missing, [])

    def test_allowlist_parser_reads_labels(self):
        text = "self-hosted-runner:\n  labels:\n    - a  # x\n    - b\nconfig-variables: null\n"
        self.assertEqual(allowlist_labels(text), ["a", "b"])

    def lint_actionlint_recipe(self):
        lines = self.mk.splitlines()
        start = next(i for i, l in enumerate(lines) if l.startswith("lint-actionlint:"))
        recipe = []
        for line in lines[start + 1:]:
            if not line.startswith("\t"):
                break
            recipe.append(line)
        return "\n".join(recipe)

    def test_recipe_names_the_shellcheck_binary(self):
        self.assertIn("-shellcheck=$(SHELLCHECK)", self.lint_actionlint_recipe())

    def test_workflows_glob_includes_yaml(self):
        line = next(
            (l for l in self.mk.splitlines() if l.startswith("ACTIONLINT_WORKFLOWS")),
            None,
        )
        self.assertIsNotNone(line, "mk/host.mk does not set ACTIONLINT_WORKFLOWS")
        self.assertIn(".github/workflows/*.yml", line)
        self.assertIn(".github/workflows/*.yaml", line)


class MissingShellcheckTest(unittest.TestCase):
    """`make lint-actionlint` refuses to run without shellcheck on PATH.

    ACTIONLINT=false would fail too, so the message must be the shellcheck
    one: the refusal comes before actionlint runs.
    """

    def test_refuses_without_shellcheck(self):
        r = subprocess.run(
            ["make", "-s", "-f", str(REPO / "Makefile"), "-C", str(REPO),
             "lint-actionlint", "SHELLCHECK=/nonexistent/shellcheck",
             "ACTIONLINT=false"],
            capture_output=True, text=True, check=False, timeout=300,
        )
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn(
            "shellcheck (/nonexistent/shellcheck) is not on PATH", r.stderr,
        )


WORKFLOW = """name: scratch
on: push
jobs:
  j:
    runs-on: [self-hosted, {label}]
    steps:
      - run: echo ok
"""


@unittest.skipUnless(
    shutil.which("actionlint") and shutil.which("shellcheck"),
    "actionlint and shellcheck are not on PATH (the toolchain image has both)",
)
class BehaviourTest(unittest.TestCase):
    """`make lint-actionlint` fails on an unknown label and passes a known one."""

    def run_lint(self, label):
        with tempfile.TemporaryDirectory() as tmp:
            wf = os.path.join(tmp, "scratch.yml")
            with open(wf, "w", encoding="utf-8") as f:
                f.write(WORKFLOW.format(label=label))
            return subprocess.run(
                ["make", "-s", "-f", str(REPO / "Makefile"), "-C", str(REPO),
                 "lint-actionlint", f"ACTIONLINT_WORKFLOWS={wf}"],
                capture_output=True, text=True, check=False, timeout=300,
            )

    def test_misspelled_label_fails(self):
        r = self.run_lint("ci-gat")
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ci-gat", r.stdout + r.stderr)

    def test_allowed_label_passes(self):
        r = self.run_lint("ci-gate")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
