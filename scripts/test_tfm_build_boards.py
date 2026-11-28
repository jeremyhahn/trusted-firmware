# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/tfm-build.sh's boards (make scripts-test).

#608 slice 4: the NUCLEO-H563ZI builds TF-M v2.3.1 from the out-of-tree port
src/tz/tfm/platform/nucleo_h563zi (derived from TF-M's stm/stm32h573i_dk),
with the hardware crypto accelerator off: the H563 has no SAES and no PKA.
Each run puts stub arm-none-eabi-gcc, arm-none-eabi-size and git first on
PATH; the stub git reports HEAD as forty zeros, so a known board stops at the
pinned-commit check before any clone, venv or cmake. No network, no
toolchain.
"""

import os
import re
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("TFM_BUILD_SCRIPT", os.path.join(REPO, "scripts", "tfm-build.sh"))
PORT = os.path.join(REPO, "src", "tz", "tfm", "platform", "nucleo_h563zi")
BOARDS = ("an521", "stm32h573i-dk", "nucleo-h563zi")

GIT_STUB = """#!/bin/sh
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
    return dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])


def _run(cwd, env, board, arg):
    return subprocess.run(
        ["bash", SCRIPT, board, arg], cwd=cwd, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )


class BoardsTest(unittest.TestCase):
    def test_nucleo_h563zi_gets_past_the_board_case(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = _env(tmp)
            os.makedirs(os.path.join(tmp, "work", "tf-m", ".git"))
            proc = _run(tmp, env, "nucleo-h563zi", "work")
            self.assertNotEqual(proc.returncode, 0, proc.stderr)
            self.assertNotIn("usage", proc.stderr)
            self.assertIn("pinned", proc.stderr)

    def test_an_unknown_board_names_all_three(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = _env(tmp)
            proc = _run(tmp, env, "nucleo-h999", "work")
            self.assertNotEqual(proc.returncode, 0)
            usage = [l for l in proc.stderr.splitlines() if "usage" in l]
            self.assertTrue(usage, proc.stderr)
            for board in BOARDS:
                self.assertIn(board, usage[0])

    def test_the_port_has_the_hardware_crypto_accelerator_off(self):
        config = os.path.join(PORT, "config.cmake")
        self.assertTrue(os.path.isfile(config), config)
        with open(config) as f:
            lines = [l for l in f if re.match(r"\s*set\(\s*CRYPTO_HW_ACCELERATOR\s", l)]
        self.assertEqual(len(lines), 1, lines)
        self.assertRegex(lines[0], r"CRYPTO_HW_ACCELERATOR\s+OFF\b")


if __name__ == "__main__":
    unittest.main()
