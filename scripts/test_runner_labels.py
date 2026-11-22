# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests holding every workflow's runner labels to actionlint's allowlist.

.github/actionlint.yaml lists the self-hosted runner labels the workflows may
use, and actionlint reports any other label as an unknown runner. `make lint`
does not run actionlint yet (#615), so no CI check reads the labels, and a
misspelled self-hosted label would reach main through a green gate. These tests
read the allowlist and every `runs-on:` key, bare or quoted (scalar, list, or
group/labels mapping, a flow list or mapping on one line or across several) of
.github/workflows/*.yml and *.yaml with
the standard library alone (no actionlint binary, no PyYAML) and fail naming
file:line and label for any plain-text label the allowlist and actionlint's
built-in labels do not accept. Built-in labels match in any case (`Linux`,
`X64`), as in actionlint; allowlisted labels match case-sensitively. A label
with a `${{ }}` expression anywhere in it is skipped, as actionlint skips it:
its value is known only when the workflow runs.
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

RUNS_ON = re.compile(r"""^(\s*)(?:-\s+)?["']?runs-on["']?:(.*)$""")
ITEM = re.compile(r"^(\s*)-\s+(.*)$")
ENTRY = re.compile(r"^(\s*)[\"']?([\w-]+)[\"']?:(\s.*)?$")
FLOW_LABELS = re.compile(r"[{,]\s*[\"']?labels[\"']?\s*:\s*(\[[^\]]*\]|[^,}]*)")


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


def _labels(value):
    """Yield the labels of a scalar or flow-list value, skipping any label with
    a `${{ ... }}` expression anywhere in it."""
    items = [value]
    if value.startswith("[") and value.endswith("]"):
        items = value[1:-1].split(",")
    for label in items:
        label = _value(label)
        if label and not 0 <= label.find("${{") < label.find("}}"):
            yield label


def plain_labels(text):
    """Yield (lineno, label) for each plain-text label of each `runs-on:` key.

    The value is a scalar (`codex`), a flow list (`[self-hosted, linux]`), a
    block list on the lines after an empty `runs-on:`, or a mapping whose
    `labels:` entry is one of those (`{group: g, labels: codex}` or `group:` and
    `labels:` keys on the lines after `runs-on:`); `group:` is not a label. The
    key may be quoted (`'runs-on':` or `"runs-on":`), as may the `group:` and
    `labels:` keys of its mapping (`'labels':`, `"group":`), and a flow list or
    mapping that does not close on the `runs-on:` line takes the following
    lines until it closes (an unclosed one yields nothing); its labels carry
    the `runs-on:` line number. A value that starts with `${{` is an
    expression and yields nothing, and a label with a `${{ ... }}` expression
    anywhere in it (`runner-${{ matrix.n }}`) is skipped.
    """
    lines = text.splitlines()
    for i, line in enumerate(lines):
        key = RUNS_ON.match(line)
        if not key:
            continue
        value = _value(key.group(2))
        close = {"[": "]", "{": "}"}.get(value[:1])
        if close and not value.endswith(close):
            for j in range(i + 1, len(lines)):
                value = f"{value} {_value(lines[j])}".strip()
                if value.endswith(close):
                    break
            else:
                continue
        if value.startswith("{") and value.endswith("}"):
            entry = FLOW_LABELS.search(value)
            for label in _labels(_value(entry.group(1)) if entry else ""):
                yield i + 1, label
        elif value:
            for label in _labels(value):
                yield i + 1, label
        else:
            indent = len(key.group(1))
            # `- item` lines at or past list_indent are labels; None after a
            # mapping key whose value is not a block list.
            list_indent = indent
            for j in range(i + 1, len(lines)):
                if not lines[j].strip() or lines[j].lstrip().startswith("#"):
                    continue
                item = ITEM.match(lines[j])
                listed = list_indent is not None
                if item and listed and len(item.group(1)) >= list_indent:
                    for label in _labels(_value(item.group(2))):
                        yield j + 1, label
                    continue
                entry = ENTRY.match(lines[j])
                if not entry or len(entry.group(1)) <= indent:
                    break
                list_indent = None
                if entry.group(2) == "labels":
                    value = _value(entry.group(3) or "")
                    if value:
                        for label in _labels(value):
                            yield j + 1, label
                    else:
                        list_indent = len(entry.group(1))


def unknown(workflow_dir, allowed):
    """Return `file:line: label` for each label neither allowed nor built in."""
    workflow_dir = pathlib.Path(workflow_dir)
    found = []
    paths = [*workflow_dir.glob("*.yml"), *workflow_dir.glob("*.yaml")]
    for path in sorted(paths):
        try:
            name = path.relative_to(ROOT)
        except ValueError:
            name = path.name
        for lineno, label in plain_labels(path.read_text()):
            if label not in allowed and label.lower() not in BUILTIN:
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

    def test_a_mapping_label_list_is_read(self):
        found = self.check(
            "jobs:\n  build:\n    runs-on:\n"
            "      group: g\n      labels: [self-hosted, qwne]\n"
        )
        self.assertEqual(found, ["w.yml:5: qwne"])

    def test_a_mapping_scalar_label_is_read(self):
        found = self.check(
            "jobs:\n  build:\n    runs-on:\n      group: g\n      labels: qwne\n"
        )
        self.assertEqual(found, ["w.yml:5: qwne"])

    def test_a_flow_mapping_is_read(self):
        found = self.check(
            "jobs:\n  build:\n    runs-on: {group: g, labels: codex}\n"
        )
        self.assertEqual(found, [])
        found = self.check(
            "jobs:\n  build:\n    runs-on: {group: g, labels: qwne}\n"
        )
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_yaml_workflow_is_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            pathlib.Path(tmp, "w.yaml").write_text(
                "jobs:\n  review:\n    runs-on: qwne\n"
            )
            found = unknown(tmp, allowlist(ACTIONLINT))
        self.assertEqual(found, ["w.yaml:3: qwne"])

    def test_a_quoted_runs_on_key_is_read(self):
        for key in ("'runs-on'", '"runs-on"'):
            found = self.check(f"jobs:\n  build:\n    {key}: qwne\n")
            self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_flow_value_across_lines_is_read(self):
        found = self.check(
            "jobs:\n  build:\n    runs-on: [self-hosted,\n      qwne]\n"
        )
        self.assertEqual(found, ["w.yml:3: qwne"])
        found = self.check(
            "jobs:\n  build:\n    runs-on: {group: g,\n      labels: qwne}\n"
        )
        self.assertEqual(found, ["w.yml:3: qwne"])
        found = self.check(
            "jobs:\n  build:\n    runs-on: [self-hosted,\n      codex]\n"
        )
        self.assertEqual(found, [])

    def test_a_quoted_mapping_key_is_read(self):
        for key in ("'labels'", '"labels"'):
            found = self.check(
                f"jobs:\n  build:\n    runs-on:\n      group: g\n      {key}: qwne\n"
            )
            self.assertEqual(found, ["w.yml:5: qwne"])
            found = self.check(
                "jobs:\n  build:\n    runs-on:\n      group: g\n"
                f"      {key}:\n        - qwne\n"
            )
            self.assertEqual(found, ["w.yml:6: qwne"])
        found = self.check(
            "jobs:\n  build:\n    runs-on:\n      'group': g\n      labels: qwne\n"
        )
        self.assertEqual(found, ["w.yml:5: qwne"])

    def test_a_quoted_flow_mapping_key_is_read(self):
        for value in ('{group: g, "labels": qwne}', "{'labels': [qwne]}"):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, ["w.yml:3: qwne"])
        found = self.check("jobs:\n  build:\n    runs-on: {'labels': codex}\n")
        self.assertEqual(found, [])

    def test_an_embedded_expression_label_is_skipped(self):
        for value in ("runner-${{ matrix.n }}", '[self-hosted, "x-${{ inputs.h }}"]'):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, [])
        found = self.check('jobs:\n  build:\n    runs-on: "x-}} ${{"\n')
        self.assertEqual(found, ["w.yml:3: x-}} ${{"])

    def test_a_builtin_label_in_any_case_is_accepted(self):
        for value in ("[Self-Hosted, Linux, X64]", "ARM64"):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, [])
        found = self.check("jobs:\n  build:\n    runs-on: Codex\n")
        self.assertEqual(found, ["w.yml:3: Codex"])

    def test_the_allowlist_is_read(self):
        labels = allowlist(ACTIONLINT)
        for label in ("codex", "qwen", "ci-gate"):
            self.assertIn(label, labels)


if __name__ == "__main__":
    unittest.main()
