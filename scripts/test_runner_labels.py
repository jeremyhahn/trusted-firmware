# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests holding every workflow's runner labels to actionlint's allowlist.

.github/actionlint.yaml lists the self-hosted runner labels the workflows may use, and actionlint
reports any other label as an unknown runner. These tests hold every plain-text label to that
allowlist with the standard library alone (no actionlint binary, no PyYAML), so `make scripts-test`
catches a misspelled self-hosted label on any host; `make lint-actionlint` checks the same in the
toolchain image. The tests read the allowlist and every job's `runs-on:` key, `jobs.<job>.runs-on`
only (a `runs-on` action input under `with:` or a `workflow_call` input is no runner), bare or
quoted (scalar,
list, or group/labels mapping, a flow list or mapping on one line or across several, and a scalar
or flow value on the line after an empty `runs-on:` or `labels:` key; a flow `labels:` or
next-line value may also span lines; a key may have spaces before its colon, `runs-on : codex`; a
value's anchor `&r` or tag `!!str` is dropped and an alias `*r` is read at its anchor, anywhere
earlier in the file, or fails the test as `<unresolved alias *r>`, while a quoted label is read
as written, so `"*r"`, `"&a b"` and `"a # b"` are labels; a value of `runs-on:`, `labels:` or a
list item that spans lines, a `>-` or `|` block scalar or a plain or quoted scalar such as
`${{ matrix.os` then `}}`, is read whole and its label reported at the line of its key; the
content of a `|` or `>` block scalar, such as a `run: |` script, is not read as keys) of
.github/workflows/*.yml and *.yaml, and fail naming file:line and label for any plain-text label
the allowlist and actionlint's built-in labels do not accept. The built-in labels, the self-hosted
ones (`linux`, `x64`) and the GitHub-hosted ones (`ubuntu-24.04`, `macos-14`), match in any case
(`Linux`, `Windows-Latest`), as in actionlint. Allowlist entries are glob patterns (`gpu-*`)
matched case-sensitively with fnmatch; actionlint's Go path.Match differs only for `/`, `\\`
escapes and `[^...]`, none of which a runner label uses. A label with a `${{ }}` expression
anywhere in it is skipped, as actionlint skips it: its value is known only when the workflow runs.
A comma or bracket inside a quoted label or a `${{ }}` expression neither splits nor closes a flow
list or mapping, so `"${{ format('runner-{0}', matrix.arch) }}"` is one (skipped) label.
A `jobs:` value or job written as a flow mapping (`jobs: {build: {runs-on: x}}`) is not parsed:
its `runs-on` key fails the test as the label `<runs-on in a flow mapping>`; a flow mapping under
a step, `with:`, `env:` or `on:` is not a job.
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
    start of a value or inside an expression; a value starts at the start of
    a line (see line), after a `- ` item indicator there, after a `:` that a
    blank or the end of the text follows, and after a `,`, `[` or `{` outside
    quotes and expressions. A quote inside a plain scalar (`say "hello`,
    `{NOTE: it is 'fine}`) is text, as is a `[`, `]`, `{` or `}` inside a
    plain scalar outside any flow (`run: echo '{a}'`). The state carries over from one feed to
    the next, so a flow can be walked line by line."""

    def __init__(self):
        self.quote, self.expr, self.nest, self.prev = None, 0, 0, " "
        # Whether the next non-blank top-level character starts a value.
        self.start = True

    def line(self):
        """Mark the start of a new line: a value may start on it."""
        self.prev, self.start = " ", True

    def feed(self, text):
        """Yield (index, char, top) for each character of text; top is True
        for a character outside any quoted scalar and expression (`nest` is
        already updated for a top bracket, `prev` is the character before)."""
        i = 0
        while i < len(text):
            c, top, step = text[i], False, 1
            follow = text[i + 1 : i + 2]
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
                self.start = False
                step = 3
            elif self.expr and text.startswith("}}", i):
                self.expr -= 1
                step = 2
            elif c in "'\"" and (self.expr or self.start):
                self.quote = c
                self.start = False
            elif not self.expr:
                top = True
                # A bracket inside a plain scalar outside a flow is text.
                flow = self.start or self.nest > 0
                if c in "[{" and flow:
                    self.nest += 1
                elif c in "]}" and flow:
                    self.nest -= 1
                if (c in "[{" and flow) or c == "," or (c == ":" and follow in ("", " ", "\t")):
                    self.start = True
                elif c == "-" and self.start and follow in ("", " ", "\t"):
                    pass
                elif c not in " \t":
                    self.start = False
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
    """Return a scalar stripped of blanks and, when quoted, decoded (see
    _decode_quoted)."""
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        text = _decode_quoted(text)
    return text


# YAML 1.2 double-quoted escapes: one character, and hex escapes by length.
ESCAPES = {
    "0": "\0", "a": "\a", "b": "\b", "t": "\t", "\t": "\t", "n": "\n",
    "v": "\v", "f": "\f", "r": "\r", "e": "\x1b", " ": " ", '"': '"',
    "/": "/", "\\": "\\", "N": chr(0x85), "_": chr(0xA0), "L": chr(0x2028),
    "P": chr(0x2029),
}
HEX_ESCAPES = {"x": 2, "u": 4, "U": 8}


def _decode_quoted(text):
    """Return the content of a quoted scalar (quotes included in text) as
    YAML 1.2 decodes it: in single quotes `''` is `'`; in double quotes the
    escapes of ESCAPES and HEX_ESCAPES are decoded. Line folding: a line's
    trailing blanks (not escaped ones) and the next line's leading blanks are
    dropped, one line break is a space and n breaks (n - 1 empty lines) are
    n - 1 newlines; in double quotes a `\\` at the end of a line joins it to
    the next with nothing. An unknown or short escape is not decoded: the
    content is returned raw, so the label fails closed."""
    quote, inner = text[0], text[1:-1]
    rows = inner.split("\n")
    out, joined, empty = "", None, 0
    for k, row in enumerate(rows):
        last = k == len(rows) - 1
        if k:
            row = row.lstrip(" \t")
            if not row and not last:
                empty += 1
                continue
        chars, escaped_break, j = [], False, 0
        while j < len(row):
            c = row[j]
            if quote == "'" or c != "\\":
                if quote == "'" and row.startswith("''", j):
                    chars.append(("'", True))
                    j += 2
                else:
                    chars.append((c, False))
                    j += 1
                continue
            e = row[j + 1 : j + 2]
            if not e:
                if last:
                    return inner
                escaped_break = True
                break
            if e in ESCAPES:
                chars.append((ESCAPES[e], True))
                j += 2
                continue
            n = HEX_ESCAPES.get(e)
            digits = row[j + 2 : j + 2 + n] if n else ""
            if not n or len(digits) != n or not all(
                d in "0123456789abcdefABCDEF" for d in digits
            ):
                return inner
            code = int(digits, 16)
            if code > 0x10FFFF:
                return inner
            chars.append((chr(code), True))
            j += 2 + n
        if not last and not escaped_break:
            while chars and not chars[-1][1] and chars[-1][0] in " \t":
                chars.pop()
        if k:
            out += "\n" * empty if joined or empty else " "
        out += "".join(c for c, _ in chars)
        joined, empty = escaped_break, 0
    return out


def _node(text):
    """Return a raw value stripped of its trailing ` # comment`, its blanks and
    a leading anchor (`&r`) and tag (`!!str`); an unquoted alias (`*r`) is
    returned as written, for _alias to read at its anchor. Quotes are kept,
    so a quoted scalar stays one scalar (`"*r"`, `"[a]"`, `"&a b"` are text,
    not an alias, flow or anchor) and _node is idempotent: _labels unquotes it
    once, at the end."""
    return PROPS.sub("", _strip_comment(text).strip(), count=1)


UNRESOLVED = "<unresolved alias {}>"


def _alias(node, anchors, i):
    """Return the node an unquoted alias on line i names: the node of the
    last anchor of that name on line i or before it. An alias with no such
    anchor, or whose anchor marks a block node (a block list or mapping on
    later lines, or a block scalar), is the quoted label
    `<unresolved alias *r>`, which no allowlist accepts, so the test fails
    closed on a label it cannot check. Any other node is returned as is."""
    if not node.startswith("*"):
        return node
    found = [n for line, n in anchors.get(node[1:], ()) if line <= i]
    if found and found[-1] is not None:
        return found[-1]
    return '"' + UNRESOLVED.format(node) + '"'


def _anchor(text, i, anchors, lines=None):
    """Record in anchors the anchor of a raw node on line i, and those of the
    items of a flow node, as {name: [(line, node)]}; return the last line the
    node takes. In a block (lines given) a flow node may span lines, and an
    empty node or a block scalar header anchors a block node, recorded as
    None; in a flow an empty node is an empty scalar."""
    text = _strip_comment(text).strip()
    props = PROPS.match(text)
    names = re.findall(r"(?:^|\s)&(\S+)", props.group(0)) if props else []
    node, last = text[props.end() if props else 0 :].strip(), i
    if lines is not None and node.startswith(("[", "{")):
        node, last = _flow(lines, i, node)
        if node is None:
            last = i
    if lines is not None and node is not None:
        if not node or BLOCK_SCALAR.fullmatch(node):
            node = None
    if node and node[0] + node[-1] in ("[]", "{}"):
        for part in _split_flow(node[1:-1]):
            pair = KEY.match(part.strip())
            _anchor((pair.group(2) or "") if pair else part, i, anchors)
    for name in names:
        anchors.setdefault(name, []).append((i, node))
    return last


def _anchors(lines, skip):
    """Return the anchors of every mapping value and list item in lines (see
    _anchor), outside multi-line scalar content (skip, see _scalar_lines)."""
    anchors, last = {}, -1
    for i, line in enumerate(lines):
        if i <= last or i in skip:
            continue
        _, value = _line_node(line)
        if value is not None:
            last = _anchor(value, i, anchors, lines)
    return anchors


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


def _labels(value, anchors, i):
    """Yield the labels of a scalar or flow-list value on line i, skipping any
    label with a `${{ ... }}` expression anywhere in it. The value and its
    items are raw text (see _node): a quoted scalar is one label, read as
    written; an alias, as a value or an item, is read at its anchor (see
    _alias). A _Decoded block scalar is one label, taken as is."""
    if isinstance(value, _Decoded):
        if not 0 <= value.find("${{") < value.find("}}"):
            yield value
        return
    value = _alias(_node(value), anchors, i)
    items = [value]
    if value.startswith("[") and value.endswith("]"):
        items = _split_flow(value[1:-1])
    for label in items:
        label = _unquote(_alias(_node(label), anchors, i))
        if label and not 0 <= label.find("${{") < label.find("}}"):
            yield label


def _value_labels(value, anchors, i):
    """Yield the labels of a scalar, flow-list or flow-mapping value on line
    i, an alias read at its anchor; of a flow mapping only its `labels:`
    entry counts; a _Decoded block scalar is one label (see _labels)."""
    if isinstance(value, _Decoded):
        yield from _labels(value, anchors, i)
        return
    value = _alias(_node(value), anchors, i)
    if value.startswith("{") and value.endswith("}"):
        entries = (FLOW_ENTRY.match(e) for e in _split_flow(value[1:-1]))
        values = [e.group(1) for e in entries if e]
        value = values[0] if values else ""
    yield from _labels(value, anchors, i)


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
        scanner.line()
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
        flow = outside and scanner.nest > 0 and before in ("{", ",")
        if flow and FLOW_KEY.match(line, i):
            return True
        if _is_comment(scanner, c, top):
            return False
        if outside and c not in " \t":
            before = c
        quote, expr = scanner.quote, scanner.expr
    return False


LINE = re.compile(r"^(\s*)((?:-(?:\s+|$))*)(.*)$")
# A block mapping or flow mapping key, bare or quoted, and its raw value.
KEY = re.compile(
    r"""^("(?:[^"\\]|\\.)*"|'(?:[^']|'')*'|[^\s"'#&*!|>%@`\[\]{},][^#]*?)"""
    r"""\s*:(?:\s+(.*))?$"""
)
BLOCK_SCALAR = re.compile(r"[|>][1-9+-]*")


def _line_node(line):
    """Return (column, value) for a block mapping entry or list item on line:
    its raw value and the column of its key, or of its last `-` when the item
    has no key (`      - run: |` is column 8, `  - |` column 2); (None, None)
    for any other line."""
    m = LINE.match(line)
    key = KEY.match(m.group(3))
    if key:
        return len(m.group(1)) + len(m.group(2)), key.group(2) or ""
    if m.group(2):
        return len((m.group(1) + m.group(2)).rstrip()) - 1, m.group(3)
    return None, None


def _indent(line):
    return len(line) - len(line.lstrip())


def _scalar_lines(lines):
    """Return the indexes of the lines inside a multi-line scalar, which hold
    no keys: the content lines of every block scalar, the lines after a `|`
    or `>` header (with any indentation or chomping indicator, anchor, tag or
    ` # comment`) that are blank or deeper than the column of the header's
    key or `-`; and the continuation lines of every multi-line quoted scalar,
    the lines after a mapping value or list item that starts with `'` or `"`
    up to the line on which its quote closes (to the end of the file if it
    never closes). A quote later in a plain scalar (`run: echo "x`) opens no
    quoted scalar. The quote is tracked on the comment-stripped value and
    ends at its closing quote, so a quote in a trailing comment
    (`name: "CI" # "x`, `done" # "x`) opens nothing. A value that opens a
    flow (`{NOTE: "a`, `[ "a`) is walked to where the flow closes, and a
    line of it that starts inside a quoted scalar is skipped too."""
    skip, i = set(), 0
    while i < len(lines):
        col, value = _line_node(lines[i])
        i += 1
        node = "" if value is None else _node(value)
        if BLOCK_SCALAR.fullmatch(node):
            while i < len(lines) and (
                not lines[i].strip() or _indent(lines[i]) > col
            ):
                skip.add(i)
                i += 1
        elif node[:1] in ("'", '"', "[", "{"):
            flow = node[0] in "[{"
            scanner = _Scanner()
            for _ in scanner.feed(_strip_comment(value)):
                pass
            while i < len(lines) and (
                scanner.quote is not None or (flow and scanner.nest > 0)
            ):
                if scanner.quote is not None:
                    skip.add(i)
                scanner.line()
                for _, c, top in scanner.feed(lines[i]):
                    if _is_comment(scanner, c, top):
                        break
                    if not flow and scanner.quote is None:
                        break
                i += 1
    return skip


class _Decoded(str):
    """A decoded block scalar: one label as is, never trimmed, unquoted,
    comment-stripped, read as an alias or split as a flow."""


def _block_text(header, content, col):
    """Return the text of a block scalar as YAML decodes it. Only the
    content indentation is removed: col plus the header's indentation
    indicator, else the indentation of the first non-blank content line;
    deeper spaces are kept. `|` keeps every line break; `>` folds the break
    between two non-blank lines that are not more indented into a space, a
    blank line between them being one break. Chomping: `-` strips every
    trailing break, `+` keeps them all (trailing blank lines included), none
    (clip) keeps one."""
    digits = [c for c in header if c.isdigit()]
    if digits:
        indent = col + int(digits[0])
    else:
        indent = next((_indent(line) for line in content if line.strip()), 0)
    body = []
    for line in content:
        if not line.strip() and len(line) <= indent:
            body.append("")
        elif line[:indent].strip():
            body.append(line.lstrip())
        else:
            body.append(line[indent:])
    trailing = 0
    while body and not body[-1]:
        body.pop()
        trailing += 1
    if header[0] == "|":
        text = "\n".join(body)
    else:
        text, prev, empty = "", None, 0
        for line in body:
            if not line:
                empty += 1
                continue
            if prev is None:
                text += "\n" * empty
            elif prev[0] in " \t" or line[0] in " \t":
                text += "\n" * (empty + 1)
            else:
                text += "\n" * empty if empty else " "
            text += line
            prev, empty = line, 0
    if "-" in header:
        return text
    if "+" in header:
        return text + "\n" * ((1 if body else 0) + trailing)
    return text + "\n" if body else text


def _read_value(lines, i, col, value, skip):
    """Return (value, last): the raw value of a key or `-` at column col on
    line i, read to `last`, the last line it takes; (None, len(lines)) for a
    flow that never closes. The value is decoded before any label is read
    from it, and a label it holds is reported at line i, its key's line:
    - a block scalar header (`>-`, `|`, anchored, tagged or with a
      ` # comment`) is the text of its content lines, every blank line or
      line deeper than col after it (see _scalar_lines and _block_text),
      returned as a _Decoded label, whitespace and all;
    - a flow list or mapping is read to the line where it closes (_flow);
    - a plain or quoted scalar takes the following lines deeper than col
      that are not a key, an item, a comment or block scalar content
      (skip), joined with a blank as YAML folds them, so `${{ matrix.os`
      then `}}` is one expression; a quoted scalar takes them only while
      its quote is open, up to the line that closes it (those lines are in
      skip as its own continuation lines, so skip does not stop it)."""
    node = _node(value)
    if BLOCK_SCALAR.fullmatch(node):
        j = i + 1
        while j < len(lines) and (not lines[j].strip() or _indent(lines[j]) > col):
            j += 1
        return _Decoded(_block_text(node, lines[i + 1 : j], col)), j - 1
    if node.startswith(("[", "{")):
        return _flow(lines, i, node)
    if not node or node.startswith("*"):
        return node, i
    if value[len(_strip_comment(value)) :]:
        # A ` # comment` ends a plain scalar (an open quote has none).
        return node, i
    if node[0] in "'\"":
        return _read_quoted(lines, i, col, node)
    parts, last = [node], i
    for j in range(i + 1, len(lines)):
        line = lines[j]
        if not line.strip():
            continue
        if j in skip or _indent(line) <= col:
            break
        if ENTRY.match(line) or ITEM.match(line) or line.lstrip().startswith("#"):
            break
        text = _strip_comment(line).strip()
        parts.append(text)
        last = j
        if text != line.strip():
            break
    return " ".join(parts), last


def _read_quoted(lines, i, col, node):
    """Return (value, last) for a quoted scalar opened by node on line i: node
    itself when its quote closes on line i (_labels decodes it), else the
    raw lines up to its closing quote (blank ones included, nothing after the
    quote) decoded as one _Decoded label; a scalar whose quote never closes
    (end of file, or a non-blank line not deeper than col) is its stripped
    lines joined with a blank, a label that fails closed."""
    scanner = _Scanner()
    for _ in scanner.feed(node):
        pass
    rows, last = [node], i
    for j in range(i + 1, len(lines)):
        if scanner.quote is None:
            break
        line = lines[j]
        if line.strip() and _indent(line) <= col:
            break
        end = len(line)
        for k, _c, _top in scanner.feed(line):
            if scanner.quote is None:
                end = k + 1
                break
        rows.append(line[:end])
        if scanner.quote is None:
            return _Decoded(_decode_quoted("\n".join(rows))), j
        if line.strip():
            last = j
    if scanner.quote is None:
        return node, i
    return " ".join(r.strip() for r in rows if r.strip()), last


def _job_keys(lines, skip):
    """Return (keys, flows) for the `jobs:` mapping at column 0 in lines,
    outside multi-line scalar content (skip, see _scalar_lines): keys holds
    the indexes of the lines of the direct keys of each job,
    `jobs.<job>.<key>`, by indent as the file writes them (each job key one
    level deeper than `jobs:`, its keys the next level); flows holds the
    indexes of the `jobs:` line and the job-key lines whose value is a flow
    mapping with a `runs-on` key in it (see _flow_runs_on), read to where
    the flow closes. A flow value's continuation lines are not keys."""
    keys, flows = set(), set()
    in_jobs, job_indent, key_indent, last = False, None, None, -1
    for i, line in enumerate(lines):
        if i <= last or i in skip:
            continue
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent, entry = _indent(line), ENTRY.match(line)
        if indent == 0:
            in_jobs = bool(entry) and entry.group(2) == "jobs"
            job_indent = key_indent = None
        elif not in_jobs:
            continue
        elif job_indent is None or indent == job_indent:
            job_indent, key_indent = indent, None
        elif indent < job_indent:
            in_jobs = False
            continue
        elif key_indent is None or indent == key_indent:
            key_indent = indent
            if entry:
                keys.add(i)
                node = _node(entry.group(3) or "")
                if node.startswith(("[", "{")):
                    value, end = _flow(lines, i, node)
                    if value is not None:
                        last = end
            continue
        else:
            continue
        if not in_jobs or not entry:
            continue
        # The `jobs:` line or a job-key line: its value may be a flow.
        node = _node(entry.group(3) or "")
        if node.startswith("{"):
            value, last = _flow(lines, i, node)
            if value is None:
                value, last = node, i
            if _flow_runs_on(value):
                flows.add(i)
    return keys, flows


def plain_labels(text):
    """Yield (lineno, label) for each plain-text label of each job's `runs-on:`
    key. Only a direct key of a job, `jobs.<job>.runs-on`, is read (see
    _job_keys): a `runs-on` key under a step's `with:` or `env:`, under
    `on.workflow_call.inputs` or anywhere else is not a runner.

    The value is a scalar (`codex`), a flow list (`[self-hosted, linux]`), a
    block list on the lines after an empty `runs-on:`, or a mapping whose
    `labels:` entry is one of those (`{group: g, labels: codex}` or `group:` and
    `labels:` keys on the lines after `runs-on:`); `group:` is not a label. A
    scalar or flow value of an empty `runs-on:` or `labels:` key may sit on
    the next, deeper-indented line (`runs-on:` then `  codex`). A scalar value
    of `runs-on:`, `labels:`, an empty key's next line or a list item that
    spans lines is read whole before its labels are (see _read_value): a
    block scalar (`runs-on: >-` then `  codex`) is its decoded content, and
    a plain or quoted scalar takes its deeper continuation lines
    (`runs-on: ${{ matrix.os` then `  }}` is one skipped expression); its
    label is reported at the line of its key or `-`. The
    key may be quoted (`'runs-on':` or `"runs-on":`), as may the `group:` and
    `labels:` keys of its mapping (`'labels':`, `"group":`); each of these
    keys may have spaces before its colon (`runs-on : codex`). A flow list or
    mapping that does not close on its line, whether it is the `runs-on:`
    value, a `labels:` value or the next-line value of an empty key, takes the
    following lines until it closes (an unclosed one yields nothing); its
    labels carry the line it opens on. A value that starts with `${{` is an
    expression and yields nothing, and a label with a `${{ ... }}` expression
    anywhere in it (`runner-${{ matrix.n }}`) is skipped. A value's leading
    anchor (`&r`) and tag (`!!str`) are dropped. An alias (`*r`), as a value,
    a block-list item or a flow item, yields the labels of its anchor's node,
    read as a written value and reported on the alias's line; the anchor may
    be on any mapping value or list item, or flow item, earlier in the file
    (`env: {RUNNER: &runner codex}`). An alias whose anchor is missing, comes
    later, or marks a block list, mapping or scalar yields the label
    `<unresolved alias *r>`, which no allowlist accepts, so the test fails
    closed on it. A quoted label is unquoted and decoded once, as YAML
    decodes it (escapes, `''`, line folding; see _decode_quoted):
    `"*r"`, `"&a linux"`, `"!!str linux"`, `"linux # x"` and `"'linux'"` are
    labels, not an alias, anchor, tag, comment or second quote.

    A `jobs:` value or a job written as a flow mapping is not parsed: a
    `runs-on` key, bare or quoted, after a `{` or `,` in that flow, outside
    quoted scalars, `${{ }}` expressions and comments
    (`jobs: {build: {runs-on: x}}`, `  build: {runs-on: x}`), yields the
    label `<runs-on in a flow mapping>` at the line the flow opens on, which
    no allowlist accepts, so the test fails closed on it. A flow mapping
    under a step, `with:`, `env:` or `on:` is not a job.

    The content lines of a block scalar (`run: |`, `>-`, `|2`, anchored or
    tagged), every blank line or line deeper than its key or `-` after the
    header, are not read as keys, so a `runs-on:` in a script is not a key;
    nor are the continuation lines of a multi-line quoted scalar (`run: "echo`
    then `  runs-on: x` then `  done"`), up to the line its quote closes on
    (see _scalar_lines).
    """
    lines = text.splitlines()
    skip = _scalar_lines(lines)
    anchors = _anchors(lines, skip)
    keys, flows = _job_keys(lines, skip)
    # The last line of a multi-line runs-on value already read.
    done = -1
    for i, line in enumerate(lines):
        if i in skip or i <= done:
            continue
        if i in flows:
            yield i + 1, FLOW_RUNS_ON
        key = RUNS_ON.match(line) if i in keys else None
        if not key:
            continue
        col = _line_node(line)[0]
        if col is None:
            col = len(key.group(1))
        value, done = _read_value(lines, i, col, key.group(2), skip)
        if value is None:
            # A flow that never closes yields nothing; read on from line i.
            done = i
            continue
        if value or isinstance(value, _Decoded):
            for label in _value_labels(value, anchors, i):
                yield i + 1, label
        else:
            indent = len(key.group(1))
            # `- item` lines at or past list_indent are labels; None after a
            # mapping key whose value is not a block list.
            list_indent = indent
            # The last line of a flow value already read.
            last = i
            for j in range(i + 1, len(lines)):
                if j <= last or j in skip:
                    continue
                if not lines[j].strip() or lines[j].lstrip().startswith("#"):
                    continue
                item = ITEM.match(lines[j])
                listed = list_indent is not None
                if item and listed and len(item.group(1)) >= list_indent:
                    dash = len(item.group(1))
                    value, last = _read_value(lines, j, dash, item.group(2), skip)
                    for label in _labels(value or "", anchors, j):
                        yield j + 1, label
                    continue
                entry = ENTRY.match(lines[j])
                depth = len(lines[j]) - len(lines[j].lstrip())
                if listed and not item and not entry and depth > list_indent:
                    # The value of the empty key, on the next line.
                    value, last = _read_value(lines, j, col, lines[j], skip)
                    for label in _value_labels(value or "", anchors, j):
                        yield j + 1, label
                    list_indent = None
                    continue
                if not entry or len(entry.group(1)) <= indent:
                    break
                list_indent = None
                if entry.group(2) == "labels":
                    value, last = _read_value(
                        lines, j, len(entry.group(1)), entry.group(3) or "", skip
                    )
                    if value is None:
                        break
                    if value or isinstance(value, _Decoded):
                        for label in _labels(value, anchors, j):
                            yield j + 1, label
                    else:
                        list_indent = len(entry.group(1))


def unknown(workflow_dir, allowed):
    """Return `file:line: label` for each label neither built in nor matched
    by an allowlist glob pattern. A label with leading or trailing
    whitespace or a line break (a block scalar's decoded text) is printed
    as its repr (`w.yml:3: 'ci-gate\\n'`), so the whitespace shows."""
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
                shown = label
                if label != label.strip() or "\n" in label:
                    shown = repr(str(label))
                found.append(f"{name}:{lineno}: {shown}")
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

    def test_an_unresolved_alias_fails_closed(self):
        head = "jobs:\n  build:\n    runs-on:"
        for value, want in (
            (" *r\n", ["w.yml:3: <unresolved alias *r>"]),
            (" [*r, qwne]\n", ["w.yml:3: <unresolved alias *r>", "w.yml:3: qwne"]),
            ("\n      *r\n", ["w.yml:4: <unresolved alias *r>"]),
            (
                "\n      - *r\n      - qwne\n",
                ["w.yml:4: <unresolved alias *r>", "w.yml:5: qwne"],
            ),
            ("\n      group: g\n      labels: *l\n", ["w.yml:5: <unresolved alias *l>"]),
        ):
            self.assertEqual(self.check(head + value), want, value)
        for workflow, want in (
            # An anchor on a block list.
            ("x: &r\n  - codex\n" + head + " *r\n", ["w.yml:5: <unresolved alias *r>"]),
            # An anchor on a block scalar.
            ("x: &r |\n  codex\n" + head + " *r\n", ["w.yml:5: <unresolved alias *r>"]),
            # An alias before its anchor.
            (head + " *r\nx: &r codex\n", ["w.yml:3: <unresolved alias *r>"]),
        ):
            self.assertEqual(self.check(workflow), want, workflow)

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
            "jobs:\n  build:\n    env: |\n      echo\n    runs-on: qwne\n"
        )
        self.assertEqual(found, ["w.yml:5: qwne"])

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

    def check_multi_line(self, cases, head="    runs-on: "):
        """Check each (value, labels) case as the value after head in a
        job, followed by a job whose `runs-on: qwne` must still be read:
        the multi-line value did not swallow it. Each label is `line: label`,
        the line of the key (or `-`) whose value holds it."""
        allowed = allowlist(ACTIONLINT) + ["codex"]
        for value, labels in cases:
            with self.subTest(value=value):
                workflow = f"jobs:\n  build:\n{head}{value}\n"
                after = workflow.count("\n") + 2
                workflow += "  next:\n    runs-on: qwne\n"
                found = self.check(workflow, allowed)
                expected = [f"w.yml:{label}" for label in labels]
                self.assertEqual(found, expected + [f"w.yml:{after}: qwne"])

    def test_a_block_scalar_runs_on_value_is_read(self):
        self.check_multi_line(
            (
                (">-\n      codex", []),
                (">-\n      qwne", ["3: qwne"]),
                ("|-\n      qwne", ["3: qwne"]),
                (">\n      qwne", ["3: 'qwne\\n'"]),
                ("|+\n      qwne\n", ["3: 'qwne\\n\\n'"]),
                (">- # note\n      qwne", ["3: qwne"]),
                ("&r >-\n      qwne", ["3: qwne"]),
                ("!!str >-\n      qwne", ["3: qwne"]),
                (">-\n      ${{ matrix.runner }}", []),
                (">\n      ${{\n      matrix.runner }}", []),
            )
        )

    def test_a_plain_or_quoted_runs_on_value_spans_lines(self):
        self.check_multi_line(
            (
                ("${{ matrix.os\n      }}", []),
                ('"${{ matrix.os\n      }}"', []),
                ("'${{ matrix.os\n      }}'", []),
                ("runner-${{\n      matrix.n }}", []),
            )
        )

    def test_a_multi_line_next_line_or_labels_value_is_read(self):
        self.check_multi_line(
            (
                ("\n      >-\n        qwne", ["4: qwne"]),
                ("\n      ${{ matrix.os\n        }}", []),
                ("\n      labels: >-\n        qwne", ["4: qwne"]),
                ("\n      group: g\n      labels: |-\n        qwne", ["5: qwne"]),
                ("\n      labels: ${{ matrix.os\n        }}", []),
            ),
            head="    runs-on:",
        )

    def test_a_multi_line_list_item_is_read(self):
        self.check_multi_line(
            (
                ("\n      - >-\n        qwne", ["4: qwne"]),
                ("\n      - &r !!str >-\n        qwne", ["4: qwne"]),
                ("\n      - ${{ matrix.os\n        }}", []),
                ('\n      - "${{ matrix.os\n        }}"', []),
                ("\n      labels:\n        - >-\n          qwne", ["5: qwne"]),
            ),
            head="    runs-on:",
        )

    def test_a_multi_line_quoted_scalar_line_is_not_a_key(self):
        head = "jobs:\n  build:\n    runs-on: ubuntu-latest\n"
        step = "    steps:\n      - run: "
        after = "  next:\n    runs-on: qwne\n"
        for body in (
            step + '"echo\n          runs-on: qwne\n          done"\n',
            step + "'echo\n          runs-on: qwne\n          done'\n",
            step + '"echo\n          runs-on: [self-hosted, qwne]\n          done"\n',
            step + '"echo\n          jobs: {b: {runs-on: qwne}}\n          done"\n',
            '    env:\n      X: "a\n        runs-on: qwne\n        b"\n',
            step + '"echo \\"x\n          runs-on: qwne\n          done"\n',
            step + "'it''s\n          runs-on: qwne\n          done'\n",
        ):
            with self.subTest(body=body):
                self.assertEqual(self.check(head + body), [])
                line = (head + body).count("\n") + 2
                found = self.check(head + body + after)
                self.assertEqual(found, [f"w.yml:{line}: qwne"])
        # A quote inside a plain scalar opens no quoted scalar.
        body = step + 'echo "a\n'
        line = (head + body).count("\n") + 2
        self.assertEqual(self.check(head + body + after), [f"w.yml:{line}: qwne"])

    def test_a_quote_in_a_trailing_comment_opens_no_quoted_scalar(self):
        jobs = "jobs:\n  build:\n    runs-on: ubuntu-latest\n"
        step = "    steps:\n      - run: "
        after = "  next:\n    runs-on: qwne\n"
        for head, body in (
            ('name: "CI" # "TODO\n' + jobs, step + "echo\n"),
            ("name: 'CI' # 'TODO\n" + jobs, step + "echo\n"),
            (jobs, step + '"echo # x"  # "y\n'),
            (jobs, step + '"echo\n          done" # "z\n'),
            (jobs, step + "'echo\n          done' # 'z\n"),
            # A real multi-line scalar with a `#` inside its quote still
            # hides its inner lines.
            (jobs, step + '"a # b\n          runs-on: qwne\n          done"\n'),
        ):
            with self.subTest(head=head, body=body):
                line = (head + body).count("\n") + 2
                found = self.check(head + body + after)
                self.assertEqual(found, [f"w.yml:{line}: qwne"])

    def test_a_quoted_scalar_in_a_flow_collection_is_not_a_key(self):
        head = "jobs:\n  build:\n    runs-on: ubuntu-latest\n"
        after = "  next:\n    runs-on: qwne\n"
        for body in (
            '    env: {NOTE: "first\n      runs-on: qwne\n      last"}\n',
            "    env: {NOTE: 'first\n      runs-on: qwne\n      last'}\n",
            "    steps:\n      - uses: a/b@v1\n        with:\n"
            '          labels: [ "a\n            runs-on: qwne\n            b" ]\n',
        ):
            with self.subTest(body=body):
                line = (head + body).count("\n") + 2
                found = self.check(head + body + after)
                self.assertEqual(found, [f"w.yml:{line}: qwne"])

    def test_a_quote_inside_a_plain_scalar_opens_no_quoted_scalar(self):
        head = "jobs:\n  a:\n    runs-on: ubuntu-latest\n"
        after = "  b:\n    runs-on: qwne\n"
        for body in (
            '    env: {NOTE: say "hello}\n',
            '    env: [say "hello]\n',
            "    env: {NOTE: it is 'fine}\n",
            '    env:\n      NOTE: say "hello\n',
        ):
            with self.subTest(body=body):
                line = (head + body).count("\n") + 2
                found = self.check(head + body + after)
                self.assertEqual(found, [f"w.yml:{line}: qwne"])

    def test_a_quoted_label_is_decoded(self):
        # The YAML escape backslash, u, 0063: the letter c.
        U63 = "\\" + "u0063"
        allowed = allowlist(ACTIONLINT) + ["codex"]
        head = "jobs:\n  build:\n    runs-on: "
        for value, want in (
            ('"' + U63 + 'odex"', []),
            (r'"\x63odex"', []),
            (r'"co\U00000064ex"', []),
            ('["' + U63 + 'odex"]', []),
            ('"co\\\n      dex"', []),
            ("'qw''ne'", ["w.yml:3: qw'ne"]),
            (r'"qw\"ne"', ['w.yml:3: qw"ne']),
            ('"co\n      dex"', ["w.yml:3: co dex"]),
            ('"co\n\n      dex"', ["w.yml:3: 'co\\ndex'"]),
            ("'co\n\n      dex'", ["w.yml:3: 'co\\ndex'"]),
            # An unknown escape is not decoded: the raw label fails closed.
            (r'"co\qdex"', [r"w.yml:3: co\qdex"]),
        ):
            with self.subTest(value=value):
                self.assertEqual(self.check(head + value + "\n", allowed), want)

    def test_a_block_scalar_label_keeps_its_whitespace(self):
        allowed = allowlist(ACTIONLINT) + ["ci-gate"]
        head = "jobs:\n  build:\n    runs-on: "
        for value, want in (
            ("|\n      ci-gate\n", ["w.yml:3: 'ci-gate\\n'"]),
            (">\n      ci-gate\n", ["w.yml:3: 'ci-gate\\n'"]),
            ("|+\n      ci-gate\n\n", ["w.yml:3: 'ci-gate\\n\\n'"]),
            ("|2-\n        ci-gate\n", ["w.yml:3: '  ci-gate'"]),
            ("|-\n      ci-gate\n        x\n", ["w.yml:3: 'ci-gate\\n  x'"]),
            (">-\n      ci-gate\n", []),
            ("|-\n      ci-gate\n", []),
            (">-\n      ${{ matrix.runner }}\n", []),
        ):
            with self.subTest(value=value):
                self.assertEqual(self.check(head + value, allowed), want)

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

    def test_a_runs_on_action_input_is_not_a_runner(self):
        job = "jobs:\n  build:\n    runs-on: ubuntu-latest\n"
        step = "    steps:\n      - uses: a/b@v1\n"
        for workflow in (
            job + step + "        with: {runs-on: arbitrary-input}\n",
            job + step + "        with:\n          runs-on: arbitrary-input\n",
            job + step + "        env: {runs-on: x}\n",
            "on:\n  workflow_call:\n    inputs:\n      runs-on:\n"
            "        type: string\n" + job,
        ):
            with self.subTest(workflow=workflow):
                self.assertEqual(self.check(workflow), [])
        workflow = (
            job + step + "        with:\n          runs-on: arbitrary-input\n"
            "  next:\n    runs-on: qwne\n"
        )
        self.assertEqual(self.check(workflow), ["w.yml:9: qwne"])

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
