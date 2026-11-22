# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/deps-check.py's pure helpers (make scripts-test).

Every verdict of `make deps-check` rests on these: a version's numbers, a
Debian version read back from dpkg without its epoch and revision, the Report
that prints ok/held/BEHIND and counts what fails, go list's stream of JSON
objects, and a go.mod `held:` reason found for its own module only. dpkg is
replaced by a patched run(); no network, no dpkg, no go.

DEPS_CHECK_SCRIPT names another copy of the script, for a mutation run only.
"""

import contextlib
import importlib.util
import io
import json
import os
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("DEPS_CHECK_SCRIPT") or os.path.join(REPO, "scripts", "deps-check.py")
spec = importlib.util.spec_from_file_location("deps_check_helpers", SCRIPT)
dc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dc)


class Numbers(unittest.TestCase):
    def test_a_version_is_its_numbers(self):
        self.assertEqual(dc.numbers("go1.25.3"), (1, 25, 3))
        self.assertEqual(dc.numbers("v0.58.2"), (0, 58, 2))

    def test_show_round_trips_them(self):
        self.assertEqual(dc.show(dc.numbers("go1.25.3")), "1.25.3")
        self.assertEqual(dc.show(dc.numbers("v0.58.2")), "0.58.2")


class DpkgVersion(unittest.TestCase):
    # What dpkg-query says, and the upstream release it is.
    CASES = (
        ("1:5.4.0+Atmel3.6.2-3", (5, 4, 0)),
        ("2.0.0+Atmel3.6.2-3", (2, 0, 0)),
        ("1.6-6ubuntu1", (1, 6)),
        ("7.3.0~rc1-2", (7, 3, 0)),
        ("2:1.9.2-1-1", (1, 9, 2, 1)),
    )

    def test_the_epoch_and_revision_are_dropped(self):
        for answer, want in self.CASES:
            with self.subTest(answer=answer):
                with mock.patch.object(dc, "run", return_value=answer + "\n") as r:
                    self.assertEqual(dc.dpkg_version("gcc-avr"), want)
                self.assertEqual(r.call_args.args[0],
                                 ["dpkg-query", "-W", "-f", "${Version}", "gcc-avr"])

    def test_an_empty_answer_names_the_package(self):
        with mock.patch.object(dc, "run", return_value=""):
            with self.assertRaisesRegex(dc.LookupFailed, "^avr-libc is not installed"):
                dc.dpkg_version("avr-libc")


class ReportCounts(unittest.TestCase):
    def check(self, pinned, latest, reason):
        rep, out = dc.Report(), io.StringIO()
        with contextlib.redirect_stdout(out):
            rep.check("simavr", pinned, latest, reason)
        return rep, out.getvalue()

    def test_equal_and_newer_are_ok(self):
        for pinned in ((1, 6), (1, 7)):
            with self.subTest(pinned=pinned):
                rep, out = self.check(pinned, (1, 6), "")
                self.assertEqual(out, f"  ok      simavr {dc.show(pinned)}\n")
                self.assertEqual((rep.behind, rep.failed), (0, 0))

    def test_older_with_a_reason_is_held_and_not_counted(self):
        rep, out = self.check((1, 6), (1, 7), "waits on the image")
        self.assertEqual(out, "  held    simavr 1.6 (latest 1.7): waits on the image\n")
        self.assertEqual((rep.behind, rep.failed), (0, 0))

    def test_older_with_no_reason_is_behind_and_counted(self):
        rep, out = self.check((1, 6), (1, 7), "")
        self.assertEqual(out, "  BEHIND  simavr 1.6 (latest 1.7), no reason recorded\n")
        self.assertEqual((rep.behind, rep.failed), (1, 0))

    def test_fail_is_printed_and_counted(self):
        rep, out = dc.Report(), io.StringIO()
        with contextlib.redirect_stdout(out):
            rep.fail("go", "no answer")
        self.assertEqual(out.getvalue(), "  FAILED  go: no answer\n")
        self.assertEqual((rep.behind, rep.failed), (0, 1))


class DecodeStream(unittest.TestCase):
    def test_objects_come_back_in_order(self):
        text = '\n  {"Path": "a"}{"Path": "b"}\n{\n  "Path": "c"\n}\n\t '
        self.assertEqual(dc.decode_stream(text), [{"Path": "a"}, {"Path": "b"}, {"Path": "c"}])

    def test_empty_text_is_no_object(self):
        self.assertEqual(dc.decode_stream(""), [])
        self.assertEqual(dc.decode_stream(" \n"), [])

    def test_a_truncated_object_raises(self):
        with self.assertRaises(json.JSONDecodeError):
            dc.decode_stream('{"Path": "a"}\n{"Path": ')


class HeldInGomod(unittest.TestCase):
    GOMOD = [
        "require (\n",
        "\tgolang.org/x/tools/gopls v0.20.0 // held: gopls 0.21 needs go 1.26\n",
        "\tgithub.com/minio/minlz v1.2.2 // indirect\n",
        "\tgithub.com/held:/x v1.0.0\n",
        "\tgolang.org/x/telemetry v0.1.0 // indirect; held: waits on the image cache\n",
        ")\n",
    ]

    def test_the_reason_after_held_is_found_for_its_module(self):
        self.assertEqual(dc.held_in_gomod(self.GOMOD, "golang.org/x/tools/gopls"),
                         "gopls 0.21 needs go 1.26")
        self.assertEqual(dc.held_in_gomod(self.GOMOD, "golang.org/x/telemetry"),
                         "waits on the image cache")

    def test_a_prefix_of_the_module_borrows_no_reason(self):
        self.assertEqual(dc.held_in_gomod(self.GOMOD, "golang.org/x/tools"), "")

    def test_a_comment_without_held_is_no_reason(self):
        self.assertEqual(dc.held_in_gomod(self.GOMOD, "github.com/minio/minlz"), "")

    def test_held_in_the_code_half_is_no_reason(self):
        self.assertEqual(dc.held_in_gomod(self.GOMOD, "github.com/held:/x"), "")


if __name__ == "__main__":
    unittest.main()
