#!/usr/bin/env python3
"""The scripts tests run on the standard library alone (#613, Codex round 5).

scripts-test runs in the toolchain image, whose Python has no PyYAML
(docker/packages.txt installs no python3-yaml). Every scripts/test_*.py
imports only standard-library modules and the repository's own scripts.

Run: python3 scripts/test_ci_workflow_text.py
"""

import ast
import glob
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


class StandardLibraryOnly(unittest.TestCase):
    def test_tests_import_only_stdlib_and_scripts(self):
        own = {os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(HERE, "*.py"))}
        own |= {os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(HERE, "*", "__init__.py"))}
        tests = sorted(glob.glob(os.path.join(HERE, "test_*.py")))
        self.assertTrue(tests)
        for path in tests:
            with open(path, encoding="utf-8") as f:
                tree = ast.parse(f.read(), path)
            names = set()
            for n in ast.walk(tree):
                if isinstance(n, ast.Import):
                    names |= {a.name.split(".")[0] for a in n.names}
                elif isinstance(n, ast.ImportFrom) and n.level == 0:
                    names.add(n.module.split(".")[0])
            with self.subTest(test=os.path.basename(path)):
                self.assertEqual(sorted(names - set(sys.stdlib_module_names) - own), [])


if __name__ == "__main__":
    unittest.main()
