# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/deps-check.py's pins, tag choice and verdict (make scripts-test).

`make deps-check` reads scripts/tool-pins.sh, takes the highest release tag
upstream (never a pre-release), asks the registry for the base image's digest
and turns its Report into an exit code. run(), fetch() and the checks main()
calls are patched; no network, no git, no dpkg.

DEPS_CHECK_SCRIPT names another copy of the script, for a mutation run only.
"""

import contextlib
import importlib.util
import io
import os
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("DEPS_CHECK_SCRIPT") or os.path.join(REPO, "scripts", "deps-check.py")
spec = importlib.util.spec_from_file_location("deps_check_pins", SCRIPT)
dc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dc)


class LoadPins(unittest.TestCase):
    def pins(self, text):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "tool-pins.sh")
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            return dc.load_pins(path)

    def test_a_plain_pin_is_its_value(self):
        self.assertEqual(self.pins("GO_VERSION=1.27.4\n"), {"GO_VERSION": "1.27.4"})

    def test_one_pair_of_double_quotes_is_stripped(self):
        self.assertEqual(self.pins('HELD_GO="a, b"\n'), {"HELD_GO": "a, b"})

    def test_a_lone_quote_is_kept(self):
        self.assertEqual(self.pins('ONE="\n'), {"ONE": '"'})

    def test_single_quotes_are_kept(self):
        self.assertEqual(self.pins("S='x'\n"), {"S": "'x'"})

    def test_an_empty_value_is_an_empty_pin(self):
        self.assertEqual(self.pins("EMPTY=\n"), {"EMPTY": ""})

    def test_lines_that_are_not_a_pin_are_skipped(self):
        text = "# C=1\nexport X=1\nlower=1\n INDENT=1\n1A=2\n\n"
        self.assertEqual(self.pins(text), {})

    def test_the_repo_pins_name_every_dockerfile_pin(self):
        pins = dc.load_pins(os.path.join(REPO, dc.PINS))
        for path, keys in dc.DOCKERFILE_PINS:
            for key in keys:
                with self.subTest(path=path, key=key):
                    self.assertIn(key, pins)
                    self.assertNotEqual(pins[key], "")


def ls_remote(*tags):
    return "".join(f"{i:040x}\trefs/tags/{t}\n" for i, t in enumerate(tags))


class LatestTag(unittest.TestCase):
    PLAIN = r"v(\d+)\.(\d+)\.(\d+)"

    def latest(self, out, pattern=PLAIN):
        with mock.patch.object(dc, "run", return_value=out) as run:
            return dc.latest_tag("R", pattern), run

    def test_the_highest_is_numeric_not_lexical(self):
        best, _ = self.latest(ls_remote("v1.2.0", "v1.10.0", "v1.9.9"))
        self.assertEqual(best, (1, 10, 0))

    def test_a_pre_release_is_never_taken(self):
        best, _ = self.latest(ls_remote("v1.2.0", "v1.3.0-rc1", "v1.3.0-beta"))
        self.assertEqual(best, (1, 2, 0))

    def test_an_optional_group_that_did_not_match_is_dropped(self):
        best, _ = self.latest(ls_remote("v2.1", "v2.0.9"), r"v(\d+)\.(\d+)(?:\.(\d+))?")
        self.assertEqual(best, (2, 1))

    def test_it_asks_ls_remote_upstream(self):
        _, run = self.latest(ls_remote("v1.0.0"))
        run.assert_called_once_with(["git", "ls-remote", "--tags", "--refs", "R"], upstream=True)

    def test_no_match_names_repo_and_pattern(self):
        with self.assertRaises(dc.LookupFailed) as ctx:
            self.latest(ls_remote("v1.3.0-rc1"))
        self.assertIn("R: no tag matches", str(ctx.exception))


class BaseDigest(unittest.TestCase):
    def digest(self, image):
        answers = [('{"token": "T"}', {}), ("", {"Docker-Content-Digest": "sha256:ab"})]
        with mock.patch.object(dc, "fetch", side_effect=answers) as fetch:
            return dc.base_digest(image), fetch.call_args_list

    def test_an_official_image_gets_library(self):
        got, calls = self.digest("debian:trixie-slim")
        self.assertEqual(got, "sha256:ab")
        self.assertEqual(len(calls), 2)
        self.assertTrue(calls[0].args[0].endswith("scope=repository:library/debian:pull"))
        self.assertEqual(calls[1].args[0], "https://registry-1.docker.io/v2/library/debian/manifests/trixie-slim")
        self.assertEqual(calls[1].kwargs["method"], "HEAD")
        self.assertEqual(calls[1].kwargs["headers"]["Authorization"], "Bearer T")

    def test_a_namespaced_image_keeps_its_repo(self):
        got, calls = self.digest("acme/tool:1.0")
        self.assertEqual(got, "sha256:ab")
        self.assertTrue(calls[0].args[0].endswith("scope=repository:acme/tool:pull"))
        self.assertEqual(calls[1].args[0], "https://registry-1.docker.io/v2/acme/tool/manifests/1.0")
        self.assertNotIn("library/", calls[1].args[0])


def report(behind=0, failed=0):
    rep = dc.Report()
    rep.behind, rep.failed = behind, failed
    return rep


class Main(unittest.TestCase):
    def main(self, argv, git="/usr/bin/git", toolchain=None, go=None, download=3):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(dc, "toolchain", return_value=toolchain or report()) as tc, \
                mock.patch.object(dc, "go_modules", return_value=go or report()) as gm, \
                mock.patch.object(dc, "download", return_value=download) as dl, \
                mock.patch.object(dc.shutil, "which", return_value=git) as which, \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = dc.main(argv)
        return code, out.getvalue(), err.getvalue(), {"toolchain": tc, "go_modules": gm,
                                                      "download": dl, "which": which}

    def test_usage_is_exit_2(self):
        for argv in ([], ["d"], ["d", "x"], ["d", "go"], ["d", "download"]):
            with self.subTest(argv=argv):
                code, _, err, mocks = self.main(argv)
                self.assertEqual(code, 2)
                self.assertTrue(err.startswith("usage: deps-check.py"))
                for name, m in mocks.items():
                    self.assertFalse(m.called, name)

    def test_download_skips_the_git_check(self):
        code, _, _, mocks = self.main(["d", "download", "m"], git=None)
        self.assertEqual(code, 3)
        mocks["download"].assert_called_once_with(["m"])

    def test_git_missing_is_exit_1(self):
        code, _, err, mocks = self.main(["d", "toolchain"], git=None)
        self.assertEqual(code, 1)
        self.assertIn("git is missing", err)
        self.assertFalse(mocks["toolchain"].called)

    def test_a_clean_report_is_exit_0(self):
        code, _, _, mocks = self.main(["d", "toolchain"])
        self.assertEqual(code, 0)
        mocks["toolchain"].assert_called_once_with()
        code, _, _, mocks = self.main(["d", "go", "a", "b"])
        self.assertEqual(code, 0)
        mocks["go_modules"].assert_called_once_with(["a", "b"])

    def test_a_failed_lookup_is_exit_1(self):
        code, out, _, _ = self.main(["d", "toolchain"], toolchain=report(failed=2))
        self.assertEqual(code, 1)
        self.assertIn("2 lookup(s) failed; an unanswered check is not a pass", out)

    def test_behind_without_reason_is_exit_1(self):
        code, out, _, _ = self.main(["d", "toolchain"], toolchain=report(behind=1))
        self.assertEqual(code, 1)
        self.assertIn("1 behind without a recorded reason", out)


if __name__ == "__main__":
    unittest.main()
