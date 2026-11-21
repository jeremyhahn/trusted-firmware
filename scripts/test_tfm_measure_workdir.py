# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/tfm-measure.sh's work directory (make scripts-test).

The script's optional argument names the work directory every later path is
built from, CMake's toolchain file and CONFIG_SPE_PATH among them, so a
relative argument must be made absolute against the invocation directory
first (Codex P2 on #493, #640). Each run here puts stub arm-none-eabi-gcc,
arm-none-eabi-size and git first on PATH; the stub git logs its argv and
reports HEAD as forty zeros, so the script stops at the pinned-commit check
before any clone, venv or cmake. No network, no toolchain.
"""

import os
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("TFM_MEASURE_SCRIPT", os.path.join(REPO, "scripts", "tfm-measure.sh"))

GIT_STUB = """#!/bin/sh
printf '%s\\n' "$*" >>"$GIT_LOG"
for a in "$@"; do
\tif [ "$a" = rev-parse ]; then
\t\techo 0000000000000000000000000000000000000000
\tfi
done
exit 0
"""


def _stub(path, body):
    with open(path, "w") as f:
        f.write(body)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)


class WorkDirTest(unittest.TestCase):
    def run_script(self, tmp, cwd, arg, work):
        """Run the script from cwd with arg; work is the directory it names."""
        bindir = os.path.join(tmp, "bin")
        os.mkdir(bindir)
        _stub(os.path.join(bindir, "arm-none-eabi-gcc"), "#!/bin/sh\nexit 0\n")
        _stub(os.path.join(bindir, "arm-none-eabi-size"), "#!/bin/sh\nexit 0\n")
        _stub(os.path.join(bindir, "git"), GIT_STUB)
        os.makedirs(os.path.join(work, "tf-m", ".git"))
        log = os.path.join(tmp, "git.log")
        env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"], GIT_LOG=log)
        proc = subprocess.run(
            ["bash", SCRIPT, arg], cwd=cwd, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        self.assertNotEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("pinned", proc.stderr)
        self.assertTrue(os.path.isdir(os.path.join(work, "logs")))
        with open(log) as f:
            lines = f.read().splitlines()
        self.assertTrue(lines, "git was never run")
        args = lines[0].split(" ")
        self.assertEqual(args[0], "-C", lines[0])
        return args[1]

    def test_a_relative_work_dir_reaches_git_absolute(self):
        with tempfile.TemporaryDirectory() as tmp:
            got = self.run_script(tmp, tmp, "rel/work", os.path.join(tmp, "rel", "work"))
            self.assertEqual(got, os.path.join(os.path.realpath(tmp), "rel", "work", "tf-m"))

    def test_a_dotdot_work_dir_reaches_git_absolute(self):
        with tempfile.TemporaryDirectory() as tmp:
            sub = os.path.join(tmp, "sub")
            os.mkdir(sub)
            got = self.run_script(tmp, sub, "../work", os.path.join(tmp, "work"))
            self.assertEqual(got, os.path.join(os.path.realpath(tmp), "work", "tf-m"))

    def test_an_absolute_work_dir_is_kept(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = os.path.join(tmp, "abs")
            got = self.run_script(tmp, tmp, work, work)
            self.assertEqual(got, os.path.join(os.path.realpath(tmp), "abs", "tf-m"))


if __name__ == "__main__":
    unittest.main()
