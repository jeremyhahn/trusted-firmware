# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/release_alpha.py's tag arithmetic (make scripts-test).

series keeps only the names of its own prefix that are versions; next_alpha
takes the newest plain vX.Y.Z-alpha by semver precedence and bumps PATCH, and
refuses when there is none; check_monotonic refuses a new version equal to or
below any tag of its own series, and only of its own series. Each test calls
the helpers on lists of names: no git repository, no network, no tag.

RELEASE_ALPHA_SCRIPT names another copy of the script to test (a mutation run).
"""

import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("RELEASE_ALPHA_SCRIPT", os.path.join(REPO, "scripts", "release_alpha.py"))

_spec = importlib.util.spec_from_file_location("release_alpha_series", SCRIPT)
release_alpha = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(release_alpha)

ALPHA = ("alpha",)


class SeriesTest(unittest.TestCase):
    TAGS = ["v0.4.1-alpha", "sdk/go/v0.4.1-alpha", "xkms/v0.9.0-alpha", "v0.4", "notes", "sdk/go/vX"]

    def test_repo_series_keeps_only_its_versions(self):
        self.assertEqual(release_alpha.series(self.TAGS, ""), {"v0.4.1-alpha": (0, 4, 1, ALPHA)})

    def test_sdk_series_keeps_only_its_versions(self):
        self.assertEqual(release_alpha.series(self.TAGS, "sdk/go/"),
                         {"sdk/go/v0.4.1-alpha": (0, 4, 1, ALPHA)})


class NextAlphaTest(unittest.TestCase):
    def test_newest_by_precedence_not_by_text(self):
        self.assertEqual(release_alpha.next_alpha(["v0.4.9-alpha", "v0.4.10-alpha"]), (0, 4, 11, ALPHA))

    def test_only_a_plain_repo_alpha_is_the_base(self):
        tags = ["v0.4.1-alpha", "v0.5.0-alpha.1", "v0.5.0-rc.1", "v0.5.0",
                "sdk/go/v0.9.0-alpha", "xkms/v0.9.0-alpha"]
        self.assertEqual(release_alpha.next_alpha(tags), (0, 4, 2, ALPHA))

    def test_no_alpha_is_refused(self):
        for tags in ([], ["v0.4.1", "sdk/go/v0.4.1-alpha"]):
            with self.subTest(tags=tags):
                with self.assertRaises(SystemExit) as cm:
                    release_alpha.next_alpha(tags)
                self.assertIn("cut the first by hand", str(cm.exception.code))


class CheckMonotonicTest(unittest.TestCase):
    NEW = (0, 4, 2, ALPHA)

    def test_above_every_tag_passes(self):
        self.assertIsNone(release_alpha.check_monotonic(["v0.4.1-alpha"], "", self.NEW))

    def test_equal_tag_is_refused(self):
        with self.assertRaises(SystemExit) as cm:
            release_alpha.check_monotonic(["v0.4.2-alpha"], "", self.NEW)
        self.assertIn("v0.4.2-alpha would not sort above v0.4.2-alpha", str(cm.exception.code))

    def test_another_series_does_not_block(self):
        self.assertIsNone(release_alpha.check_monotonic(["v0.9.0", "sdk/go/v0.4.1-alpha"], "sdk/go/", self.NEW))

    def test_higher_tag_of_the_series_is_refused(self):
        with self.assertRaises(SystemExit) as cm:
            release_alpha.check_monotonic(["sdk/go/v0.4.2-rc.1"], "sdk/go/", self.NEW)
        self.assertIn("sdk/go/v0.4.2-rc.1", str(cm.exception.code))


if __name__ == "__main__":
    unittest.main()
