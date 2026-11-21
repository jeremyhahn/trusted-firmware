# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for the helpers of scripts/api-doc-check.py (make scripts-test).

strip, public_packages and page_names are what every verdict of
`make api-doc-check` rests on: a literal that ends early leaves a brace that
hides later declarations, a walk that enters internal/ documents what is not
public, and a row read outside the Exported symbols section is a symbol the
page never claimed. No network, no Go toolchain.

API_DOC_CHECK_SCRIPT points the suite at another copy of the script (a
scratch mutation run); unset, it tests the one in the tree.
"""

import importlib.util
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("API_DOC_CHECK_SCRIPT") or os.path.join(REPO, "scripts", "api-doc-check.py")
spec = importlib.util.spec_from_file_location("api_doc_check", SCRIPT)
adc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adc)


class TestStrip(unittest.TestCase):
    CASES = [
        # (what, Go text, stripped)
        ("escaped quote in a string", '"a\\"b" x', '"" x'),
        ("escaped quote rune", "'\\'' y", "'' y"),
        ("raw string over three lines", "`a\nb\nc`", '""\n\n'),
        ("block comment over two lines", "/* a\nb */x", "\nx"),
        ("line comment", "// c {\nx", "\nx"),
        ("unterminated block comment", "a /* b\nc {", "a \n"),
        ("unterminated raw string", "a `b\nc {", 'a ""\n'),
    ]

    def test_cases(self):
        for what, src, want in self.CASES:
            with self.subTest(what):
                got = adc.strip(src)
                self.assertEqual(got, want)
                self.assertEqual(got.count("\n"), src.count("\n"))

    def test_escaped_quote_does_not_hide_later_declarations(self):
        src = 'package p\nvar S = "a\\"{"\nfunc Real() {}\n'
        package, symbols = adc.parse(src)
        self.assertEqual(package, "p")
        self.assertEqual({name for name, _ in symbols}, {"S", "Real"})


class TestPublicPackages(unittest.TestCase):
    FILES = {
        "a.go": "package tfw\n",
        "sub/b.go": "package sub\n",
        "internal/c.go": "package internal\n",
        "testdata/d.go": "package testdata\n",
        ".git/e.go": "package dot\n",
        "_x/f.go": "package underscore\n",
        "cmd/tool/main.go": "package main\n",
        "only/g_test.go": "package only\n",
    }

    def test_walk(self):
        with tempfile.TemporaryDirectory() as root:
            base = os.path.join(root, "sdk", "go")
            for rel, text in self.FILES.items():
                path = os.path.join(base, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(text)
            self.assertEqual(adc.public_packages(root), {"tfw": base, "sub": os.path.join(base, "sub")})


PAGE = """# API

| `tfw.Before` | a row before the section |

## Exported symbols

| Symbol | Meaning |
|---|---|
| `tfw.A`, `tfw.B` | two in one cell |

### tfw

| `tfw.C` | under a subheading |
| `tfw.C` | again |

## Other

| `tfw.After` | a row in a later section |

Prose calls `tfw.New(...)`, names `tfw.Status` and the method `tfw.Device.Ping`.
"""


class TestPageNames(unittest.TestCase):
    def test_rows(self):
        rows, _ = adc.page_names(PAGE)
        self.assertEqual(rows, ["tfw.A", "tfw.B", "tfw.C", "tfw.C"])

    def test_refs(self):
        _, refs = adc.page_names(PAGE)
        self.assertEqual(refs, {"tfw.Before", "tfw.A", "tfw.B", "tfw.C", "tfw.After", "tfw.New", "tfw.Status"})


if __name__ == "__main__":
    unittest.main()
