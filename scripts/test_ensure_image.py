# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/ensure-image.sh, the toolchain image's front door.

Before every CI job that needs a devcontainer image, ensure-image.sh decides
whether to build it: the image on the daemon wins, then a trusted image on the
daemon (tagged as the image), then with IMAGE_PULL=1 the registry (the image,
then the trusted one), and `make <target>` last; with IMAGE_PUSH=1 a built
image and its cache tag are pushed, and a refused push only warns. These
tests copy the real script into a temporary directory beside a fake
docker-tag.sh, put a fake docker and a fake make first on PATH (no daemon, no
build), and hold the script to that order through the one log every fake
writes its argv to.
"""

import os
import shutil
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
# ENSURE_IMAGE_SCRIPT points the suite at a scratch copy, for a mutation run
# only; CI and `make scripts-test` test the script in the repo.
SCRIPT = os.environ.get("ENSURE_IMAGE_SCRIPT") or os.path.join(HERE, "ensure-image.sh")

IMAGE = "tf-toolchain:h-abc"
TRUSTED = "registry.invalid/main/tf-toolchain:h-abc"
CACHE = "registry.invalid/pr/tf-toolchain:cache"
TARGET = "docker-build-toolchain"

# The fake docker: `image inspect REF` finds REF when it is in $FAKE_HAVE,
# `pull -q REF` gets it when it is in $FAKE_PULL, `push -q REF` is accepted
# when it is in $FAKE_PUSH (each a space-separated list). Every call is logged.
FAKE_DOCKER = r"""#!/usr/bin/env bash
echo "docker $*" >> "$FAKE_LOG"
listed() { local r; for r in $2; do [ "$r" = "$1" ] && return 0; done; return 1; }
case "$1 $2" in
  "image inspect") listed "$3" "${FAKE_HAVE:-}" ;;
  "pull -q") listed "$3" "${FAKE_PULL:-}" ;;
  "push -q") listed "$3" "${FAKE_PUSH:-}" ;;
  *) exit 99 ;;
esac
"""

FAKE_MAKE = r"""#!/usr/bin/env bash
echo "make $*" >> "$FAKE_LOG"
exit "${FAKE_MAKE_RC:-0}"
"""

FAKE_TAG = r"""#!/usr/bin/env bash
echo "docker-tag.sh $*" >> "$FAKE_LOG"
"""


def _write_exec(path, body):
    with open(path, "w", encoding="utf-8") as f:
        f.write(body)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class EnsureImageTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = self._tmp.name
        self.bin = os.path.join(root, "bin")
        self.scripts = os.path.join(root, "scripts")
        os.mkdir(self.bin)
        os.mkdir(self.scripts)
        self.log = os.path.join(root, "calls.log")
        # Read fresh on every case, so the copy is the script as it is now.
        self.script = os.path.join(self.scripts, "ensure-image.sh")
        shutil.copyfile(SCRIPT, self.script)
        _write_exec(os.path.join(self.scripts, "docker-tag.sh"), FAKE_TAG)
        _write_exec(os.path.join(self.bin, "docker"), FAKE_DOCKER)
        _write_exec(os.path.join(self.bin, "make"), FAKE_MAKE)

    def run_script(self, *args, have=(), pull=(), push=(), make_rc=0, **env):
        clean = {
            k: v
            for k, v in os.environ.items()
            if k not in ("IMAGE_PULL", "IMAGE_PUSH", "IMAGE_CACHE_REF")
            and not k.startswith("FAKE_")
        }
        clean.update(
            PATH=self.bin + os.pathsep + os.environ.get("PATH", "/usr/bin:/bin"),
            FAKE_LOG=self.log,
            FAKE_HAVE=" ".join(have),
            FAKE_PULL=" ".join(pull),
            FAKE_PUSH=" ".join(push),
            FAKE_MAKE_RC=str(make_rc),
        )
        clean.update(env)
        proc = subprocess.run(
            ["bash", self.script, *args],
            env=clean,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        return proc, self.calls()

    def calls(self):
        if not os.path.exists(self.log):
            return []
        with open(self.log, encoding="utf-8") as f:
            return f.read().splitlines()

    def test_usage_without_target_and_image(self):
        for args in ((), (TARGET,)):
            with self.subTest(args=args):
                proc, calls = self.run_script(*args)
                self.assertEqual(proc.returncode, 2, proc.stderr)
                self.assertIn("usage:", proc.stderr)
                self.assertEqual(calls, [])

    def test_image_on_daemon_is_used_and_nothing_built(self):
        proc, calls = self.run_script(
            TARGET, IMAGE, TRUSTED, have=(IMAGE, TRUSTED), pull=(IMAGE, TRUSTED), IMAGE_PULL="1"
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(calls, [f"docker image inspect {IMAGE}"])
        self.assertIn("on the daemon, not built", proc.stdout)

    def test_trusted_on_daemon_is_tagged_as_image(self):
        proc, calls = self.run_script(TARGET, IMAGE, TRUSTED, have=(TRUSTED,))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(
            calls,
            [
                f"docker image inspect {IMAGE}",
                f"docker image inspect {TRUSTED}",
                f"docker-tag.sh {TRUSTED} {IMAGE}",
            ],
        )

    def test_no_pull_without_image_pull(self):
        proc, calls = self.run_script(TARGET, IMAGE, TRUSTED, pull=(IMAGE, TRUSTED))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(
            calls,
            [
                f"docker image inspect {IMAGE}",
                f"docker image inspect {TRUSTED}",
                f"make --no-print-directory {TARGET}",
            ],
        )

    def test_pull_image_from_registry(self):
        proc, calls = self.run_script(TARGET, IMAGE, TRUSTED, pull=(IMAGE, TRUSTED), IMAGE_PULL="1")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(
            calls,
            [
                f"docker image inspect {IMAGE}",
                f"docker image inspect {TRUSTED}",
                f"docker pull -q {IMAGE}",
            ],
        )

    def test_pull_falls_back_to_trusted(self):
        proc, calls = self.run_script(TARGET, IMAGE, TRUSTED, pull=(TRUSTED,), IMAGE_PULL="1")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(
            calls,
            [
                f"docker image inspect {IMAGE}",
                f"docker image inspect {TRUSTED}",
                f"docker pull -q {IMAGE}",
                f"docker pull -q {TRUSTED}",
                f"docker-tag.sh {TRUSTED} {IMAGE}",
            ],
        )

    def test_pull_misses_then_builds(self):
        proc, calls = self.run_script(TARGET, IMAGE, TRUSTED, IMAGE_PULL="1")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(
            calls,
            [
                f"docker image inspect {IMAGE}",
                f"docker image inspect {TRUSTED}",
                f"docker pull -q {IMAGE}",
                f"docker pull -q {TRUSTED}",
                f"make --no-print-directory {TARGET}",
            ],
        )

    def test_push_after_build_image_then_cache(self):
        proc, calls = self.run_script(
            TARGET, IMAGE, push=(IMAGE, CACHE), IMAGE_PUSH="1", IMAGE_CACHE_REF=CACHE
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(
            calls,
            [
                f"docker image inspect {IMAGE}",
                f"make --no-print-directory {TARGET}",
                f"docker push -q {IMAGE}",
                f"docker-tag.sh {IMAGE} {CACHE}",
                f"docker push -q {CACHE}",
            ],
        )
        self.assertNotIn("warn", proc.stdout)

    def test_refused_pushes_only_warn(self):
        proc, calls = self.run_script(TARGET, IMAGE, IMAGE_PUSH="1", IMAGE_CACHE_REF=CACHE)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(
            [c for c in calls if c.startswith("docker push")],
            [f"docker push -q {IMAGE}", f"docker push -q {CACHE}"],
        )
        for ref in (IMAGE, CACHE):
            self.assertIn(f"warn  push of {ref} refused", proc.stdout)

    def test_failed_build_fails_and_pushes_nothing(self):
        proc, calls = self.run_script(
            TARGET, IMAGE, push=(IMAGE, CACHE), make_rc=3, IMAGE_PUSH="1", IMAGE_CACHE_REF=CACHE
        )
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual(
            calls, [f"docker image inspect {IMAGE}", f"make --no-print-directory {TARGET}"]
        )


if __name__ == "__main__":
    unittest.main()
