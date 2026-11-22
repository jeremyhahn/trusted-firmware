# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/reap-stale-ci-stacks.sh (make scripts-test).

The reaper is the first step of every integration job: it removes this repo's
dead CI leftovers and must never remove what a running job still needs. Each
case writes a fixture of containers, images, networks and volumes and a stub
`docker` first on PATH that answers from it and records every argv, then runs
the real script. A compose project goes only when every container is stale, a
prefixed `it` image only under the image lock, when neither created nor tagged
within REAP_IMAGE_MIN_AGE and with no container on its id. No Docker daemon.

REAP_TEST_SCRIPT names another copy of the script to run instead (a scratch
mutation run); it is never set in the gate.
"""

import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.environ.get("REAP_TEST_SCRIPT") or os.path.join(HERE, "reap-stale-ci-stacks.sh")
LOCK_ID = "lockcontainer0"
NEVER_TAGGED = "0001-01-01 00:00:00 +0000 UTC"

# The stub docker: it answers the calls the script makes from STUB_FIXTURE and
# appends each argv, as one JSON line, to STUB_RECORD.
STUB = r'''
import json, os, sys

NEVER_TAGGED = "''' + NEVER_TAGGED + r'''"
args = sys.argv[1:]
with open(os.environ["STUB_RECORD"], "a") as f:
    f.write(json.dumps(args) + "\n")
with open(os.environ["STUB_FIXTURE"]) as f:
    fx = json.load(f)
cons = fx.get("containers", [])
imgs = fx.get("images", [])


def values(flag):
    return [args[i + 1] for i, a in enumerate(args[:-1]) if a == flag]


def fmt():
    v = values("-f") or values("--format")
    return v[0] if v else ""


def container(key):
    for c in cons:
        if key in (c["id"], c["name"]):
            return c
    return None


def image(ref):
    for i in imgs:
        if ref in (i["ref"], i["id"]):
            return i
    return None


def ps():
    quiet = "-q" in args or "-aq" in args
    rows = cons
    for flt in values("--filter"):
        if flt == "label=com.docker.compose.project":
            rows = [c for c in rows if c.get("project")]
        elif flt.startswith("label=com.docker.compose.project="):
            want = flt.split("=", 2)[2]
            rows = [c for c in rows if c.get("project") == want]
        elif flt.startswith("name=^"):
            rows = [c for c in rows if c["name"].startswith(flt[len("name=^"):])]
        elif flt.startswith("ancestor="):
            rows = [c for c in rows if c.get("image") == flt[len("ancestor="):]]
        else:
            sys.exit("stub: ps filter " + flt)
    for c in rows:
        if quiet:
            print(c["id"])
        elif fmt() == '{{.Label "com.docker.compose.project"}}':
            print(c["project"])
        elif fmt().startswith("{{.ID}} {{.Names}}"):
            print(c["id"], c["name"], c.get("project", ""))
        else:
            sys.exit("stub: ps format " + fmt())


def inspect(what):
    if what is None:
        sys.exit(1)
    out = fmt()
    for field, key in (("{{.State.Status}}", "state"), ("{{.State.FinishedAt}}", "finished"),
                       ("{{.Created}}", "created"), ("{{.Id}}", "id"),
                       ("{{.Metadata.LastTagTime}}", "tagged")):
        out = out.replace(field, what.get(key, NEVER_TAGGED if key == "tagged" else ""))
    print(out)


cmd = args[:2]
if args[0] == "ps":
    ps()
elif args[0] == "inspect":
    inspect(container(args[-1]))
elif cmd == ["image", "inspect"]:
    if not fmt():
        sys.exit(1)
    inspect(image(args[-1]))
elif args[0] == "images":
    repo = values("--filter")[0][len("reference="):]
    for i in imgs:
        if i["ref"].split(":")[0] == repo:
            print(i["ref"])
elif cmd in (["network", "ls"], ["volume", "ls"]):
    want = values("--filter")[0].split("=", 2)[2]
    for name in fx.get(args[0] + "s", {}).get(want, []):
        print(name)
elif args[0] == "create":
    if not fx.get("lock_free", True):
        sys.exit("stub: the lock name is in use")
    print("''' + LOCK_ID + r'''")
elif args[0] == "import":
    sys.stdin.read()
elif args[0] == "rm" or cmd in (["network", "rm"], ["volume", "rm"], ["image", "rm"]):
    pass
else:
    sys.exit("stub: no answer for " + " ".join(args))
'''


def ago(seconds):
    """An RFC 3339 time `seconds` before now, as docker inspect prints it."""
    return time.strftime("%Y-%m-%dT%H:%M:%S.123456789Z", time.gmtime(time.time() - seconds))


def tag_ago(seconds):
    """A Metadata.LastTagTime `seconds` before now, in Go's time format."""
    return time.strftime("%Y-%m-%d %H:%M:%S.26 +0000 UTC", time.gmtime(time.time() - seconds))


HOUR = 3600


def exited(cid, name, project=None, hours=4.0, image=None):
    c = {"id": cid, "name": name, "state": "exited", "finished": ago(hours * HOUR),
         "created": ago(hours * HOUR + 60)}
    if project:
        c["project"] = project
    if image:
        c["image"] = image
    return c


STACK = {
    "containers": [exited("c1", "tf-x-1-app-1", "tf-x-1"), exited("c2", "tf-x-1-db-1", "tf-x-1")],
    "networks": {"tf-x-1": ["n1"]},
    "volumes": {"tf-x-1": ["v1"]},
}
IMAGE = {"ref": "tf-xit:aaaa", "id": "sha256:aaaa", "created": ago(3 * HOUR)}


class TestReaper(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.bin = os.path.join(self.dir, "bin")
        os.mkdir(self.bin)
        stub = os.path.join(self.bin, "docker")
        with open(stub, "w") as f:
            f.write(f"#!{sys.executable} -I\n" + STUB)
        os.chmod(stub, 0o755)
        self.record = os.path.join(self.dir, "record")

    def tearDown(self):
        self.tmp.cleanup()

    def reap(self, fixture, *args, **env):
        path = os.path.join(self.dir, "fixture.json")
        with open(path, "w") as f:
            json.dump(fixture, f)
        run_env = dict(os.environ, PATH=self.bin + os.pathsep + os.environ.get("PATH", ""),
                       STUB_FIXTURE=path, STUB_RECORD=self.record, **env)
        for k in ("REAP_DRY_RUN", "REAP_IMAGE_MIN_AGE", "REAP_LOCK_WAIT"):
            if k not in env:
                run_env.pop(k, None)
        got = subprocess.run(["bash", SCRIPT, *(args or ("tf-x",))], env=run_env,
                             capture_output=True, text=True, timeout=60)
        self.assertNotIn("stub:", got.stderr)
        return got

    def calls(self):
        if not os.path.exists(self.record):
            return []
        with open(self.record) as f:
            return [json.loads(line) for line in f]

    def removals(self):
        """Every removal but the lock container's own."""
        return [c for c in self.calls()
                if (c[0] == "rm" and c != ["rm", LOCK_ID]) or c[:2] in (
                    ["network", "rm"], ["volume", "rm"], ["image", "rm"])]

    def assert_reaped(self, got, projects, images, done_as="removed"):
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertIn(f"reaper: {projects} compose project(s) and {images} image(s) {done_as} "
                      "for prefix 'tf-x'", got.stdout)
        # Every call the script made reached the stub, none a daemon.
        self.assertIn(["ps", "-a", "--filter", "label=com.docker.compose.project", "--format",
                       '{{.Label "com.docker.compose.project"}}'], self.calls())

    def test_an_all_stale_prefixed_stack_is_removed_with_its_network_and_volume(self):
        got = self.reap(STACK)
        self.assert_reaped(got, 1, 0)
        self.assertEqual(self.removals(), [["rm", "-v", "c1", "c2"], ["network", "rm", "n1"],
                                           ["volume", "rm", "v1"]])

    def test_a_stack_with_one_running_container_is_kept(self):
        fixture = json.loads(json.dumps(STACK))
        fixture["containers"][1].update(state="running", finished="0001-01-01T00:00:00Z")
        got = self.reap(fixture)
        self.assert_reaped(got, 0, 0)
        self.assertIn("keep  tf-x-1 (a container is live or stopped within 11100s)", got.stdout)
        self.assertEqual(self.removals(), [])

    def test_a_stale_stack_without_the_prefix_is_untouched(self):
        fixture = {"containers": [exited("o1", "other-y-app-1", "other-y")],
                   "networks": {"other-y": ["on"]}, "volumes": {"other-y": ["ov"]}}
        got = self.reap(fixture)
        self.assert_reaped(got, 0, 0)
        self.assertEqual(self.removals(), [])

    def test_a_stale_lone_container_goes_and_a_fresh_one_stays(self):
        fresh = {"id": "l2", "name": "tf-xlease-2", "state": "created",
                 "finished": "0001-01-01T00:00:00Z", "created": ago(60)}
        got = self.reap({"containers": [exited("l1", "tf-xlease-1"), fresh]})
        self.assert_reaped(got, 0, 0)
        self.assertIn("reap  container tf-xlease-1", got.stdout)
        self.assertEqual(self.removals(), [["rm", "-v", "l1"]])

    def test_an_old_unused_image_is_removed_under_the_lock(self):
        got = self.reap({"images": [IMAGE]})
        self.assert_reaped(got, 0, 1)
        calls = self.calls()
        self.assertEqual(self.removals(), [["image", "rm", "tf-xit:aaaa"]])
        create = next(i for i, c in enumerate(calls) if c[0] == "create")
        self.assertIn("tf-ximage-lock", calls[create])
        self.assertLess(create, calls.index(["image", "rm", "tf-xit:aaaa"]))
        self.assertEqual(calls[-1], ["rm", LOCK_ID])

    def test_an_image_tagged_recently_is_kept(self):
        got = self.reap({"images": [dict(IMAGE, tagged=tag_ago(5 * 60))]})
        self.assert_reaped(got, 0, 0)
        self.assertIn("keep  image tf-xit:aaaa (created or tagged within 7200s)", got.stdout)
        self.assertEqual(self.removals(), [])

    def test_an_image_with_a_container_on_its_id_is_kept(self):
        lease = exited("u1", "tf-xlease-9", hours=0.01, image=IMAGE["id"])
        got = self.reap({"containers": [lease], "images": [IMAGE]})
        self.assert_reaped(got, 0, 0)
        self.assertIn("keep  image tf-xit:aaaa (in use: 1 container(s) on sha256:aaaa)",
                      got.stdout)
        self.assertEqual(self.removals(), [])

    def test_no_image_is_removed_without_the_lock(self):
        got = self.reap({"images": [IMAGE], "lock_free": False}, REAP_LOCK_WAIT="0")
        self.assert_reaped(got, 0, 0)
        self.assertIn("keep  every tf-xit image (the image lock was not free within 0s)",
                      got.stdout)
        self.assertEqual(self.removals(), [])
        self.assertNotIn("images", [c[0] for c in self.calls()])

    def test_a_dry_run_removes_nothing_and_takes_no_lock(self):
        got = self.reap(dict(STACK, images=[IMAGE]), REAP_DRY_RUN="1")
        self.assert_reaped(got, 1, 1, "would be removed")
        self.assertIn("dry   docker rm -v c1 c2", got.stdout)
        self.assertIn("dry   docker image rm tf-xit:aaaa", got.stdout)
        self.assertEqual(self.removals(), [])
        self.assertEqual([c for c in self.calls() if c[0] in ("create", "import", "rm")], [])

    def test_a_bad_argument_exits_2_and_calls_no_docker(self):
        for args, env in (((("bad/x",)), {}), (("tf-x", "1h"), {}),
                          (("tf-x",), {"REAP_IMAGE_MIN_AGE": "x"})):
            with self.subTest(args=args, env=env):
                got = self.reap({}, *args, **env)
                self.assertEqual(got.returncode, 2, got.stdout)
                self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
