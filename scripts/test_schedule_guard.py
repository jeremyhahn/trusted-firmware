# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for the schedule guard and CI Gate's handling of its verdict.

The schedule guard step (`id: guard`) of the `preflight` job of
.github/workflows/ci.yml reads the Actions API on a
schedule run and decides `run`, `busy` or `unreadable`; the bigdisk jobs run on
`run` only, and CI Gate fails on any verdict but `run`. Only a schedule event on
a forge exercises that code, so a PR or push run never would. These tests take
the guard step's script out of the workflow and run it with bash against a fake
curl first on PATH (no network), answering each status query from a table, and
read the verdict from GITHUB_OUTPUT. They run the CI Gate step the same way,
and hold the `if:` of each bigdisk job and of the gate to the verdicts.
"""

import json
import os
import re
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
WORKFLOW = os.path.join(REPO, ".github", "workflows", "ci.yml")

API = "http://forge.invalid"

# The fake curl: reads the token from the -H @file the guard passes, the status
# from the URL, and answers from $FAKE_TABLE/<token>.<status> (line 1 the http
# code, line 2 the body). FAKE_RACE=1 models one run that is waiting at the
# first call and in progress from the second on. Every call is logged.
FAKE_CURL = r"""#!/usr/bin/env bash
out="" hdr=""
while [ $# -gt 1 ]; do
  case "$1" in
    -o) out="$2"; shift 2;;
    -H) hdr="${2#@}"; shift 2;;
    -w|--max-time) shift 2;;
    *) shift;;
  esac
done
url="$1"
token=$(sed -n 's/^Authorization: token //p' "$hdr")
st=${url##*status=}; st=${st%%&*}
echo "$token $st" >> "$FAKE_LOG"
if [ -n "${FAKE_RACE:-}" ]; then
  n=$(wc -l < "$FAKE_LOG")
  state=in_progress; [ "$n" -gt 1 ] || state=waiting
  c=0; [ "$st" = "$state" ] && c=1
  printf '{"total_count": %s}' "$c" > "$out"
  printf 200
  exit 0
fi
f="$FAKE_TABLE/$token.$st"
if [ ! -f "$f" ]; then printf 500; : > "$out"; exit 0; fi
{ read -r code; cat > "$out"; } < "$f"
printf '%s' "$code"
"""


def read_workflow():
    with open(WORKFLOW, encoding="utf-8") as f:
        return f.read()


def step_script(job, step_id):
    """The `run:` script of the step with `id: step_id` in `job`, dedented."""
    text = read_workflow()
    m = re.search(r"^  %s:\n(.*?)(?=^  [a-z]|\Z)" % re.escape(job), text, re.MULTILINE | re.DOTALL)
    if not m:
        raise AssertionError("ci.yml has no job %s" % job)
    body = m.group(1)
    if step_id:
        s = body.index("id: %s\n" % step_id)
        body = body[s:]
    r = re.search(r"^(\s+)run: \|\n((?:\1 .*\n|\s*\n)+)", body, re.MULTILINE)
    if not r:
        raise AssertionError("no run script in %s %s" % (job, step_id))
    indent = len(r.group(1)) + 2
    return "".join(l[indent:] if l.strip() else "\n" for l in r.group(2).splitlines(True))


def job_block(job):
    m = re.search(r"^  %s:\n(.*?)(?=^  [a-z]|\Z)" % re.escape(job), read_workflow(),
                  re.MULTILINE | re.DOTALL)
    if not m:
        raise AssertionError("ci.yml has no job %s" % job)
    return m.group(1)


def reply(code, total=None, body=None):
    if body is None:
        body = json.dumps({"total_count": total}) if total is not None else "{}"
    return "%s\n%s" % (code, body)


class Guard(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        d = self.tmp.name
        self.bin = os.path.join(d, "bin")
        self.table = os.path.join(d, "table")
        os.mkdir(self.bin)
        os.mkdir(self.table)
        self.log = os.path.join(d, "curl.log")
        self.out = os.path.join(d, "output")
        open(self.log, "w").close()
        curl = os.path.join(self.bin, "curl")
        with open(curl, "w") as f:
            f.write(FAKE_CURL)
        os.chmod(curl, os.stat(curl).st_mode | stat.S_IXUSR)
        self.script = step_script("preflight", "guard")

    def table_set(self, token, waiting, in_progress):
        for st, r in (("waiting", waiting), ("in_progress", in_progress)):
            with open(os.path.join(self.table, "%s.%s" % (token, st)), "w") as f:
                f.write(r)

    def run_guard(self, event="schedule", server=API, job="JOB", bot="", race=False):
        env = {
            "PATH": self.bin + os.pathsep + os.environ["PATH"],
            "EVENT": event, "SERVER": server,
            "RUNS_API": server + "/api/v1/repos/o/r/actions/workflows/ci.yml/runs",
            "JOB_TOKEN": job, "BOT": bot,
            "RUNNER_TEMP": self.tmp.name, "GITHUB_OUTPUT": self.out,
            "FAKE_LOG": self.log, "FAKE_TABLE": self.table,
        }
        if race:
            env["FAKE_RACE"] = "1"
        p = subprocess.run(["bash", "-c", self.script], env=env, capture_output=True,
                           text=True, check=False)
        with open(self.out, encoding="utf-8") as f:
            outs = f.read().split()
        with open(self.log, encoding="utf-8") as f:
            calls = f.read().splitlines()
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertEqual(len(outs), 1, outs)
        self.assertTrue(outs[0].startswith("verdict="), outs)
        return outs[0][len("verdict="):], calls, p.stdout

    def test_a_non_schedule_event_runs_without_a_call(self):
        for event in ("push", "pull_request", "workflow_dispatch"):
            with self.subTest(event=event):
                open(self.out, "w").close()
                verdict, calls, _ = self.run_guard(event=event)
                self.assertEqual(verdict, "run")
                self.assertEqual(calls, [])

    def test_the_github_mirror_runs_without_a_call(self):
        verdict, calls, _ = self.run_guard(server="https://github.com")
        self.assertEqual(verdict, "run")
        self.assertEqual(calls, [])

    def test_the_job_token_reading_zero_and_zero_runs(self):
        self.table_set("JOB", reply(200, 0), reply(200, 0))
        verdict, calls, _ = self.run_guard()
        self.assertEqual(verdict, "run")
        self.assertEqual(calls, ["JOB waiting", "JOB in_progress"])

    def test_a_waiting_run_is_busy(self):
        self.table_set("JOB", reply(200, 1), reply(200, 0))
        self.assertEqual(self.run_guard()[0], "busy")

    def test_an_in_progress_run_is_busy(self):
        self.table_set("JOB", reply(200, 0), reply(200, 2))
        self.assertEqual(self.run_guard()[0], "busy")

    def test_the_agent_token_is_read_when_the_job_token_gets_403_and_runs(self):
        self.table_set("JOB", reply(403), reply(403))
        self.table_set("BOTT", reply(200, 0), reply(200, 0))
        verdict, calls, out = self.run_guard(bot="BOTT")
        self.assertEqual(verdict, "run")
        self.assertIn("BOTT in_progress", calls)
        self.assertIn("read with AGENT_TOKEN", out)

    def test_the_agent_token_is_read_when_the_job_token_gets_403_and_busy(self):
        self.table_set("JOB", reply(403), reply(403))
        self.table_set("BOTT", reply(200, 0), reply(200, 1))
        self.assertEqual(self.run_guard(bot="BOTT")[0], "busy")

    def test_both_tokens_failing_is_unreadable(self):
        self.table_set("JOB", reply(403), reply(403))
        self.table_set("BOTT", reply(401), reply(401))
        self.assertEqual(self.run_guard(bot="BOTT")[0], "unreadable")

    def test_an_empty_agent_token_and_a_failing_job_token_is_unreadable(self):
        self.table_set("JOB", reply(403), reply(403))
        self.assertEqual(self.run_guard(bot="")[0], "unreadable")

    def test_no_tokens_at_all_is_unreadable(self):
        self.assertEqual(self.run_guard(job="", bot="")[0], "unreadable")

    def test_a_200_with_no_total_count_is_unreadable(self):
        self.table_set("JOB", reply(200), reply(200))
        self.assertEqual(self.run_guard()[0], "unreadable")

    def test_a_run_moving_from_waiting_to_in_progress_between_queries_is_counted(self):
        # The regression test of Codex review 8301: one run, waiting at the
        # first query and in progress from the second on. Asked in_progress
        # first, both counts are zero and the verdict is run.
        verdict, calls, _ = self.run_guard(race=True)
        self.assertEqual(verdict, "busy")
        self.assertEqual(len(calls), 2)
        self.assertTrue(calls[0].endswith("waiting"), calls)


class Workflow(unittest.TestCase):
    def test_every_guarded_job_needs_a_run_verdict_on_a_schedule_run(self):
        # The guarded jobs are the ones that need preflight: since the
        # hosted runners (#335) no runs-on names bigdisk to find them by.
        text = read_workflow()
        checked = 0
        for m in re.finditer(r"^  ([a-z0-9-]+):\n(.*?)(?=^  [a-z]|\Z)", text, re.MULTILINE | re.DOTALL):
            name, body = m.groups()
            head = body.split("    steps:")[0]
            if not re.search(r"needs: *(\[[^]]*)?preflight", head) or "if:" not in head:
                continue
            if name == "ci-gate":
                continue
            checked += 1
            with self.subTest(job=name):
                self.assertIn("needs.preflight.outputs.verdict == 'run'", head)
        self.assertGreater(checked, 5, "found too few jobs that need preflight: has the workflow's layout changed?")

    def test_ci_gate_is_skipped_on_busy_only(self):
        cond = re.search(r"^    if: (.*)$", job_block("ci-gate"), re.MULTILINE).group(1)
        self.assertIn("needs.preflight.outputs.verdict != 'busy'", cond)
        self.assertEqual(re.findall(r"verdict [!=]= '([a-z]+)'", cond), ["busy"])
        self.assertIn("always()", cond)

    def run_gate(self, schedule, verdict):
        script = re.sub(r"\$\{\{[^}]*\}\}", "success", step_script("ci-gate", None))
        env = {"PATH": os.environ["PATH"], "FULL_TIER": "true", "NIGHTLY": "true",
               "DRAFT": "false", "SCHEDULE": schedule, "SCHEDULE_GUARD": verdict,
               "DOCS_ONLY": "false", "ESP32_CHANGED": "true", "ESP32_PR": "false",
               "OPTEE_CHANGED": "true"}
        return subprocess.run(["bash", "-c", script], env=env, capture_output=True,
                              text=True, check=False)

    def test_the_gate_passes_on_run(self):
        p = self.run_gate("true", "run")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)

    def test_the_gate_fails_on_an_empty_or_unreadable_verdict(self):
        for verdict in ("", "unreadable", "busy", "other"):
            with self.subTest(verdict=verdict):
                p = self.run_gate("true", verdict)
                self.assertNotEqual(p.returncode, 0, p.stdout)
                self.assertIn("FAIL schedule-guard", p.stdout)

    def test_the_gate_ignores_the_verdict_off_a_schedule_run(self):
        p = self.run_gate("false", "")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)


if __name__ == "__main__":
    unittest.main()
