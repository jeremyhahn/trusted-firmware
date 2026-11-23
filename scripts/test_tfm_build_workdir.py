# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/tfm-build.sh's work directory (make scripts-test).

The script's second argument names the work directory every later path is
built from, CMake's TFM_TOOLCHAIN_FILE and CONFIG_SPE_PATH among them, so a
relative argument must be made absolute against the invocation directory
first (Codex P1 on #900, #608): `make tfm-image TFM_WORK=../tfm` must
configure. Each run here puts stub arm-none-eabi-gcc, arm-none-eabi-size and
git first on PATH; the stub git logs its argv and reports HEAD as forty
zeros, so the script stops at the pinned-commit check before any clone, venv
or cmake. No network, no toolchain.
"""

import os
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("TFM_BUILD_SCRIPT", os.path.join(REPO, "scripts", "tfm-build.sh"))

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


def _env(tmp):
    bindir = os.path.join(tmp, "bin")
    os.mkdir(bindir)
    _stub(os.path.join(bindir, "arm-none-eabi-gcc"), "#!/bin/sh\nexit 0\n")
    _stub(os.path.join(bindir, "arm-none-eabi-size"), "#!/bin/sh\nexit 0\n")
    _stub(os.path.join(bindir, "git"), GIT_STUB)
    log = os.path.join(tmp, "git.log")
    return dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"], GIT_LOG=log), log


def _run(cwd, env, board, arg):
    return subprocess.run(
        ["bash", SCRIPT, board, arg], cwd=cwd, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )


class WorkDirTest(unittest.TestCase):
    def run_script(self, tmp, cwd, arg, work):
        """Run the script for an521 from cwd with arg; work is the directory it names."""
        env, log = _env(tmp)
        os.makedirs(os.path.join(work, "tf-m", ".git"))
        proc = _run(cwd, env, "an521", arg)
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

    def test_an_unknown_board_creates_no_work_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            env, _ = _env(tmp)
            proc = _run(tmp, env, "no-such-board", "work")
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("usage", proc.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, "work")))


if __name__ == "__main__":
    unittest.main()
