# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/in-toolchain-image.sh (make scripts-test).

The wrapper runs every CI stage in a sibling container. Each case writes a stub
`docker` first on PATH that records every argv and runs the real script in an
empty directory with an environment built from scratch. A bad setting is
refused with exit 2 before any container is made; the CPU bound is capped at
the daemon's NCPU; the memory bound and the uid reach `docker create`; the
cache volumes follow CI_CACHE, CI_CACHE_CLASS and the network; the container
is removed on exit and the script exits with the command's status. No Docker
daemon.

IN_IMAGE_TEST_SCRIPT names another copy of the script to run instead (a
scratch mutation run); it is never set in the gate.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.environ.get("IN_IMAGE_TEST_SCRIPT") or os.path.join(HERE, "in-toolchain-image.sh")
PREFIX = "t-ci-"

# The stub docker: it appends each argv, as one JSON line, to STUB_RECORD.
# `info` prints STUB_NCPU (fails when it is `fail`), `cp` drains stdin and
# `start` exits STUB_RC.
STUB = r'''
import json, os, sys

args = sys.argv[1:]
with open(os.environ["STUB_RECORD"], "a") as f:
    f.write(json.dumps(args) + "\n")
cmd = args[0] if args else ""
if cmd == "info":
    ncpu = os.environ.get("STUB_NCPU", "")
    if ncpu == "fail":
        sys.exit(1)
    print(ncpu)
elif cmd == "cp":
    sys.stdin.buffer.read()
elif cmd == "start":
    sys.exit(int(os.environ.get("STUB_RC") or 0))
'''


def flag_values(argv, flag):
    return [argv[i + 1] for i, a in enumerate(argv[:-1]) if a == flag]


class TestInToolchainImage(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.bin = os.path.join(self.dir, "bin")
        os.mkdir(self.bin)
        stub = os.path.join(self.bin, "docker")
        with open(stub, "w") as f:
            f.write(f"#!{sys.executable} -I\n" + STUB)
        os.chmod(stub, 0o755)
        self.work = os.path.join(self.dir, "work")
        os.mkdir(self.work)
        self.record = os.path.join(self.dir, "record")

    def tearDown(self):
        self.tmp.cleanup()

    def run_script(self, *args, **env):
        if os.path.exists(self.record):
            os.remove(self.record)
        full = {
            "PATH": self.bin + ":/usr/bin:/bin",
            "STUB_RECORD": self.record,
            "CI_STACK_PREFIX": PREFIX,
            "IN_IMAGE_CPUS": "2",
            "IN_IMAGE_MEMORY": "8g",
            "CI_CACHE": "off",
        }
        full.update(env)
        if not args:
            args = ("img:tag", "make", "x")
        p = subprocess.run(
            ["bash", SCRIPT, *args], cwd=self.work, env=full, capture_output=True, text=True
        )
        calls = []
        if os.path.exists(self.record):
            with open(self.record) as f:
                calls = [json.loads(line) for line in f]
        return p, calls

    def only(self, calls, cmd):
        found = [c for c in calls if c and c[0] == cmd]
        self.assertEqual(len(found), 1, f"one `docker {cmd}` expected: {calls}")
        return found[0]

    def test_one_argument_is_usage(self):
        p, calls = self.run_script("img:tag")
        self.assertEqual(p.returncode, 2)
        self.assertIn("usage:", p.stderr)
        self.assertFalse([c for c in calls if c[0] == "create"])

    def test_bad_settings_refused_before_create(self):
        cases = [
            ("IN_IMAGE_NETWORK", "host"),
            ("IN_IMAGE_UID", "0"),
            ("IN_IMAGE_UID", "abc"),
            ("CI_CACHE", "maybe"),
            ("CI_CACHE_CLASS", "dev"),
            ("IN_IMAGE_CACHE", "rel:/c"),
            ("IN_IMAGE_CACHE_SAVE", "2"),
            ("IN_IMAGE_CPUS", "0"),
            ("IN_IMAGE_MEMORY", "16G"),
            ("IN_IMAGE_MEMORY", "0"),
        ]
        for var, value in cases:
            with self.subTest(var=var, value=value):
                p, calls = self.run_script(**{var: value})
                self.assertEqual(p.returncode, 2, p.stderr)
                self.assertIn(var, p.stderr)
                self.assertFalse([c for c in calls if c[0] in ("create", "run")], calls)

    def test_cpus_capped_at_daemon(self):
        p, calls = self.run_script(IN_IMAGE_CPUS="64", STUB_NCPU="4")
        self.assertEqual(p.returncode, 0, p.stderr)
        create = self.only(calls, "create")
        self.assertEqual(flag_values(create, "--cpus"), ["4"])
        env = flag_values(create, "--env")
        self.assertIn("GOMAXPROCS=4", env)
        self.assertIn("GOFLAGS=-p=4", env)

    def test_cpus_below_daemon_or_daemon_unknown_unchanged(self):
        for ncpu in ("4", "fail"):
            with self.subTest(ncpu=ncpu):
                p, calls = self.run_script(IN_IMAGE_CPUS="2", STUB_NCPU=ncpu)
                self.assertEqual(p.returncode, 0, p.stderr)
                create = self.only(calls, "create")
                self.assertEqual(flag_values(create, "--cpus"), ["2"])
                self.assertIn("GOMAXPROCS=2", flag_values(create, "--env"))

    def test_memory_bound_and_uid(self):
        p, calls = self.run_script(IN_IMAGE_MEMORY="16g", IN_IMAGE_UID="4242")
        self.assertEqual(p.returncode, 0, p.stderr)
        create = self.only(calls, "create")
        self.assertEqual(flag_values(create, "--memory"), ["16g"])
        self.assertEqual(flag_values(create, "--memory-swap"), ["16g"])
        self.assertEqual(flag_values(create, "--user"), ["4242:4242"])
        self.assertEqual(flag_values(create, "--network"), ["none"])

    def test_exit_status_and_container_removed(self):
        p, calls = self.run_script(STUB_RC="3")
        self.assertEqual(p.returncode, 3, p.stderr)
        create = self.only(calls, "create")
        name = flag_values(create, "--name")
        self.assertEqual(len(name), 1)
        self.assertTrue(name[0].startswith(PREFIX + "tc-"), name)
        self.assertIn(["rm", "-f", "-v", name[0]], calls)
        self.assertLess(calls.index(create), calls.index(["rm", "-f", "-v", name[0]]))

    def test_cache_on_network_none(self):
        p, calls = self.run_script(CI_CACHE="on")
        self.assertEqual(p.returncode, 0, p.stderr)
        run = self.only(calls, "run")
        create = self.only(calls, "create")
        self.assertLess(calls.index(run), calls.index(create))
        vols = flag_values(run, "-v")
        for kind in ("gobuild", "golangci", "gomod"):
            self.assertIn(f"{PREFIX}cache-pr-{kind}:/c/{kind}", vols)
        cvols = flag_values(create, "-v")
        self.assertIn(f"{PREFIX}cache-pr-gobuild:/cache/gobuild", cvols)
        self.assertIn(f"{PREFIX}cache-pr-golangci:/cache/golangci", cvols)
        self.assertIn("GOCACHE=/cache/gobuild", flag_values(create, "--env"))
        self.assertFalse([a for a in create if "cache-pr-gomod" in a], create)
        self.assertFalse([a for a in create if a.startswith("GOMODCACHE=")], create)

    def test_cache_on_bridge_main_mounts_gomod(self):
        p, calls = self.run_script(CI_CACHE="on", IN_IMAGE_NETWORK="bridge", CI_CACHE_CLASS="main")
        self.assertEqual(p.returncode, 0, p.stderr)
        create = self.only(calls, "create")
        self.assertIn(f"{PREFIX}cache-main-gomod:/cache/gomod", flag_values(create, "-v"))
        self.assertIn("GOMODCACHE=/cache/gomod", flag_values(create, "--env"))
        self.assertEqual(flag_values(create, "--network"), ["bridge"])

    def test_cache_off_bridge_modcache_in_var_tmp(self):
        p, calls = self.run_script(IN_IMAGE_NETWORK="bridge")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertFalse([c for c in calls if c[0] == "run"], calls)
        create = self.only(calls, "create")
        env = flag_values(create, "--env")
        self.assertIn("GOMODCACHE=/var/tmp/gomodcache", env)
        self.assertIn("GOCACHE=/var/tmp/gocache", env)
        self.assertFalse(flag_values(create, "-v"), create)


if __name__ == "__main__":
    unittest.main()
