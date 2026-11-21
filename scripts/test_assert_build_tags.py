# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/assert-build-tags.sh (make scripts-test).

The PR-tier lint step runs the script in xkms/ with GO_BUILD_TAGS set. Each
run here puts a stub `go` first on PATH: it records its argv, takes the tag
set from the argument after `-tags` (none when `-tags` is absent) and prints
the file list STUB_TABLE gives for it, keyed by the sorted tags joined by
spaces. The tags are placeholders. No Go.

ASSERT_BUILD_TAGS_SCRIPT names another copy of the script to run (a scratch
mutation only); unset, the real script runs.
"""

import json
import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("ASSERT_BUILD_TAGS_SCRIPT") or os.path.join(HERE, "assert-build-tags.sh")
OK = "OK: every declared tag selects code"

STUB_GO = """#!/usr/bin/env python3
import json, os, sys
argv = sys.argv[1:]
tags = argv[argv.index("-tags") + 1].split() if "-tags" in argv else []
key = " ".join(sorted(tags))
table = json.loads(os.environ["STUB_TABLE"])
with open(os.environ["STUB_RECORD"], "a") as f:
    f.write(json.dumps({"argv": argv, "key": key, "missing": key not in table}) + "\\n")
for name in table.get(key, []):
    print(name)
"""


def run(tags, table):
    """Run the script with GO_BUILD_TAGS=tags (None: unset); return (proc, go calls)."""
    with tempfile.TemporaryDirectory() as tmp:
        stub = os.path.join(tmp, "go")
        with open(stub, "w") as f:
            f.write(STUB_GO)
        os.chmod(stub, 0o755)
        record = os.path.join(tmp, "record")
        env = {k: v for k, v in os.environ.items() if k != "GO_BUILD_TAGS"}
        env.update(PATH=tmp + os.pathsep + env.get("PATH", ""), STUB_TABLE=json.dumps(table), STUB_RECORD=record)
        if tags is not None:
            env["GO_BUILD_TAGS"] = tags
        proc = subprocess.run(["sh", SCRIPT], cwd=tmp, env=env, capture_output=True, text=True, timeout=30)
        calls = []
        if os.path.exists(record):
            with open(record) as f:
                calls = [json.loads(line) for line in f]
    return proc, calls


class AssertBuildTagsTest(unittest.TestCase):
    def check(self, tags, table, rc):
        proc, calls = run(tags, table)
        self.assertEqual(proc.returncode, rc, proc.stdout + proc.stderr)
        self.assertEqual([c["key"] for c in calls if c["missing"]], [], "a tag set the table does not name")
        return proc.stdout, calls

    def test_empty_tag_set_passes_without_go(self):
        for tags in (None, ""):
            with self.subTest(tags=tags):
                out, calls = self.check(tags, {}, 0)
                self.assertIn("nothing to assert", out)
                self.assertEqual(calls, [])

    def test_each_tag_adding_a_file_passes(self):
        out, _ = self.check("alpha beta", {
            "": ["p/a.go"],
            "alpha": ["p/a.go", "p/alpha.go"],
            "beta": ["p/a.go", "p/beta.go"],
            "alpha beta": ["p/a.go", "p/alpha.go", "p/beta.go"],
        }, 0)
        self.assertIn(OK, out)
        self.assertIn("untagged=1 tagged=3", out)

    def test_tag_set_that_changes_nothing_fails(self):
        out, _ = self.check("alpha", {"": ["p/a.go"], "alpha": ["p/a.go"]}, 1)
        self.assertIn("::error::-tags 'alpha' changed nothing", out)
        self.assertNotIn(OK, out)

    def test_tag_masked_by_a_real_sibling_fails(self):
        out, _ = self.check("alpha beta", {
            "": ["p/a.go"],
            "alpha": ["p/a.go", "p/alpha.go"],
            "beta": ["p/a.go"],
            "alpha beta": ["p/a.go", "p/alpha.go"],
        }, 1)
        self.assertIn("::error::build tag 'beta' selects nothing", out)
        self.assertNotIn("build tag 'alpha'", out)
        self.assertNotIn("changed nothing", out)

    def test_conjunction_passes(self):
        out, _ = self.check("alpha beta", {
            "": ["p/a.go"],
            "alpha": ["p/a.go"],
            "beta": ["p/a.go"],
            "alpha beta": ["p/a.go", "p/both.go"],
        }, 0)
        self.assertIn(OK, out)

    def test_stub_swapped_for_body_passes(self):
        # The same count with and without the tag: files are compared, not counts.
        out, _ = self.check("alpha", {"": ["p/stub.go"], "alpha": ["p/body.go"]}, 0)
        self.assertIn(OK, out)
        self.assertIn("untagged=1 tagged=1", out)

    def test_go_list_arguments(self):
        _, calls = self.check("alpha beta", {
            "": ["p/a.go"],
            "alpha": ["p/a.go", "p/alpha.go"],
            "beta": ["p/a.go", "p/beta.go"],
            "alpha beta": ["p/a.go", "p/alpha.go", "p/beta.go"],
        }, 0)
        self.assertEqual([c["key"] for c in calls], ["", "alpha beta", "beta", "alpha"])
        self.assertNotIn("-tags", calls[0]["argv"])
        for c in calls:
            self.assertEqual(c["argv"][0], "list")
            self.assertEqual(c["argv"][-1], "./...")
            self.assertEqual("-tags" in c["argv"], c["key"] != "")


if __name__ == "__main__":
    unittest.main()
