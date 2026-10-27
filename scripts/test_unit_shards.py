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
    """ci.yml's unit-shard-N jobs: {N: the make command its test step runs},
    and the jobs the CI Gate needs and checks."""
    with open(WORKFLOW, encoding="utf-8") as f:
        text = f.read()
    jobs = {}
    for m in re.finditer(r"^  unit-shard-([0-9]+):\n(.*?)(?=^  [a-z])", text, re.MULTILINE | re.DOTALL):
        runs = re.findall(r'in-toolchain-image\.sh "\$TOOLCHAIN_IMAGE" "make ([a-z0-9-]+) ', m.group(2))
        jobs[int(m.group(1))] = runs
    needs = re.search(r"^  ci-gate:\n.*?^    needs: \[([^]]*)\]", text, re.MULTILINE | re.DOTALL)
    gate_needs = [n.strip() for n in needs.group(1).split(",")] if needs else []
    checks = re.findall(r'^ +check (unit-shard-[0-9]+) +"\$\{\{ needs\.(unit-shard-[0-9]+)\.result \}\}"',
                        text, re.MULTILINE)
    return jobs, gate_needs, checks


class ShardLists(unittest.TestCase):
    def test_every_gate_is_in_exactly_one_shard(self):
        gates, shards = shard_lists()
        self.assertTrue(shards, "mk/stages.mk defines no TEST_SHARD_N")
        self.assertEqual(shard_errors(gates, shards), [])

    def test_the_workflow_runs_every_shard_and_the_gate_requires_it(self):
        _, shards = shard_lists()
        jobs, gate_needs, checks = workflow_shards()
        self.assertEqual(sorted(jobs), sorted(shards),
                         "ci.yml must have one unit-shard-N job per TEST_SHARD_N in "
                         "mk/stages.mk, and no other; fix: add or delete the job")
        for n in sorted(jobs):
            job = "unit-shard-%d" % n
            with self.subTest(job=job):
                self.assertEqual(jobs[n], ["test-shard-%d" % n],
                                 "%s must run make test-shard-%d, once" % (job, n))
                self.assertIn(job, gate_needs, "fix: add %s to the CI Gate's needs" % job)
                self.assertIn((job, job), checks,
                              "fix: add a `check %s` line to the CI Gate" % job)

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
