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
value's anchor `&r` or tag `!!str` is dropped and an alias `*r` skipped, while a quoted label is
read as written, so `"*r"`, `"&a b"` and `"a # b"` are labels) of
.github/workflows/*.yml and *.yaml, and fail naming file:line and label for any plain-text label
the allowlist and actionlint's built-in labels do not accept. The built-in labels, the self-hosted
ones (`linux`, `x64`) and the GitHub-hosted ones (`ubuntu-24.04`, `macos-14`), match in any case
(`Linux`, `Windows-Latest`), as in actionlint. Allowlist entries are glob patterns (`gpu-*`)
matched case-sensitively with fnmatch; actionlint's Go path.Match differs only for `/`, `\\`
escapes and `[^...]`, none of which a runner label uses. A label with a `${{ }}` expression
anywhere in it is skipped, as actionlint skips it: its value is known only when the workflow runs.
A comma or bracket inside a quoted label or a `${{ }}` expression neither splits nor closes a flow
list or mapping, so `"${{ format('runner-{0}', matrix.arch) }}"` is one (skipped) label.
A job written as a flow mapping (`jobs: {build: {runs-on: x}}`) is not parsed: its `runs-on` key
fails the test as the label `<runs-on in a flow mapping>`.
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
FLOW_ENTRY = re.compile(r"""^\s*["']?labels["']?\s*:(.*)$""", re.DOTALL)
PROPS = re.compile(r"^(?:[&!]\S*(?:\s+|$))+")


class _Scanner:
    """Walk YAML text a character at a time, tracking whether it is inside a
    quoted scalar (`"a,b"`, `'a,b'`), a `${{ ... }}` expression, and how deep
    in nested `[...]` and `{...}` flows. A single-quoted scalar escapes its
    quote as `''`, a double-quoted one with a backslash; an expression's own
    string literals are single-quoted. A quote opens a scalar only at the
    start of a value (after a blank, `:`, `,` or an opening bracket) or inside
    an expression. The state carries over from one feed to the next, so a
    flow can be walked line by line."""

    def __init__(self):
        self.quote, self.expr, self.nest, self.prev = None, 0, 0, " "

    def feed(self, text):
        """Yield (index, char, top) for each character of text; top is True
        for a character outside any quoted scalar and expression (`nest` is
        already updated for a top bracket, `prev` is the character before)."""
        i = 0
        while i < len(text):
            c, top, step = text[i], False, 1
            if self.quote == "'":
                if text.startswith("''", i):
                    step = 2
                elif c == "'":
                    self.quote = None
            elif self.quote == '"':
                if c == "\\":
                    step = 2
                elif c == '"':
                    self.quote = None
            elif text.startswith("${{", i):
                self.expr += 1
                step = 3
            elif self.expr and text.startswith("}}", i):
                self.expr -= 1
                step = 2
            elif c in "'\"" and (self.expr or self.prev in " \t:,[{"):
                self.quote = c
            elif not self.expr:
                top = True
                if c in "[{":
                    self.nest += 1
                elif c in "]}":
                    self.nest -= 1
            yield i, c, top
            self.prev = text[min(i + step, len(text)) - 1]
            i += step


def _is_comment(scanner, c, top):
    """Return whether a character the scanner yielded starts a ` # comment`."""
    return top and c == "#" and scanner.prev in " \t"


def _strip_comment(text):
    """Return text up to a ` # comment` outside quoted scalars and expressions."""
    scanner = _Scanner()
    for i, c, top in scanner.feed(text):
        if _is_comment(scanner, c, top):
            return text[:i]
    return text


def _value(text):
    """Return a YAML scalar with its trailing ` # comment` and quotes removed;
    a `#` inside a quoted scalar or a `${{ }}` expression is text."""
    return _unquote(_strip_comment(text))


def _unquote(text):
    """Return a scalar stripped of blanks and of the quotes around it."""
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        text = text[1:-1]
    return text


def _node(text):
    """Return a raw value stripped of its trailing ` # comment`, its blanks and
    a leading anchor (`&r`) and tag (`!!str`); None for an unquoted alias
    (`*r`), whose labels are read where its anchor is. Quotes are kept, so a
    quoted scalar stays one scalar (`"*r"`, `"[a]"`, `"&a b"` are text, not
    an alias, flow or anchor) and _node is idempotent: _label unquotes it
    once, at the end."""
    text = PROPS.sub("", _strip_comment(text).strip(), count=1)
    return None if text.startswith("*") else text


def _label(text):
    """Return the label a raw flow item or scalar names: its node unquoted
    once, as written between the quotes; None for an alias."""
    text = _node(text)
    return None if text is None else _unquote(text)


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


def _split_flow(inner):
    """Split the body of a flow list or mapping (the text between its outer
    brackets) on its top-level commas. A comma inside a quoted scalar
    (`"a,b"`, `'a,b'`), a `${{ ... }}` expression or a nested `[...]` or
    `{...}` flow does not split it, nor does a bracket inside any of those
    close it (see _Scanner)."""
    parts, start, scanner = [], 0, _Scanner()
    for i, c, top in scanner.feed(inner):
        if top and c == "," and not scanner.nest:
            parts.append(inner[start:i])
            start = i + 1
    parts.append(inner[start:])
    return parts


def _labels(value):
    """Yield the labels of a scalar or flow-list value, skipping any label with
    a `${{ ... }}` expression anywhere in it. The value and its items are raw
    text (see _node): a quoted scalar is one label, read as written."""
    value = _node(value)
    if value is None:
        return
    items = [value]
    if value.startswith("[") and value.endswith("]"):
        items = _split_flow(value[1:-1])
    for label in items:
        label = _label(label) or ""
        if label and not 0 <= label.find("${{") < label.find("}}"):
            yield label


def _value_labels(value):
    """Yield the labels of a scalar, flow-list or flow-mapping value; of a
    flow mapping only its `labels:` entry counts."""
    if value.startswith("{") and value.endswith("}"):
        entries = (FLOW_ENTRY.match(e) for e in _split_flow(value[1:-1]))
        values = [e.group(1) for e in entries if e]
        value = values[0] if values else ""
    yield from _labels(value)


def _flow(lines, i, value):
    """Return (value, last): a flow list or mapping `value` opened on line i
    joined with the following lines up to `last`, the line where it closes;
    (value, i) when it is not a flow or closes on line i, (None, len(lines))
    when it never closes. It closes at the bracket that brings it back to
    depth 0 outside quoted scalars and expressions, so a `]`, `}` or `}}`
    inside an open quoted label does not end it; a ` # comment` on a
    following line is dropped only outside them too."""
    if not value.startswith(("[", "{")):
        return value, i
    scanner, parts = _Scanner(), []
    for j in range(i, len(lines)):
        text = value if j == i else lines[j].strip()
        # Lines join with a blank, as the folded line break of a flow.
        scanner.prev = " "
        end = len(text)
        for k, c, top in scanner.feed(text):
            if _is_comment(scanner, c, top):
                end = k
                break
            if top and c in "]}" and not scanner.nest:
                parts.append(text[: k + 1])
                return " ".join(parts), j
        parts.append(text[:end].strip())
    return None, len(lines)


FLOW_RUNS_ON = "<runs-on in a flow mapping>"
FLOW_KEY = re.compile(r"""["']?runs-on["']?\s*:""")


def _flow_runs_on(line):
    """Return whether a line has a `runs-on` key, bare or quoted, inside a
    flow mapping: after a `{` or `,` (blanks between) and before `\\s*:`, all
    outside quoted scalars, `${{ }}` expressions and ` # comments`."""
    scanner, before, quote, expr = _Scanner(), None, None, 0
    for i, c, top in scanner.feed(line):
        # quote and expr are the scanner's state before this character.
        outside = quote is None and not expr
        if outside and before in ("{", ",") and FLOW_KEY.match(line, i):
            return True
        if _is_comment(scanner, c, top):
            return False
        if outside and c not in " \t":
            before = c
        quote, expr = scanner.quote, scanner.expr
    return False


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
    key is not read). A quoted label is read as written, unquoted once:
    `"*r"`, `"&a linux"`, `"!!str linux"`, `"linux # x"` and `"'linux'"` are
    labels, not an alias, anchor, tag, comment or second quote.

    A job written as a flow mapping is not parsed: a `runs-on` key, bare or
    quoted, after a `{` or `,` on a line, outside quoted scalars, `${{ }}`
    expressions and comments (`jobs: {build: {runs-on: x}}`), yields the
    label `<runs-on in a flow mapping>`, which no allowlist accepts, so the
    test fails closed on it.
    """
    lines = text.splitlines()
    for i, line in enumerate(lines):
        key = RUNS_ON.match(line)
        if not key:
            if _flow_runs_on(line):
                yield i + 1, FLOW_RUNS_ON
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
                    for label in _labels(item.group(2)):
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

    def test_a_block_scalar_runs_on_line_is_not_a_key(self):
        body = (
            "          runs-on: qwne\n"
            "          runs-on: [self-hosted, qwne]\n"
            "\n"
            "          jobs: {b: {runs-on: qwne}}\n"
        )
        after = "  next:\n    runs-on: qwne\n"
        for header in ("|", ">-", "|2", "|2-", ">+", "&s |", "!!str >", "| # sh"):
            head = (
                "jobs:\n  build:\n    runs-on: ubuntu-latest\n    steps:\n"
                f"      - run: {header}\n"
            )
            self.assertEqual(self.check(head + body), [], header)
            found = self.check(head + body + after)
            self.assertEqual(found, ["w.yml:11: qwne"], header)
        # A sibling key at the owning key's column ends the scalar.
        found = self.check(
            "jobs:\n  build:\n    steps:\n      - run: |\n          echo\n"
            "        shell: bash\n        runs-on: qwne\n"
        )
        self.assertEqual(found, ["w.yml:7: qwne"])

    def test_an_alias_resolves_to_its_anchor(self):
        job = "jobs:\n  build:\n    runs-on:"
        for workflow, want in (
            ("env: {RUNNER: &runner qwne}\n" + job + " *runner\n", ["w.yml:4: qwne"]),
            ("env:\n  RUNNER: &runner qwne\n" + job + " *runner\n", ["w.yml:5: qwne"]),
            ("env:\n  RUNNER: &runner codex\n" + job + " *runner\n", []),
            ("x: &l [self-hosted, qwne]\n" + job + " *l\n", ["w.yml:4: qwne"]),
            (
                "x: &l [self-hosted, qwne]\n" + job + "\n      group: g\n"
                "      labels: *l\n",
                ["w.yml:6: qwne"],
            ),
            ("x:\n  - &a qwne\n" + job + " [*a, linux]\n", ["w.yml:5: qwne"]),
            ("x: [&a qwne, b]\n" + job + "\n      - *a\n", ["w.yml:5: qwne"]),
            ("x: &r 'qwne'  # c\n" + job + "\n      *r\n", ["w.yml:5: qwne"]),
        ):
            self.assertEqual(self.check(workflow), want, workflow)

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

    def test_a_hash_in_a_quoted_expression_label_is_kept(self):
        found = self.check(
            "jobs:\n  build:\n    runs-on: [self-hosted, "
            "\"${{ contains(' # ', inputs.x) && 'linux' || 'x64' }}\", qwne]\n"
        )
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_hash_in_a_single_quoted_expression_label_is_kept(self):
        found = self.check(
            "jobs:\n  build:\n    runs-on: [self-hosted, "
            "'${{ contains('' # '', inputs.x) }}', qwne]\n"
        )
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_hash_in_a_quoted_label_is_one_label(self):
        found = self.check('jobs:\n  build:\n    runs-on: [self-hosted, "a #b"]\n')
        self.assertEqual(found, ["w.yml:3: a #b"])

    def test_a_comment_after_a_flow_list_is_stripped(self):
        found = self.check("jobs:\n  build:\n    runs-on: [self-hosted, qwne] # note\n")
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_an_expression_end_in_an_open_quote_does_not_close_a_mapping(self):
        found = self.check(
            'jobs:\n  build:\n    runs-on: {group: "${{\n'
            '      inputs.group }}\n      ", labels: qwne}\n'
        )
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_bracket_in_an_open_quote_does_not_close_a_list(self):
        found = self.check(
            'jobs:\n  build:\n    runs-on: [self-hosted, "${{\n'
            '      matrix.os[0]\n      }}", qwne]\n'
        )
        self.assertEqual(found, ["w.yml:3: qwne"])

    def test_a_hash_in_an_expression_on_a_continuation_line_is_kept(self):
        for value in (
            "[self-hosted,\n      \"${{ contains(' # ', inputs.x) }}\",\n      qwne]",
            "[self-hosted, \"${{\n      contains(' # ', inputs.x) }}\",\n      qwne]",
            "[self-hosted, # note\n      qwne]",
        ):
            found = self.check(f"jobs:\n  build:\n    runs-on: {value}\n")
            self.assertEqual(found, ["w.yml:3: qwne"], value)

    def test_a_quoted_alias_looking_label_is_a_label(self):
        for workflow, line in (
            ('    runs-on: "*qwne"\n', 3),
            ('    runs-on:\n      - "*qwne"\n', 4),
            ('    runs-on: ["*qwne"]\n', 3),
            ('    runs-on:\n      labels: "*qwne"\n', 4),
            ('    runs-on: {labels: "*qwne"}\n', 3),
            ('    runs-on:\n      "*qwne"\n', 4),
        ):
            with self.subTest(workflow=workflow):
                found = self.check("jobs:\n  build:\n" + workflow)
                self.assertEqual(found, [f"w.yml:{line}: *qwne"])

    def test_a_quoted_label_keeps_a_hash(self):
        found = self.check('jobs:\n  build:\n    runs-on: "linux # qwne"\n')
        self.assertEqual(found, ["w.yml:3: linux # qwne"])

    def test_a_quoted_label_keeps_anchor_and_tag_text(self):
        for label in ("&a linux", "!!str linux"):
            with self.subTest(label=label):
                found = self.check(f'jobs:\n  build:\n    runs-on: "{label}"\n')
                self.assertEqual(found, [f"w.yml:3: {label}"])

    def test_a_quoted_label_is_unquoted_once(self):
        found = self.check("jobs:\n  build:\n    runs-on: \"'linux'\"\n")
        self.assertEqual(found, ["w.yml:3: 'linux'"])

    def test_a_runs_on_in_a_flow_mapping_job_is_reported(self):
        for workflow, line in (
            ("jobs: {build: {runs-on: qwne}}\n", 1),
            ("jobs:\n  build: {runs-on: [self-hosted], steps: []}\n", 2),
            ("jobs:\n  build: {steps: [], 'runs-on' : linux}\n", 2),
        ):
            with self.subTest(workflow=workflow):
                found = self.check(workflow)
                self.assertEqual(found, [f"w.yml:{line}: <runs-on in a flow mapping>"])

    def test_a_runs_on_in_a_comment_or_quoted_value_is_not_a_flow_mapping(self):
        for workflow in (
            "# {runs-on: qwne}\njobs:\n  build:\n    runs-on: linux\n",
            "jobs:\n  build:\n    runs-on: linux # {runs-on: qwne}\n",
            'jobs:\n  build:\n    runs-on: linux\n    steps:\n      - run: "echo {runs-on: x}"\n',
            "jobs:\n  build:\n    runs-on: linux\n    steps:\n      - run: echo '{a, runs-on: x}'\n",
        ):
            with self.subTest(workflow=workflow):
                self.assertEqual(self.check(workflow), [])

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
