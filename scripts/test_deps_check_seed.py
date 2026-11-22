# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# deps-check.py's seed_modcache on its own: unset it does nothing, a seed
# without cache/download is skipped, only @v go.mod and .info files are copied,
# a file already in the run's cache is kept. No go, no network: both caches are
# temporary directories and the module's `run` is mocked.

import contextlib
import importlib.util
import io
import os
import shutil
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
# Overridable only to prove the test bites against a mutated copy.
SCRIPT = os.environ.get("DEPS_CHECK_SCRIPT", os.path.join(HERE, "deps-check.py"))
spec = importlib.util.spec_from_file_location("deps_check", SCRIPT)
dc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dc)

GO_ENV = ["go", "env", "GOMODCACHE"]


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def read(path):
    with open(path) as f:
        return f.read()


def tree(top):
    out = []
    for root, _, files in os.walk(top):
        for f in files:
            out.append(os.path.relpath(os.path.join(root, f), top))
    return sorted(out)


class SeedModcache(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.seed = os.path.join(self.tmp, "seed")
        self.cache = os.path.join(self.tmp, "cache")
        os.makedirs(self.seed)
        os.makedirs(self.cache)

    def seed_call(self, seed):
        out = io.StringIO()
        with mock.patch.dict(os.environ, {"DEPS_SEED_GOMODCACHE": seed}), \
                mock.patch.object(dc, "run", return_value=self.cache + "\n") as run, \
                contextlib.redirect_stdout(out):
            dc.seed_modcache()
        return out.getvalue(), run

    def test_unset_does_nothing(self):
        out, run = self.seed_call("")
        self.assertEqual(out, "")
        run.assert_not_called()
        self.assertEqual(tree(self.cache), [])

    def test_seed_without_download_is_skipped(self):
        out, run = self.seed_call(self.seed)
        self.assertEqual(
            out,
            f"deps-check: DEPS_SEED_GOMODCACHE={self.seed} has no cache/download; not seeding\n")
        run.assert_not_called()
        self.assertEqual(tree(self.cache), [])

    def test_copies_only_missing_mod_and_info(self):
        v = os.path.join(self.seed, "cache", "download", "example.com", "m", "@v")
        for f in ("v1.0.0.mod", "v1.0.0.info", "v1.0.0.zip", "v1.0.0.ziphash",
                  "v1.0.0.lock", "list"):
            write(os.path.join(v, f), "seed " + f)
        write(os.path.join(self.seed, "cache", "download", "example.com", "m", "x.mod"), "outside")
        mine = os.path.join(self.cache, "cache", "download", "example.com", "m", "@v", "v1.0.0.info")
        write(mine, "mine")

        out, run = self.seed_call(self.seed)
        self.assertEqual(out, f"deps-check: seeded 1 go.mod/.info file(s) from {self.seed}\n")
        run.assert_called_once_with(GO_ENV)
        self.assertEqual(tree(self.cache), [
            "cache/download/example.com/m/@v/v1.0.0.info",
            "cache/download/example.com/m/@v/v1.0.0.mod",
        ])
        self.assertEqual(read(mine), "mine")
        self.assertEqual(
            read(os.path.join(self.cache, "cache", "download", "example.com", "m", "@v", "v1.0.0.mod")),
            "seed v1.0.0.mod")

        out, _ = self.seed_call(self.seed)
        self.assertEqual(out, f"deps-check: seeded 0 go.mod/.info file(s) from {self.seed}\n")
        self.assertEqual(read(mine), "mine")


if __name__ == "__main__":
    unittest.main()
