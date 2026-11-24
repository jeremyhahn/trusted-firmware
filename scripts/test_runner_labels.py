# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests holding every workflow's runner labels to actionlint's allowlist.

.github/actionlint.yaml lists the self-hosted runner labels the workflows may use, and actionlint
reports any other label as an unknown runner. These tests hold every plain-text label to that
allowlist with the standard library alone (no actionlint binary, no PyYAML), so `make scripts-test`
catches a misspelled self-hosted label on any host; `make lint-actionlint` checks the same in the
toolchain image. The tests read the allowlist and every `runs-on:` key, bare or quoted (scalar,
list, or group/labels mapping, a flow list or mapping on one line or across several, and a scalar
or flow value on the line after an empty `runs-on:` or `labels:` key; a flow `labels:` or
next-line value may also span lines; a key may have spaces before its colon, `runs-on : codex`; a
value's anchor `&r` or tag `!!str` is dropped and an alias `*r` skipped) of
.github/workflows/*.yml and *.yaml, and fail naming file:line and label for any plain-text label
the allowlist and actionlint's built-in labels do not accept. The built-in labels, the self-hosted
ones (`linux`, `x64`) and the GitHub-hosted ones (`ubuntu-24.04`, `macos-14`), match in any case
(`Linux`, `Windows-Latest`), as in actionlint. Allowlist entries are glob patterns (`gpu-*`)
matched case-sensitively with fnmatch; actionlint's Go path.Match differs only for `/`, `\\`
escapes and `[^...]`, none of which a runner label uses. A label with a `${{ }}` expression
anywhere in it is skipped, as actionlint skips it: its value is known only when the workflow runs.
"""

import fnmatch
import pathlib
import re
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
ACTIONLINT = ROOT / ".github" / "actionlint.yaml"
WORKFLOWS = ROOT / ".github" / "workflows"

# The labels actionlint accepts on a self-hosted runner without an allowlist.
BUILTIN = {"self-hosted", "linux", "macos", "windows", "x64", "arm64", "arm"}

# The GitHub-hosted runner labels actionlint accepts without an allowlist:
# allGitHubHostedRunnerLabels in actionlint v1.7.12 rule_runner_label.go,
# lines 33-61.
GITHUB_HOSTED = {
    "windows-latest",
    "windows-latest-8-cores",
    "windows-2025",
    "windows-2025-vs2026",
    "windows-2022",
    "windows-11-arm",
    "ubuntu-slim",
    "ubuntu-latest",
    "ubuntu-latest-4-cores",
    "ubuntu-latest-8-cores",
    "ubuntu-latest-16-cores",
    "ubuntu-24.04",
    "ubuntu-24.04-arm",
    "ubuntu-22.04",
    "ubuntu-22.04-arm",
    "macos-latest",
    "macos-latest-xlarge",
    "macos-latest-large",
    "macos-26-intel",
    "macos-26-xlarge",
    "macos-26-large",
    "macos-26",
    "macos-15-intel",
    "macos-15-xlarge",
    "macos-15-large",
    "macos-15",
    "macos-14-xlarge",
    "macos-14-large",
    "macos-14",
}

RUNS_ON = re.compile(r"""^(\s*)(?:-\s+)?["']?runs-on["']?\s*:(.*)$""")
ITEM = re.compile(r"^(\s*)-\s+(.*)$")
ENTRY = re.compile(r"^(\s*)[\"']?([\w-]+)[\"']?\s*:(\s.*)?$")
FLOW_LABELS = re.compile(r"[{,]\s*[\"']?labels[\"']?\s*:\s*(\[[^\]]*\]|[^,}]*)")
PROPS = re.compile(r"^(?:[&!]\S*(?:\s+|$))+")


def _value(text):
    """Return a YAML scalar with its trailing ` # comment` and quotes removed."""
    text = re.split(r"\s+#", " " + text, maxsplit=1)[0].strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        text = text[1:-1]
    return text


def _node(text):
    """Return a value as _value returns it, its leading anchor and tag
    removed; None for an alias, whose labels are read where its anchor is."""
    text = PROPS.sub("", _value(text), count=1)
    return None if text.startswith("*") else _value(text)


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
        label = _node(label) or ""
        if label and not 0 <= label.find("${{") < label.find("}}"):
            yield label


def _value_labels(value):
    """Yield the labels of a scalar, flow-list or flow-mapping value; of a
    flow mapping only its `labels:` entry counts."""
    if value.startswith("{") and value.endswith("}"):
        entry = FLOW_LABELS.search(value)
        value = _value(entry.group(1)) if entry else ""
    yield from _labels(value)


def _flow(lines, i, value):
    """Return (value, last): a flow list or mapping `value` opened on line i
    joined with the following lines up to `last`, the line where it closes;
    (value, i) when it is not a flow or closes on line i, (None, len(lines))
    when it never closes."""
    close = {"[": "]", "{": "}"}.get(value[:1])
    if not close or value.endswith(close):
        return value, i
    for j in range(i + 1, len(lines)):
        value = f"{value} {_value(lines[j])}".strip()
        if value.endswith(close):
            return value, j
    return None, len(lines)


def plain_labels(text):
    """Yield (lineno, label) for each plain-text label of each `runs-on:` key.

    The value is a scalar (`codex`), a flow list (`[self-hosted, linux]`), a
    block list on the lines after an empty `runs-on:`, or a mapping whose
    `labels:` entry is one of those (`{group: g, labels: codex}` or `group:` and
    `labels:` keys on the lines after `runs-on:`); `group:` is not a label. A
    scalar or flow value of an empty `runs-on:` or `labels:` key may sit on
    the next, deeper-indented line (`runs-on:` then `  codex`); a plain scalar
    folded over several lines is not read. The
    key may be quoted (`'runs-on':` or `"runs-on":`), as may the `group:` and
    `labels:` keys of its mapping (`'labels':`, `"group":`); each of these
    keys may have spaces before its colon (`runs-on : codex`). A flow list or
    mapping that does not close on its line, whether it is the `runs-on:`
    value, a `labels:` value or the next-line value of an empty key, takes the
    following lines until it closes (an unclosed one yields nothing); its
    labels carry the line it opens on. A value that starts with `${{` is an
    expression and yields nothing, and a label with a `${{ ... }}` expression
    anywhere in it (`runner-${{ matrix.n }}`) is skipped. A value's leading
    anchor (`&r`) and tag (`!!str`) are dropped, and an alias (`*r`) yields
    nothing, its labels read at its anchor (an anchor outside a `runs-on:`
    key is not read).
    """
    lines = text.splitlines()
    for i, line in enumerate(lines):
        key = RUNS_ON.match(line)
        if not key:
            continue
        value = _node(key.group(2))
        if value is not None:
            value, _ = _flow(lines, i, value)
        if value is None:
            continue
        if value:
            for label in _value_labels(value):
                yield i + 1, label
        else:
            indent = len(key.group(1))
            # `- item` lines at or past list_indent are labels; None after a
            # mapping key whose value is not a block list.
            list_indent = indent
            # The last line of a flow value already read.
            last = i
            for j in range(i + 1, len(lines)):
                if j <= last:
                    continue
                if not lines[j].strip() or lines[j].lstrip().startswith("#"):
                    continue
                item = ITEM.match(lines[j])
                listed = list_indent is not None
                if item and listed and len(item.group(1)) >= list_indent:
                    for label in _labels(_value(item.group(2))):
                        yield j + 1, label
                    continue
                entry = ENTRY.match(lines[j])
                depth = len(lines[j]) - len(lines[j].lstrip())
                if listed and not item and not entry and depth > list_indent:
                    # The value of the empty key, on the next line.
                    value = _node(lines[j])
                    if value is not None:
                        value, last = _flow(lines, j, value)
                    for label in _value_labels(value or ""):
                        yield j + 1, label
                    list_indent = None
                    continue
                if not entry or len(entry.group(1)) <= indent:
                    break
                list_indent = None
                if entry.group(2) == "labels":
                    value = _node(entry.group(3) or "")
                    if value is not None:
                        value, last = _flow(lines, j, value)
                    if value is None:
                        break
                    if value:
                        for label in _labels(value):
                            yield j + 1, label
                    else:
                        list_indent = len(entry.group(1))


def unknown(workflow_dir, allowed):
    """Return `file:line: label` for each label neither built in nor matched
    by an allowlist glob pattern."""
    workflow_dir = pathlib.Path(workflow_dir)
    found = []
    paths = [*workflow_dir.glob("*.yml"), *workflow_dir.glob("*.yaml")]
    for path in sorted(paths):
        try:
            name = path.relative_to(ROOT)
        except ValueError:
            name = path.name
        for lineno, label in plain_labels(path.read_text()):
            if label.lower() not in BUILTIN | GITHUB_HOSTED and not any(
                fnmatch.fnmatchcase(label, p) for p in allowed
            ):
                found.append(f"{name}:{lineno}: {label}")
    return found


class RunnerLabelsTest(unittest.TestCase):
    def check(self, workflow, allowed=None):
        if allowed is None:
            allowed = allowlist(ACTIONLINT)
        with tempfile.TemporaryDirectory() as tmp:
            pathlib.Path(tmp, "w.yml").write_text(workflow)
            return unknown(tmp, allowed)

    def test_every_workflow_label_is_allowlisted(self):
        self.assertEqual(unknown(WORKFLOWS, allowlist(ACTIONLINT)), [])

    def test_a_misspelled_label_is_reported(self):
        self.assertNotIn(
            "qwne", allowlist(ACTIONLINT), "fixture label is now allowlisted"
        )
        found = self.check("jobs:\n  review:\n    runs-on: qwne  # typo\n")
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_list_label_missing_from_the_allowlist_is_reported(self):
        self.assertNotIn(
            "publish", allowlist(ACTIONLINT), "fixture label is now allowlisted"
        )
        found = self.check(
            "jobs:\n  build:\n    runs-on: [self-hosted, linux, x64, publish]\n"
        )
        self.assertEqual(found, ["w.yml:3: publish"])

    def test_an_allowlisted_list_label_is_accepted(self):
        for label in ("release", "bigdisk"):
            with self.subTest(label=label):
                found = self.check(
                    "jobs:\n  build:\n"
                    f"    runs-on: [self-hosted, linux, x64, {label}]\n"
                )
                self.assertEqual(found, [])

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

    def test_a_value_on_the_next_line_is_read(self):
        for value in ("qwne", '"qwne"', "[self-hosted, qwne]", "{group: g, labels: qwne}"):
            found = self.check(f"jobs:\n  build:\n    runs-on:\n      {value}\n")
            self.assertEqual(found, ["w.yml:4: qwne"], value)
        found = self.check(
            "jobs:\n  build:\n    runs-on:\n      [self-hosted, linux]\n"
            "    steps:\n      - run: echo\n"
        )
        self.assertEqual(found, [])

    def test_a_labels_value_on_the_next_line_is_read(self):
        head = "jobs:\n  build:\n    runs-on:\n      group: g\n      labels:\n"
        for value in ("qwne", "[qwne]"):
            found = self.check(f"{head}        {value}\n")
            self.assertEqual(found, ["w.yml:6: qwne"], value)
        self.assertEqual(self.check(f"{head}        codex\n"), [])

    def test_a_space_before_the_runs_on_colon_is_read(self):
        for line in ("runs-on : qwne", '"runs-on"  : [self-hosted, qwne]'):
            found = self.check(f"jobs:\n  build:\n    {line}\n")
            self.assertEqual(found, ["w.yml:3: qwne"], line)
        found = self.check("jobs:\n  build:\n    runs-on : ubuntu-latest\n")
        self.assertEqual(found, [])

    def test_a_space_before_a_mapping_key_colon_is_read(self):
        found = self.check(
            "jobs:\n  build:\n    runs-on:\n      group: g\n      labels : qwne\n"
        )
        self.assertEqual(found, ["w.yml:5: qwne"])
        head = "jobs:\n  build:\n    runs-on:\n      group : g\n      'labels' :\n"
        self.assertEqual(self.check(f"{head}        - qwne\n"), ["w.yml:6: qwne"])
        self.assertEqual(self.check(f"{head}        - codex\n"), [])

    def test_a_labels_flow_list_across_lines_is_read(self):
        head = "jobs:\n  build:\n    runs-on:\n      group: g\n      labels: [self-hosted,\n"
        self.assertEqual(self.check(f"{head}        qwne]\n"), ["w.yml:5: qwne"])
        self.assertEqual(self.check(f"{head}        codex]\n"), [])
        self.assertEqual(self.check(f"{head}        qwne\n"), [])

    def test_a_next_line_flow_value_across_lines_is_read(self):
        head = "jobs:\n  build:\n    runs-on:\n"
        for value, want in (
            ("      [self-hosted,\n       qwne]\n", ["w.yml:4: qwne"]),
            ("      [self-hosted,\n       codex]\n", []),
            ("      labels:\n        [self-hosted,\n         qwne]\n", ["w.yml:5: qwne"]),
            ("      {group: g,\n       labels: qwne}\n", ["w.yml:4: qwne"]),
        ):
            self.assertEqual(self.check(head + value), want, value)

    def test_an_anchored_or_tagged_value_is_read(self):
        head = "jobs:\n  build:\n    runs-on:"
        for value, want in (
            (" &r qwne\n", ["w.yml:3: qwne"]),
            (" &r codex\n", []),
            (' !!str "qwne"\n', ["w.yml:3: qwne"]),
            (" &r [self-hosted, qwne]\n", ["w.yml:3: qwne"]),
            (" &r\n      - self-hosted\n      - qwne\n", ["w.yml:5: qwne"]),
            ("\n      &r qwne\n", ["w.yml:4: qwne"]),
            ("\n      &r [self-hosted,\n       qwne]\n", ["w.yml:4: qwne"]),
            ("\n      - &a qwne\n", ["w.yml:4: qwne"]),
            ("\n      labels: &l [self-hosted, qwne]\n", ["w.yml:4: qwne"]),
        ):
            self.assertEqual(self.check(head + value), want, value)

    def test_an_alias_value_is_skipped(self):
        head = "jobs:\n  build:\n    runs-on:"
        for value, want in (
            (" *r\n", []),
            (" [*r, qwne]\n", ["w.yml:3: qwne"]),
            ("\n      *r\n", []),
            ("\n      - *r\n      - qwne\n", ["w.yml:5: qwne"]),
            ("\n      group: g\n      labels: *l\n", []),
        ):
            self.assertEqual(self.check(head + value), want, value)

    def test_an_embedded_expression_label_is_skipped(self):
        for value in ("runner-${{ matrix.n }}", '[self-hosted, "x-${{ inputs.h }}"]'):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, [])
        found = self.check('jobs:\n  build:\n    runs-on: "x-}} ${{"\n')
        self.assertEqual(found, ["w.yml:3: x-}} ${{"])

    def test_a_multi_argument_expression_label_is_skipped(self):
        expr = "\"${{ format('runner-{0}', matrix.arch) }}\""
        found = self.check(f"jobs:\n  build:\n    runs-on: [self-hosted, {expr}]\n")
        self.assertEqual(found, [])
        found = self.check(
            f"jobs:\n  build:\n    runs-on: [self-hosted, {expr}, qwne]\n"
        )
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_single_quoted_expression_label_is_skipped(self):
        found = self.check(
            "jobs:\n  build:\n"
            "    runs-on: [self-hosted, '${{ format(''a-{0}'', inputs.h) }}']\n"
        )
        self.assertEqual(found, [])

    def test_a_flow_mapping_expression_label_is_skipped(self):
        for value, want in (
            ("{group: g, labels: \"${{ format('a', b) }}\"}", []),
            ("{group: g, labels: [qwne, \"${{ f('a', b) }}\"]}", ["w.yml:3: qwne"]),
            ('{labels: [self-hosted, "${{ matrix.os[0] }}", qwne]}', ["w.yml:3: qwne"]),
        ):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, want, value)

    def test_a_bracket_in_an_expression_label_is_kept(self):
        found = self.check(
            'jobs:\n  build:\n    runs-on: [self-hosted, "${{ matrix.os[0] }}", qwne]\n'
        )
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_quoted_label_with_a_comma_is_one_label(self):
        found = self.check('jobs:\n  build:\n    runs-on: [self-hosted, "a,b"]\n')
        self.assertEqual(found, ["w.yml:3: a,b"])

    def test_a_multi_argument_expression_across_lines_is_skipped(self):
        for value in (
            "[self-hosted, \"${{ format('runner-{0}',\n      matrix.arch) }}\"]",
            "[self-hosted,\n      \"${{ format('runner-{0}', matrix.arch) }}\"]",
            "{group: g, labels: \"${{ format('a',\n      b) }}\"}",
        ):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, [], value)

    def test_a_builtin_label_in_any_case_is_accepted(self):
        for value in ("[Self-Hosted, Linux, X64]", "ARM64"):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, [])
        found = self.check("jobs:\n  build:\n    runs-on: Codex\n")
        self.assertEqual(found, ["w.yml:3: Codex"])

    def test_a_github_hosted_label_is_accepted(self):
        for value in (
            "ubuntu-24.04",
            "macos-14",
            "Windows-Latest",
            "[ubuntu-24.04-arm]",
        ):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, [], value)
        found = self.check("jobs:\n  build:\n    runs-on: ubuntu-26.04\n")
        self.assertEqual(found, ["w.yml:3: ubuntu-26.04"])

    def test_an_allowlist_glob_matches_a_label(self):
        for value, want in (
            ("[self-hosted, gpu-a1]", []),
            ("[self-hosted, gpu]", ["w.yml:3: gpu"]),
            ("GPU-a1", ["w.yml:3: GPU-a1"]),
        ):
            workflow = f"jobs:\n  build:\n    runs-on: {value}\n"
            found = self.check(workflow, allowed=["gpu-*"])
            self.assertEqual(found, want, value)

    def test_the_allowlist_is_read(self):
        labels = allowlist(ACTIONLINT)
        for label in ("codex", "qwen", "ci-gate", "bigdisk", "release"):
            self.assertIn(label, labels)


if __name__ == "__main__":
    unittest.main()
