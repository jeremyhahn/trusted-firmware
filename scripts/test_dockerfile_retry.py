# SPDX-License-Identifier: AGPL-3.0-or-later
"""Every module or tool download in the devcontainer Dockerfile retries.

One proxy stream reset failed the whole image twice: a tool install (main
72bc7770, DS-9630, #612) and then `go mod download` (main 329216b7, run
44795756466). Each `go mod download`, `go install` and `go get`, and each
`curl` in a RUN that defines retry(), must run as `retry <command>` in a RUN
that defines retry(). Run: python3 scripts/test_dockerfile_retry.py
"""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCKERFILE = os.path.join(ROOT, "build", "docker", "devcontainer", "Dockerfile")

GO_FETCH = re.compile(r"\bgo\s+(?:mod\s+download|install|get)\b")
CURL = re.compile(r"\bcurl\s+-")
RETRY_DEF = re.compile(r"\bretry\(\)\s*\{")
QUOTED = re.compile(r"\"(?:[^\"\\]|\\.)*\"|'[^']*'")


def run_instructions(text):
    """Each RUN instruction, continuation lines joined, comments dropped."""
    runs, cur = [], None
    for line in text.splitlines():
        s = line.strip()
        if cur is None:
            if not re.match(r"RUN\b", s):
                continue
            cur = s[3:]
        elif s.startswith("#"):
            continue
        else:
            cur += " " + s
        if cur.endswith("\\"):
            cur = cur[:-1]
            continue
        runs.append(cur)
        cur = None
    if cur is not None:
        runs.append(cur)
    return runs


def unretried(run):
    """The fetch commands in one RUN that do not run under retry."""
    # Quoted words are arguments (echo text, URLs), never the command.
    body = QUOTED.sub('""', run)
    defines = bool(RETRY_DEF.search(body))
    found = [m for m in GO_FETCH.finditer(body)]
    if defines:
        found += list(CURL.finditer(body))
    bad = []
    for m in found:
        if not defines or not re.search(r"\bretry\s+$", body[: m.start()]):
            bad.append(m.group(0))
    return bad


class DockerfileRetry(unittest.TestCase):
    def setUp(self):
        with open(DOCKERFILE, encoding="utf-8") as f:
            self.runs = run_instructions(f.read())

    def test_every_download_retries(self):
        for run in self.runs:
            self.assertEqual(unretried(run), [], run[:200])

    def test_module_download_present(self):
        self.assertTrue(any("go mod download" in QUOTED.sub("", r) for r in self.runs))

    def test_checker_flags_a_bare_download(self):
        self.assertEqual(unretried(' set -eu; (cd d && go mod download)'), ["go mod download"])
        self.assertEqual(unretried(' echo "go mod download: x"'), [])
        ok = ' retry() { n=1; until "$@"; do n=$((n+1)); done; }; (cd d && retry go mod download)'
        self.assertEqual(unretried(ok), [])
        self.assertEqual(unretried(ok + "; curl -fsSL -o x y"), ["curl -"])


if __name__ == "__main__":
    unittest.main()
