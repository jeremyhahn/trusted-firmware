#!/usr/bin/env python3
"""Ruling 901's image-history check steps in .github/workflows/ci.yml (#613).

Codex round 1 on #613 found two P1s:

- the image job's docs-only path builds only the toolchain image but passed
  the esp32 and integration images to scripts/ci-image-history.sh, which
  exits 1 on an image it cannot inspect; on the docs-only path the step
  checks the toolchain image alone;
- the integration and soak jobs leased (and so checked) only an integration
  image already on the daemon: a cold daemon took no lease, skipped the
  check, and `make integration-test` then built and ran the image unchecked.
  The jobs now build the image first, the lease fails without it, and the
  check always runs on a pool runner, before the run step.

Stdlib only; `make scripts-test` discovers it.
"""

import os
import re
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
WORKFLOW = os.path.join(REPO, ".github", "workflows", "ci.yml")

CHECK = "Check the images carry no private address"
LEASE = "Lease the integration image"
RUN_STEP = {
    "integration": "make integration-test (integration image)",
    "soak": "Soak (integration suite, looped)",
}


def read_workflow():
    with open(WORKFLOW, encoding="utf-8") as f:
        return f.read()


def job_block(job):
    m = re.search(r"^  %s:\n(.*?)(?=^  [a-z]|\Z)" % re.escape(job), read_workflow(),
                  re.MULTILINE | re.DOTALL)
    if not m:
        raise AssertionError("ci.yml has no job %s" % job)
    return m.group(1)


def steps(job):
    """The job's steps as (name, text) pairs, in order; comments dropped."""
    body = job_block(job)
    body = body[body.index("    steps:\n"):]
    out = []
    for chunk in re.split(r"^      - ", body, flags=re.MULTILINE)[1:]:
        text = "      - " + chunk
        text = "".join(l for l in text.splitlines(True) if not re.match(r"^\s*#", l))
        m = re.search(r"^      - name: (.*)$|^        name: (.*)$", text, re.MULTILINE)
        name = (m.group(1) or m.group(2)).strip() if m else ""
        out.append((name, text))
    return out


def step(job, name):
    found = [t for n, t in steps(job) if n == name]
    if len(found) != 1:
        raise AssertionError("%s has %d steps named %r" % (job, len(found), name))
    return found[0]


def step_index(job, name):
    names = [n for n, _ in steps(job)]
    if names.count(name) != 1:
        raise AssertionError("%s has %d steps named %r" % (job, names.count(name), name))
    return names.index(name)


def run_script(text):
    """The step's `run:` script, block or one line, dedented."""
    r = re.search(r"^(\s+)run: \|\n((?:\1 .*\n|\s*\n)+)", text, re.MULTILINE)
    if r:
        indent = len(r.group(1)) + 2
        return "".join(l[indent:] if l.strip() else "\n" for l in r.group(2).splitlines(True))
    r = re.search(r"^\s+run: (.+)$", text, re.MULTILINE)
    if not r:
        raise AssertionError("no run script in step:\n" + text)
    return r.group(1) + "\n"


def step_if(text):
    r = re.search(r"^        if: (.*)$", text, re.MULTILINE)
    return r.group(1) if r else ""


def bash(script, cwd, env):
    """Run a step's script as GitHub Actions does (bash -eo pipefail)."""
    path = os.path.join(cwd, "step.sh")
    with open(path, "w", encoding="utf-8") as f:
        f.write(script)
    return subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", path],
                          cwd=cwd, env=env, capture_output=True, text=True, timeout=60)


def executable(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class ImageJobDocsOnly(unittest.TestCase):
    """P1: the docs-only path checks only the toolchain image it built."""

    def checked(self, docs_only):
        script = run_script(step("image", CHECK))
        with tempfile.TemporaryDirectory() as d:
            os.mkdir(os.path.join(d, "scripts"))
            log = os.path.join(d, "args.log")
            executable(os.path.join(d, "scripts", "ci-image-history.sh"),
                       '#!/usr/bin/env bash\nprintf "%%s\\n" "$@" > %s\n' % log)
            env = dict(os.environ, TOOLCHAIN_IMAGE="tf-ci-toolchain:t",
                       ESP32_IMAGE="tf-ci-esp32:e", IT_IMAGE="tf-ci-it:i")
            env.pop("DOCS_ONLY", None)
            if docs_only is not None:
                env["DOCS_ONLY"] = docs_only
            r = bash(script, d, env)
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(log, encoding="utf-8") as f:
                return f.read().split()

    def test_docs_only_checks_the_toolchain_image_alone(self):
        self.assertEqual(self.checked("true"), ["tf-ci-toolchain:t"])

    def test_code_path_checks_all_three_images(self):
        want = ["tf-ci-toolchain:t", "tf-ci-esp32:e", "tf-ci-it:i"]
        self.assertEqual(self.checked(None), want)
        self.assertEqual(self.checked("false"), want)

    def test_step_reads_docs_only_from_preflight(self):
        self.assertRegex(step("image", CHECK),
                         r"\n          DOCS_ONLY: \$\{\{ needs\.preflight\.outputs\.docs_only \}\}")


class IntegrationImageBuiltBeforeCheck(unittest.TestCase):
    """P1: a cold daemon's integration image is built, leased and checked
    before the suite runs it."""

    JOBS = ("integration", "soak")

    def build_index(self, job):
        idx = [i for i, (_, t) in enumerate(steps(job))
               if "run:" in t and "make docker-build-it" in run_script(t)]
        self.assertTrue(idx, "%s has no step running make docker-build-it" % job)
        return idx[0]

    def test_build_lease_check_run_order(self):
        for job in self.JOBS:
            with self.subTest(job=job):
                login = step_index(job, "Log in to the forge registry")
                build = self.build_index(job)
                lease = step_index(job, LEASE)
                check = step_index(job, CHECK)
                run = step_index(job, RUN_STEP[job])
                self.assertLess(step_index(job, "Name the integration image"), build)
                self.assertLess(login, build)
                self.assertLess(build, lease)
                self.assertLess(lease, check)
                self.assertLess(check, run)

    def test_no_build_after_the_check(self):
        for job in self.JOBS:
            with self.subTest(job=job):
                check = step_index(job, CHECK)
                for name, text in steps(job)[check + 1:]:
                    script = run_script(text) if "run:" in text else ""
                    self.assertNotIn("docker-build-it", script, name)
                    self.assertNotRegex(script, r"docker (image )?build|buildx", name)

    def test_run_step_runs_the_checked_id(self):
        # make integration-test's docker-build-it prerequisite then finds
        # IT_IMAGE on the daemon (reused) and runs the checked image.
        for job in self.JOBS:
            with self.subTest(job=job):
                script = run_script(step(job, RUN_STEP[job]))
                tag = script.index('docker tag "$IT_LEASE_ID" "$IT_IMAGE"')
                self.assertLess(tag, script.index("make "))

    def test_check_never_skips_on_an_empty_lease(self):
        for job in self.JOBS:
            with self.subTest(job=job):
                cond = step_if(step(job, CHECK))
                self.assertNotIn("IT_LEASE_ID != ''", cond)
                self.assertIn("runner.environment != 'github-hosted'", cond)
                self.assertIn('"$IT_LEASE_ID"', run_script(step(job, CHECK)))

    def lease(self, job, image_present):
        script = run_script(step(job, LEASE))
        with tempfile.TemporaryDirectory() as d:
            bindir = os.path.join(d, "bin")
            os.mkdir(bindir)
            executable(os.path.join(bindir, "docker"), r"""#!/usr/bin/env bash
case "$1 $2" in
  "image inspect")
    for a in "$@"; do
      if [ "$a" = "$IT_IMAGE" ]; then
        [ "$FAKE_IT_PRESENT" = 1 ] || exit 1
        echo sha256:feed
        exit 0
      fi
    done
    exit 0 ;;
  "create "*) echo "container-$RANDOM" ;;
esac
exit 0
""")
            genv = os.path.join(d, "github_env")
            open(genv, "w").close()
            env = dict(os.environ, PATH=bindir + os.pathsep + os.environ.get("PATH", ""),
                       IT_IMAGE="tf-ci-it:cold", CI_STACK_PREFIX="tf-ci-",
                       GITHUB_RUN_ID="1", GITHUB_RUN_ATTEMPT="1",
                       GITHUB_ENV=genv, RUNNER_TEMP=d,
                       FAKE_IT_PRESENT="1" if image_present else "0")
            r = bash(script, d, env)
            with open(genv, encoding="utf-8") as f:
                return r, f.read()

    def test_lease_fails_without_the_image(self):
        for job in self.JOBS:
            with self.subTest(job=job):
                r, genv = self.lease(job, image_present=False)
                self.assertNotEqual(r.returncode, 0, "lease took no lease and passed")
                self.assertIn("::error::", r.stdout + r.stderr)
                self.assertNotIn("IT_LEASE_ID=", genv)

    def test_lease_records_the_image_id(self):
        for job in self.JOBS:
            with self.subTest(job=job):
                r, genv = self.lease(job, image_present=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertIn("IT_LEASE_ID=sha256:feed\n", genv)


if __name__ == "__main__":
    unittest.main()
