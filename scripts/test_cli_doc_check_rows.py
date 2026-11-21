# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/cli-doc-check.py's page reader (make scripts-test).

in_section names the tfwctl section's heading; page_rows takes the first
column's backticked names of that section's ### Commands and ### Flags tables
and nothing else; go_files hands over tfwctl's Go files, tests left out, in
order. A reader that took names from every column, or from another tool's
section, would let a verb named only in a description satisfy the gate.
CLI_DOC_CHECK_SCRIPT names another copy of the script (a mutation run only).
No network.
"""

import importlib.util
import os
import shutil
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("CLI_DOC_CHECK_SCRIPT", os.path.join(HERE, "cli-doc-check.py"))
spec = importlib.util.spec_from_file_location("cli_doc_check_rows", SCRIPT)
cdc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cdc)

PAGE = """# Host SDK -- command-line reference

## `tfwsim` -- the simulator

### Commands

| `simverb` | a simulator verb is not tfwctl's |

## `tfwctl` (host CLI)

Prose that names `prosey` is not a row.

### Commands\x20\x20

| Command | Does |
|---|---|
| `ping` | prints `pong` |
| `netcfg set` `netcfg get` | writes or reads the block |

### Notes

| `noted` | a row under another subsection |

### Flags

| Flag | Meaning |
|---|---|
| `-baud` | serial baud rate, `115200` |

## `tfwprov`

### Flags

| `-commit` | another command's flag |
"""


class TestInSection(unittest.TestCase):
    def test_the_heading_with_or_without_a_suffix(self):
        for line in ("## `tfwctl`", "## `tfwctl` (host CLI)"):
            with self.subTest(line=line):
                self.assertTrue(cdc.in_section(line))

    def test_other_headings_are_not_the_section(self):
        for line in ("## `tfwsim`", "## `tfwctl`x", "### `tfwctl`", " ## `tfwctl`"):
            with self.subTest(line=line):
                self.assertFalse(cdc.in_section(line))


class TestPageRows(unittest.TestCase):
    def setUp(self):
        self.rows = cdc.page_rows(PAGE)

    def test_commands_are_the_first_column_verbs(self):
        self.assertEqual(["ping", "netcfg", "netcfg"], self.rows["### Commands"])

    def test_flags_are_the_first_column_names_whole(self):
        self.assertEqual(["-baud"], self.rows["### Flags"])

    def test_a_description_column_names_nothing(self):
        self.assertNotIn("pong", self.rows["### Commands"])
        self.assertNotIn("115200", self.rows["### Flags"])

    def test_other_sections_subsections_and_prose_add_nothing(self):
        found = self.rows["### Commands"] + self.rows["### Flags"]
        for name in ("simverb", "-commit", "noted", "prosey"):
            with self.subTest(name=name):
                self.assertNotIn(name, found)

    def test_a_page_without_the_section_gives_no_rows(self):
        page = PAGE.replace("## `tfwctl` (host CLI)", "## tfwctl")
        self.assertEqual({"### Commands": [], "### Flags": []}, cdc.page_rows(page))


class TestGoFiles(unittest.TestCase):
    def test_sorted_go_files_without_tests(self):
        root = tempfile.mkdtemp(prefix="cli-doc-check-rows-")
        self.addCleanup(shutil.rmtree, root)
        d = os.path.join(root, "sdk", "go", "cmd", "tfwctl")
        os.makedirs(d)
        for name in ("b.go", "a.go", "a_test.go", "notes.txt"):
            with open(os.path.join(d, name), "w", encoding="utf-8") as f:
                f.write("text of " + name)
        self.assertEqual([("a.go", "text of a.go"), ("b.go", "text of b.go")],
                         list(cdc.go_files(root)))


if __name__ == "__main__":
    unittest.main()
