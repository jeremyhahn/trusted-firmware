# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests pinning sdk/go/status.go to include/tfw/status.h (make scripts-test). No network."""

import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

C_PATH = os.path.join(REPO, "include", "tfw", "status.h")
GO_PATH = os.path.join(REPO, "sdk", "go", "status.go")


def _block(text, start, end):
    """The text between the first `start` and the next `end` after it, or ''."""
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i + len(start))
    return text[i + len(start):j] if j >= 0 else ""


def parse_c(text):
    """{suffix: value} for the tfw_status_t enumerators; TFW_ERR_NO_STORE -> NOSTORE."""
    body = _block(text, "typedef enum {", "} tfw_status_t;")
    out = {}
    for m in re.finditer(r"TFW_(OK|ERR_[A-Z0-9_]+)\s*=\s*(\d+)", body):
        name = m.group(1)
        if name.startswith("ERR_"):
            name = name[len("ERR_"):]
        out[name.replace("_", "")] = int(m.group(2))
    return out


def parse_go(text):
    """({suffix: value} for the Status constants, set of statusNames keys' suffixes)."""
    consts = {}
    for m in re.finditer(r"Status(OK|Err\w+)\s+Status\s*=\s*(\d+)", _block(text, "const (", ")")):
        consts[_go_suffix(m.group(1))] = int(m.group(2))
    names = {_go_suffix(m.group(1))
             for m in re.finditer(r"Status(OK|Err\w+):",
                                  _block(text, "var statusNames = map[Status]string{", "\n}"))}
    return consts, names


def _go_suffix(name):
    if name.startswith("Err"):
        name = name[len("Err"):]
    return name.upper()


def mismatches(c, go, names):
    """Every way the Go mirror differs from the C enum, sorted."""
    out = []
    for k in c:
        if k not in go:
            out.append(f"{k}: in include/tfw/status.h ({c[k]}) but no Go Status constant")
        elif c[k] != go[k]:
            out.append(f"{k}: include/tfw/status.h says {c[k]}, sdk/go/status.go says {go[k]}")
    for k in go:
        if k not in c:
            out.append(f"{k}: Go Status constant ({go[k]}) but not in include/tfw/status.h")
        if k not in names:
            out.append(f"{k}: Go Status constant has no statusNames entry")
    return sorted(out)


def _read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class StatusMirror(unittest.TestCase):
    def setUp(self):
        self.c_text = _read(C_PATH)
        self.go_text = _read(GO_PATH)

    def check(self, c_text=None, go_text=None):
        go, names = parse_go(self.go_text if go_text is None else go_text)
        return mismatches(parse_c(self.c_text if c_text is None else c_text), go, names)

    def mutate(self, text, old, new):
        self.assertIn(old, text)
        return text.replace(old, new)

    def test_main_mirrors(self):
        c = parse_c(self.c_text)
        go, names = parse_go(self.go_text)
        self.assertEqual([], mismatches(c, go, names))
        self.assertEqual(21, len(c))
        self.assertEqual(21, len(go))
        self.assertEqual(21, len(names))

    def test_renumbered_in_c(self):
        c_text = self.mutate(self.c_text, "TFW_ERR_NO_STORE         = 20", "TFW_ERR_NO_STORE         = 21")
        self.assertEqual(["NOSTORE: include/tfw/status.h says 21, sdk/go/status.go says 20"],
                         self.check(c_text=c_text))

    def test_new_c_status_missing_from_go(self):
        c_text = self.mutate(self.c_text, "    TFW_ERR_NO_STORE ",
                             "    TFW_ERR_NEW_THING = 21,\n    TFW_ERR_NO_STORE ")
        self.assertEqual(["NEWTHING: in include/tfw/status.h (21) but no Go Status constant"],
                         self.check(c_text=c_text))

    def test_missing_go_name(self):
        go_text = self.mutate(self.go_text, '\tStatusErrNoStore:         "external store did not answer",\n', "")
        self.assertEqual(["NOSTORE: Go Status constant has no statusNames entry"],
                         self.check(go_text=go_text))

    def test_renumbered_in_go(self):
        go_text = self.mutate(self.go_text, "StatusErrBusy            Status = 8", "StatusErrBusy            Status = 30")
        self.assertEqual(["BUSY: include/tfw/status.h says 8, sdk/go/status.go says 30"],
                         self.check(go_text=go_text))


if __name__ == "__main__":
    unittest.main()
