# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# deps-check.py's retry of a transient upstream failure, through a fake runner
# and opener: no network, no sleep.

import importlib.util
import io
import os
import subprocess
import unittest
import urllib.error
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("deps_check", os.path.join(HERE, "deps-check.py"))
dc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dc)

LS_REMOTE = ["git", "ls-remote", "--tags", "--refs", "https://go.googlesource.com/vuln"]
ERR_503 = ("fatal: unable to access 'https://go.googlesource.com/vuln/': "
           "The requested URL returned error: 503")
ERR_404 = ("fatal: unable to access 'https://go.googlesource.com/nope/': "
           "The requested URL returned error: 404")
TAGS = "abc\trefs/tags/v1.1.4\ndef\trefs/tags/v1.2.0\n"


def done(rc, stdout="", stderr=""):
    return subprocess.CompletedProcess(LS_REMOTE, rc, stdout, stderr)


def http_error(code):
    return urllib.error.HTTPError("https://go.dev/VERSION?m=text", code, "status", {}, io.BytesIO())


class Answer(io.BytesIO):
    headers = {}


class RunRetry(unittest.TestCase):
    def setUp(self):
        self.sleep = mock.patch.object(dc.time, "sleep").start()
        self.addCleanup(mock.patch.stopall)

    def runner(self, *answers):
        return mock.patch.object(dc.subprocess, "run", side_effect=list(answers)).start()

    def test_503_then_success_passes(self):
        r = self.runner(done(128, stderr=ERR_503), done(0, stdout=TAGS))
        self.assertEqual(dc.latest_tag(LS_REMOTE[-1], r"v(\d+)\.(\d+)\.(\d+)"), (1, 2, 0))
        self.assertEqual(r.call_count, 2)
        self.sleep.assert_called_once_with(dc.BACKOFF[0])

    def test_three_503s_fail_with_the_original_message(self):
        r = self.runner(*[done(128, stderr=ERR_503)] * 3)
        with self.assertRaises(dc.LookupFailed) as e:
            dc.latest_tag(LS_REMOTE[-1], r"v(\d+)\.(\d+)\.(\d+)")
        self.assertEqual(str(e.exception), f"{' '.join(LS_REMOTE)}: {ERR_503}")
        self.assertEqual(r.call_count, 3)
        self.assertEqual([c.args[0] for c in self.sleep.call_args_list], list(dc.BACKOFF))

    def test_404_fails_at_once(self):
        r = self.runner(done(128, stderr=ERR_404), done(0, stdout=TAGS))
        with self.assertRaises(dc.LookupFailed) as e:
            dc.latest_tag(LS_REMOTE[-1], r"v(\d+)\.(\d+)\.(\d+)")
        self.assertIn("error: 404", str(e.exception))
        self.assertEqual(r.call_count, 1)
        self.sleep.assert_not_called()

    def test_no_tag_matches_is_not_retried(self):
        r = self.runner(done(0, stdout=TAGS), done(0, stdout=TAGS))
        with self.assertRaisesRegex(dc.LookupFailed, "no tag matches"):
            dc.latest_tag(LS_REMOTE[-1], r"release-(\d+)")
        self.assertEqual(r.call_count, 1)

    def test_a_local_command_is_not_retried(self):
        r = self.runner(done(1, stderr=ERR_503), done(0))
        with self.assertRaises(dc.LookupFailed):
            dc.run(["go", "mod", "edit", "-json"])
        self.assertEqual(r.call_count, 1)

    def test_go_proxy_503_is_transient(self):
        self.assertTrue(dc.TRANSIENT.search(
            "go: example.com/m@v1.0.0: reading https://proxy.golang.org/example.com/m/@v/list: "
            "503 Service Unavailable"))
        self.assertFalse(dc.TRANSIENT.search(
            "go: reading https://proxy.golang.org/example.com/m/@v/list: 404 Not Found"))

    def test_the_bound_holds_every_attempt_and_pause(self):
        # Each attempt is given what is left of BOUND, never a fresh 600 s.
        clock = iter([0, 0, 599, 599])
        mock.patch.object(dc.time, "monotonic", side_effect=lambda: next(clock)).start()
        r = self.runner(done(128, stderr=ERR_503))
        with self.assertRaises(dc.LookupFailed):
            dc.run(LS_REMOTE, upstream=True)
        self.assertEqual(r.call_args.kwargs["timeout"], dc.BOUND)
        self.assertEqual(r.call_count, 1)
        self.sleep.assert_not_called()


class FetchRetry(unittest.TestCase):
    def setUp(self):
        self.sleep = mock.patch.object(dc.time, "sleep").start()
        self.addCleanup(mock.patch.stopall)

    def opener(self, *answers):
        return mock.patch.object(dc.urllib.request, "urlopen", side_effect=list(answers)).start()

    def test_503_then_success_passes(self):
        o = self.opener(http_error(503), Answer(b"go1.25.3\n"))
        body, _ = dc.fetch("https://go.dev/VERSION?m=text")
        self.assertEqual(body, "go1.25.3\n")
        self.assertEqual(o.call_count, 2)

    def test_three_503s_fail_with_the_original_message(self):
        o = self.opener(*[http_error(503) for _ in range(3)])
        with self.assertRaises(dc.LookupFailed) as e:
            dc.fetch("https://go.dev/VERSION?m=text")
        self.assertEqual(str(e.exception), "https://go.dev/VERSION?m=text: HTTP Error 503: status")
        self.assertEqual(o.call_count, 3)

    def test_429_and_connection_reset_are_retried(self):
        o = self.opener(http_error(429), ConnectionResetError(104, "reset"), Answer(b"ok"))
        self.assertEqual(dc.fetch("https://go.dev/VERSION?m=text")[0], "ok")
        self.assertEqual(o.call_count, 3)

    def test_404_fails_at_once(self):
        o = self.opener(http_error(404), Answer(b"ok"))
        with self.assertRaisesRegex(dc.LookupFailed, "HTTP Error 404"):
            dc.fetch("https://go.dev/VERSION?m=text")
        self.assertEqual(o.call_count, 1)
        self.sleep.assert_not_called()


if __name__ == "__main__":
    unittest.main()
