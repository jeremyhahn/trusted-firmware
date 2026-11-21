# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests holding every workflow's runner labels to actionlint's allowlist.

.github/actionlint.yaml lists the self-hosted runner labels the workflows may
use, and actionlint reports any other label as an unknown runner. `make lint`
does not run actionlint yet (#615), so no CI check reads the labels, and a
misspelled self-hosted label would reach main through a green gate. These tests
read the allowlist and every `runs-on:` line of .github/workflows/*.yml with
the standard library alone (no actionlint binary, no PyYAML) and fail naming
file:line and label for any plain-text label the allowlist and actionlint's
built-in labels do not accept. A label inside a `${{ }}` expression is skipped,
as actionlint skips it: its value is known only when the workflow runs.
"""

import pathlib
import re
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
ACTIONLINT = ROOT / ".github" / "actionlint.yaml"
WORKFLOWS = ROOT / ".github" / "workflows"

# The labels actionlint accepts on a self-hosted runner without an allowlist.
BUILTIN = {"self-hosted", "linux", "macos", "windows", "x64", "arm64", "arm"}

RUNS_ON = re.compile(r"^(\s*)(?:-\s+)?runs-on:(.*)$")
ITEM = re.compile(r"^(\s*)-\s+(.*)$")


def _value(text):
    """Return a YAML scalar with its trailing ` # comment` and quotes removed."""
    text = re.split(r"\s+#", " " + text, maxsplit=1)[0].strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        text = text[1:-1]
    return text


def allowlist(path):
    """Return the `- <label>` entries under `labels:` in an actionlint.yaml."""
    labels = []
    indent = None
    for line in pathlib.Path(path).read_text().splitlines():
        if indent is None:
            if re.match(r"^\s*labels:\s*(#.*)?$", line):
                indent = len(line) - len(line.lstrip())
            continue
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        item = ITEM.match(line)
        if not item or len(item.group(1)) < indent:
            break
        labels.append(_value(item.group(2)))
    return labels


def plain_labels(text):
    """Yield (lineno, label) for each plain-text label of each `runs-on:` key.

    The value is a scalar (`codex`), a flow list (`[self-hosted, linux]`) or a
    block list on the lines after an empty `runs-on:`. A value that starts with
    `${{` is an expression and yields nothing.
    """
    lines = text.splitlines()
    for i, line in enumerate(lines):
        key = RUNS_ON.match(line)
        if not key:
            continue
        value = _value(key.group(2))
        if value.startswith("${{"):
            continue
        if value.startswith("[") and value.endswith("]"):
            for label in value[1:-1].split(","):
                label = _value(label)
                if label and not label.startswith("${{"):
                    yield i + 1, label
        elif value:
            yield i + 1, value
        else:
            indent = len(key.group(1))
            for j in range(i + 1, len(lines)):
                if not lines[j].strip() or lines[j].lstrip().startswith("#"):
                    continue
                item = ITEM.match(lines[j])
                if not item or len(item.group(1)) < indent:
                    break
                label = _value(item.group(2))
                if not label.startswith("${{"):
                    yield j + 1, label


def unknown(workflow_dir, allowed):
    """Return `file:line: label` for each label neither allowed nor built in."""
    workflow_dir = pathlib.Path(workflow_dir)
    found = []
    for path in sorted(workflow_dir.glob("*.yml")):
        try:
            name = path.relative_to(ROOT)
        except ValueError:
            name = path.name
        for lineno, label in plain_labels(path.read_text()):
            if label not in allowed and label not in BUILTIN:
                found.append(f"{name}:{lineno}: {label}")
    return found


class RunnerLabelsTest(unittest.TestCase):
    def check(self, workflow):
        with tempfile.TemporaryDirectory() as tmp:
            pathlib.Path(tmp, "w.yml").write_text(workflow)
            return unknown(tmp, allowlist(ACTIONLINT))

    def test_every_workflow_label_is_allowlisted(self):
        self.assertEqual(unknown(WORKFLOWS, allowlist(ACTIONLINT)), [])

    def test_a_misspelled_label_is_reported(self):
        found = self.check("jobs:\n  review:\n    runs-on: qwne  # typo\n")
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_list_label_missing_from_the_allowlist_is_reported(self):
        found = self.check(
            "jobs:\n  build:\n    runs-on: [self-hosted, linux, x64, release]\n"
        )
        self.assertEqual(found, ["w.yml:3: release"])

    def test_an_expression_label_is_skipped(self):
        found = self.check(
            "jobs:\n  build:\n"
            "    runs-on: ${{ cond && 'ubuntu-latest' || 'nolabel' }}\n"
        )
        self.assertEqual(found, [])

    def test_the_allowlist_is_read(self):
        labels = allowlist(ACTIONLINT)
        for label in ("codex", "qwen", "ci-gate"):
            self.assertIn(label, labels)


if __name__ == "__main__":
    unittest.main()
