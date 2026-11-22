# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# scripts/repro_build.py's app_dir_arg on its own: which --app-dir is inside
# the source tree (built from each clean copy, by relative path) and which is
# outside (one directory by absolute path, which must hold an app.mk). The
# helper only resolves paths, so these tests make directories in a temporary
# tree and run no make, no git and no build. REPRO_BUILD_SCRIPT points them at
# another copy of the script (to see that they fail when its check is wrong).

import importlib.util
import os
import shutil
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.environ.get("REPRO_BUILD_SCRIPT", os.path.join(HERE, "repro_build.py"))
spec = importlib.util.spec_from_file_location("repro_build", SCRIPT)
rb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rb)


class AppDirArgTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root)
        self.src = os.path.join(self.root, "src")
        self.ext = os.path.join(self.root, "ext")
        for d in (os.path.join(self.src, "app", "ex"), os.path.join(self.src, "..app"), self.ext):
            os.makedirs(d)

    def refused(self, app_dir, text):
        with self.assertRaises(SystemExit) as cm:
            rb.app_dir_arg(self.src, app_dir)
        self.assertIn(text, str(cm.exception))

    def test_in_tree_relative_however_spelt(self):
        self.assertEqual(rb.app_dir_arg(self.src, "app/ex"), ("app/ex", "app/ex"))
        self.assertEqual(rb.app_dir_arg(self.src, os.path.join(self.src, "app", "ex")),
                         ("app/ex", "app/ex"))

    def test_dotted_sibling_is_inside(self):
        # "..app" starts with "..", but is a directory in the tree, not its parent.
        self.assertEqual(rb.app_dir_arg(self.src, "..app"), ("..app", "..app"))

    def test_tree_itself_refused(self):
        for app_dir in (".", self.src, "app/.."):
            with self.subTest(app_dir=app_dir):
                self.refused(app_dir, "is the source tree itself")

    def test_outside_needs_app_mk_then_absolute(self):
        for app_dir in (self.ext, "../ext"):
            with self.subTest(app_dir=app_dir):
                self.refused(app_dir, "has no app.mk")
        with open(os.path.join(self.ext, "app.mk"), "w") as f:
            f.write("APP_SRCS :=\n")
        for app_dir in (self.ext, "../ext"):
            with self.subTest(app_dir=app_dir):
                self.assertEqual(rb.app_dir_arg(self.src, app_dir), (os.path.realpath(self.ext), None))

    def test_links_resolve_to_where_they_point(self):
        with open(os.path.join(self.ext, "app.mk"), "w") as f:
            f.write("APP_SRCS :=\n")
        os.symlink(self.ext, os.path.join(self.src, "link"))
        os.symlink(os.path.join(self.src, "app", "ex"), os.path.join(self.ext, "back"))
        self.assertEqual(rb.app_dir_arg(self.src, "link"), (os.path.realpath(self.ext), None))
        self.assertEqual(rb.app_dir_arg(self.src, os.path.join(self.ext, "back")),
                         ("app/ex", "app/ex"))


if __name__ == "__main__":
    unittest.main()
