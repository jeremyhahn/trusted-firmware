# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for the Unit shards, make test-shard-N (make scripts-test).

CI runs the test stage as one Unit job per shard (.github/workflows/ci.yml),
each `make test-shard-N`, and the shard lists live once, in mk/stages.mk. These
tests hold the lists to TEST_GATES: every gate in exactly one shard, no gate
TEST_GATES does not name, and every shard in the workflow's matrix, so the
shards together are `make test`. They read what make decides, without building,
and drive test-shard-N with MAKE=true and MAKE=false to pin its `make -k`
behaviour: every gate runs, one PASS or FAIL line each, and the shard fails if
any gate did.
"""

import os
import re
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
WORKFLOW = os.path.join(REPO, ".github", "workflows", "ci.yml")


def make(*args):
    """Run make in the repository, quietly; return (rc, stdout + stderr)."""
    p = subprocess.run(
        ["make", "-s", "--no-print-directory", *args],
        cwd=REPO, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


def shard_lists(*overrides):
    """make test-shards-list: (TEST_GATES, {shard number: [gates]})."""
    rc, out = make("test-shards-list", *overrides)
    if rc != 0:
        raise AssertionError(out)
    gates, shards = None, {}
    for line in out.splitlines():
        name, _, value = line.partition(" ")
        if name == "TEST_GATES":
            gates = value.split()
        elif name.startswith("TEST_SHARD_"):
            shards[int(name[len("TEST_SHARD_"):])] = value.split()
    if gates is None:
        raise AssertionError("make test-shards-list printed no TEST_GATES line:\n" + out)
    return gates, shards


def shard_errors(gates, shards):
    """Each way the shards are not TEST_GATES, as a sentence naming the fix."""
    errors = []
    seen = {}
    for n in sorted(shards):
        for g in shards[n]:
            if g not in gates:
                errors.append("shard %d runs %s, which is not in TEST_GATES; fix: delete it "
                              "from shard %d, or add it to TEST_GATES if it is a new gate"
                              % (n, g, n))
            elif g in seen:
                errors.append("runs %s in shard %d and again in shard %d; fix: delete it "
                              "from shard %d" % (g, seen[g], n, n))
            else:
                seen[g] = n
    for g in gates:
        if g not in seen:
            errors.append("%s is in TEST_GATES and in no shard, so CI never runs it; fix: "
                          "add it to one TEST_SHARD_N in mk/stages.mk" % g)
    return errors


def workflow_shards():
    """ci.yml's unit job: {N: the make commands of its test-shard-N steps},
    and the jobs the CI Gate needs and checks."""
    with open(WORKFLOW, encoding="utf-8") as f:
        text = f.read()
    jobs = {}
    m = re.search(r"^  unit:\n(.*?)(?=^  [a-z])", text, re.MULTILINE | re.DOTALL)
    if m:
        for run in re.findall(r'in-toolchain-image\.sh "\$TOOLCHAIN_IMAGE" "make (test-shard-([0-9]+)) ', m.group(1)):
            jobs.setdefault(int(run[1]), []).append(run[0])
    needs = re.search(r"^  ci-gate:\n.*?^    needs: \[([^]]*)\]", text, re.MULTILINE | re.DOTALL)
    gate_needs = [n.strip() for n in needs.group(1).split(",")] if needs else []
    checks = re.findall(r'^ +check (unit) +"\$\{\{ needs\.(unit)\.result \}\}"',
                        text, re.MULTILINE)
    return jobs, gate_needs, checks


def unit_job():
    """ci.yml's unit job without comment lines: (the job's own keys, [each step's text])."""
    with open(WORKFLOW, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"^  unit:\n(.*?)(?=^  [a-z])", text, re.MULTILINE | re.DOTALL)
    if not m:
        raise AssertionError("ci.yml has no unit job")
    body = "\n".join(l for l in m.group(1).splitlines() if not l.lstrip().startswith("#"))
    head, _, steps = body.partition("\n    steps:\n")
    return head, re.split(r"(?m)^(?=      - )", steps)[1:]


def step_if(step):
    """A step's `if:` expression, or "" when it has none."""
    m = re.search(r"(?m)^        if: (.*)$", step)
    return m.group(1) if m else ""


class ShardLists(unittest.TestCase):
    def test_every_gate_is_in_exactly_one_shard(self):
        gates, shards = shard_lists()
        self.assertTrue(shards, "mk/stages.mk defines no TEST_SHARD_N")
        self.assertEqual(shard_errors(gates, shards), [])

    def test_the_workflow_runs_every_shard_and_the_gate_requires_it(self):
        _, shards = shard_lists()
        jobs, gate_needs, checks = workflow_shards()
        self.assertEqual(sorted(jobs), sorted(shards),
                         "ci.yml's unit job must have one make test-shard-N step per "
                         "TEST_SHARD_N in mk/stages.mk, and no other; fix: add or delete the step")
        for n in sorted(jobs):
            with self.subTest(shard=n):
                self.assertEqual(jobs[n], ["test-shard-%d" % n],
                                 "the unit job must run make test-shard-%d, once" % n)
        self.assertIn("unit", gate_needs, "fix: add unit to the CI Gate's needs")
        self.assertIn(("unit", "unit"), checks, "fix: add a `check unit` line to the CI Gate")

    def test_the_unit_job_runs_the_race_suite_every_shard_and_fails_on_any(self):
        head, steps = unit_job()
        race = [s for s in steps
                if re.search(r"(?m)^ +cd sdk/go$", s) and "go test -race " in s
                and "in-toolchain-image.sh" in s]
        self.assertEqual(len(race), 1,
                         "the unit job must have one step that runs sdk/go's suite with "
                         "go test -race in the toolchain image, or the gate goes green "
                         "without the simulator race suite; fix: restore the sdk/go -race step")
        first = next((i for i, s in enumerate(steps) if "make test-shard-" in s), None)
        self.assertIsNotNone(first, "the unit job runs no make test-shard-N step")
        for s in steps[first + 1:]:
            name = s.splitlines()[0].strip()
            with self.subTest(step=name):
                cond = step_if(s)
                self.assertTrue("!cancelled()" in cond or "always()" in cond,
                                "%s has `if: %s`: a failed earlier shard would skip it, so "
                                "one run no longer reports every shard; fix: start its "
                                "`if:` with !cancelled()" % (name, cond))
        for where, text in [("the unit job", head)] + [(s.splitlines()[0].strip(), s) for s in steps]:
            with self.subTest(continue_on_error=where):
                self.assertNotIn("continue-on-error", text,
                                 "%s sets continue-on-error, so its failure no longer fails "
                                 "the unit job and the CI Gate; fix: delete it" % where)
        _, gate_needs, _ = workflow_shards()
        self.assertIn("unit", gate_needs,
                      "the CI Gate no longer needs unit, so a red shard or race suite "
                      "cannot fail it; fix: add unit to the CI Gate's needs")

    def test_each_shard_and_the_race_suite_is_its_own_matrix_leg(self):
        # CA ruling 580 follow-up: run one after another the four took up to
        # 110 min against the job's timeout; each leg runs one of them.
        head, steps = unit_job()
        _, shards = shard_lists()
        m = re.search(r"(?m)^        leg: \[([^]]*)\]$", head)
        self.assertIsNotNone(m, "the unit job has no matrix of legs; fix: restore "
                             "strategy.matrix.leg")
        legs = [l.strip() for l in m.group(1).split(",")]
        self.assertEqual(sorted(legs), sorted(["shard-%d" % n for n in shards] + ["race"]),
                         "the matrix must have one leg per TEST_SHARD_N and one race leg")
        self.assertRegex(head, r"(?m)^      fail-fast: false$",
                         "fail-fast cancels the other legs on the first red one, so a "
                         "run no longer reports every shard; fix: fail-fast: false")
        t = re.search(r"(?m)^    timeout-minutes: ([0-9]+)$", head)
        self.assertTrue(t and int(t.group(1)) <= 60,
                        "a leg runs one shard, so its timeout is at most 60 min")
        for leg in legs:
            run = ('"make test-%s ' % leg) if leg != "race" else "go test -race "
            mine = [s for s in steps if run in s]
            with self.subTest(leg=leg):
                self.assertEqual(len(mine), 1, "one step runs leg %s" % leg)
                self.assertIn("matrix.leg == '%s'" % leg, step_if(mine[0]),
                              "the %s step must run on its own leg only" % leg)

    def test_a_gate_in_two_shards_is_named_with_its_fix(self):
        gates, shards = shard_lists("TEST_SHARD_3=test-asan-host coverage")
        self.assertIn("runs coverage in shard 2 and again in shard 3; fix: delete it "
                      "from shard 3", shard_errors(gates, shards))

    def test_a_gate_in_no_shard_is_named_with_its_fix(self):
        gates, shards = shard_lists("TEST_SHARD_3=")
        self.assertIn("test-asan-host is in TEST_GATES and in no shard, so CI never runs it; "
                      "fix: add it to one TEST_SHARD_N in mk/stages.mk",
                      shard_errors(gates, shards))

    def test_an_unknown_gate_is_named_with_its_fix(self):
        gates, shards = shard_lists("TEST_SHARD_3=test-asan-host no-such-gate")
        self.assertIn("shard 3 runs no-such-gate, which is not in TEST_GATES; fix: delete "
                      "it from shard 3, or add it to TEST_GATES if it is a new gate",
                      shard_errors(gates, shards))


class ShardTarget(unittest.TestCase):
    # -o test-tools: the tool check is the stage's, not what these tests pin.
    def test_a_green_shard_prints_one_pass_per_gate(self):
        rc, out = make("-o", "test-tools", "test-shard-1", "MAKE=true")
        self.assertEqual(rc, 0, out)
        _, shards = shard_lists()
        for g in shards[1]:
            self.assertRegex(out, r"(?m)^test-shard-1: PASS %s [0-9]+s$" % re.escape(g))
        self.assertNotIn("FAIL", out)

    def test_a_failing_gate_does_not_stop_the_shard(self):
        rc, out = make("-o", "test-tools", "test-shard-2", "MAKE=false")
        self.assertNotEqual(rc, 0, out)
        _, shards = shard_lists()
        self.assertGreater(len(shards[2]), 1, "pick a shard of more than one gate")
        for g in shards[2]:
            self.assertRegex(out, r"(?m)^test-shard-2: FAIL %s [0-9]+s$" % re.escape(g))
        self.assertNotIn("green", out)

    def test_an_unknown_shard_is_refused(self):
        rc, out = make("-o", "test-tools", "test-shard-99", "MAKE=true")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("test-shard-99: no such shard", out)


if __name__ == "__main__":
    unittest.main()
