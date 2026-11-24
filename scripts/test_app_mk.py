# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# mk/app.mk's APP_DIR gate, the refusals an out-of-tree application meets
# before anything is compiled. app-image only echoes a path, so make parses the
# Makefile and stops: those tests need no compiler and build nothing.
#
# And that changing APP_COMMANDS relinks: AppCommandsRelinkTest builds tfwsim
# with an application in a temporary build directory, so it needs cc.

import os
import shutil
import subprocess
import tempfile
import unittest

_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_MAKE = shutil.which("make")


@unittest.skipIf(_MAKE is None, "no make")
class AppMkGateTest(unittest.TestCase):
    def _make(self, app_mk, sources=(), write_app_mk=True):
        """Writes app_mk (and each source) into a fresh directory and runs
        make app-image with APP_DIR set to it: (returncode, stdout + stderr)."""
        with tempfile.TemporaryDirectory() as d:
            if write_app_mk:
                with open(os.path.join(d, "app.mk"), "w") as f:
                    f.write(app_mk)
            for src in sources:
                path = os.path.join(d, src)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as f:
                    f.write("void app_main(void) {}\n")
            env = dict(os.environ)
            for name in ("MAKEFLAGS", "MAKELEVEL", "MFLAGS", "APP_DIR"):
                env.pop(name, None)
            p = subprocess.run(
                [_MAKE, "-s", "--no-print-directory", "-C", _REPO,
                 "app-image", "APP_DIR=" + d],
                env=env, capture_output=True, text=True, timeout=60)
            return p.returncode, p.stdout + p.stderr

    def _refused(self, app_mk, message, sources=(), write_app_mk=True):
        rc, out = self._make(app_mk, sources, write_app_mk)
        self.assertNotEqual(rc, 0, out)
        self.assertIn(message, out)

    def test_valid_app_accepted(self):
        rc, out = self._make("APP_NAME := ok\nAPP_SRCS := a.c\n", ["a.c"])
        self.assertEqual(rc, 0, out)
        self.assertIn("app-ok", out)

    def test_nested_src_accepted(self):
        rc, out = self._make(
            "APP_NAME := ok\nAPP_SRCS := sub/a.c\n", ["sub/a.c"])
        self.assertEqual(rc, 0, out)
        self.assertIn("app-ok", out)

    def test_dotted_names_accepted(self):
        # Only a ".." path component climbs; a name with two dots does not.
        for src in ("a..b.c", "sub/..x/a.c"):
            with self.subTest(src=src):
                rc, out = self._make(
                    "APP_NAME := ok\nAPP_SRCS := %s\n" % src, [src])
                self.assertEqual(rc, 0, out)
                self.assertIn("app-ok", out)

    def test_fifteen_character_name_accepted(self):
        rc, out = self._make(
            "APP_NAME := abcdefghijklmno\nAPP_SRCS := a.c\n", ["a.c"])
        self.assertEqual(rc, 0, out)
        self.assertIn("app-abcdefghijklmno", out)

    def test_bad_names_refused(self):
        for name in ("abcdefghijklmnop", "Bad", "-ab"):
            with self.subTest(name=name):
                self._refused(
                    "APP_NAME := %s\nAPP_SRCS := a.c\n" % name,
                    "at most 15 characters", ["a.c"])

    def test_no_app_mk_refused(self):
        self._refused("", "has no app.mk", write_app_mk=False)

    def test_empty_srcs_refused(self):
        self._refused("APP_NAME := ok\nAPP_SRCS :=\n", "APP_SRCS is empty")

    def test_escaping_srcs_refused(self):
        # A ".." anywhere, not only at the start, lets an object path climb.
        # An escaping second entry is refused too, beside a first that exists.
        for src in ("/a.c", "../a.c", "sub/../../a.c", "sub/../a.c",
                    "a.c sub/../b.c"):
            with self.subTest(src=src):
                self._refused(
                    "APP_NAME := ok\nAPP_SRCS := %s\n" % src,
                    "relative paths inside the application directory",
                    ["a.c"])

    def test_not_c_refused(self):
        self._refused("APP_NAME := ok\nAPP_SRCS := a.h\n", "not .c", ["a.h"])

    def test_missing_src_refused(self):
        self._refused(
            "APP_NAME := ok\nAPP_SRCS := a.c b.c\n", "do not exist", ["a.c"])

    def test_app_commands_other_than_yes_refused(self):
        self._refused(
            "APP_NAME := ok\nAPP_SRCS := a.c\nAPP_COMMANDS := maybe\n",
            "APP_COMMANDS", ["a.c"])

    def test_app_commands_yes_accepted(self):
        rc, out = self._make(
            "APP_NAME := ok\nAPP_SRCS := a.c\nAPP_COMMANDS := yes\n", ["a.c"])
        self.assertEqual(rc, 0, out)
        self.assertIn("app-ok", out)


# The hook-less application: example.c with its command hook renamed, so it
# defines every hook but tfw_app_command (tfw/app.h's prototype is renamed with
# it).
_NOCMD_C = ('#define tfw_app_command example_unused_command\n'
            '#include "example.c"\n')


@unittest.skipIf(_MAKE is None, "no make")
@unittest.skipIf(shutil.which("cc") is None, "no cc")
class AppCommandsRelinkTest(unittest.TestCase):
    """APP_COMMANDS does not change the image's path, so an incremental build
    after it changes must still relink: the hook missing, or defined twice,
    fails the build instead of the old binary being kept."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.app = os.path.join(tmp.name, "app")
        self.build = os.path.join(tmp.name, "build")
        os.makedirs(self.app)
        shutil.copy(os.path.join(_REPO, "test", "app", "example", "example.c"),
                    self.app)
        with open(os.path.join(self.app, "nocmd.c"), "w") as f:
            f.write(_NOCMD_C)

    def _app_mk(self, srcs, commands):
        with open(os.path.join(self.app, "app.mk"), "w") as f:
            f.write("APP_NAME := relink-t\nAPP_SRCS := %s\n" % srcs)
            if commands:
                f.write("APP_COMMANDS := yes\n")

    def _make(self, *args):
        env = dict(os.environ)
        for name in ("MAKEFLAGS", "MAKELEVEL", "MFLAGS", "APP_DIR"):
            env.pop(name, None)
        p = subprocess.run(
            [_MAKE, "--no-print-directory", "-C", _REPO] + list(args)
            + ["APP_DIR=" + self.app, "BUILD_DIR=" + self.build],
            env=env, capture_output=True, text=True, timeout=600)
        return p.returncode, p.stdout + p.stderr

    def _built(self):
        rc, out = self._make("tfwsim")
        self.assertEqual(rc, 0, out)

    def test_commands_turned_on_relinks(self):
        self._app_mk("nocmd.c", commands=False)
        self._built()
        self._app_mk("nocmd.c", commands=True)
        rc, out = self._make("tfwsim")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("tfw_app_command", out)
        self.assertIn("undefined reference", out)

    def test_commands_turned_off_relinks(self):
        self._app_mk("example.c", commands=True)
        self._built()
        self._app_mk("example.c", commands=False)
        rc, out = self._make("tfwsim")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("tfw_app_command", out)
        self.assertIn("multiple definition", out)

    def test_unchanged_selection_does_not_relink(self):
        self._app_mk("example.c", commands=True)
        self._built()
        rc, host_dir = self._make("-s", "host-dir")
        self.assertEqual(rc, 0, host_dir)
        image = os.path.join(_REPO, host_dir.strip(), "tfwsim-app-relink-t")
        before = os.stat(image).st_mtime_ns
        self._built()
        self.assertEqual(os.stat(image).st_mtime_ns, before)

    def test_firmware_link_depends_on_the_selection(self):
        # No firmware build: avr-gcc may be missing. make's database lists
        # the ELF's prerequisites without running a recipe.
        self._app_mk("example.c", commands=True)
        rc, out = self._make("-pn", "firmware", "BOARD=atmega328p")
        elf = [line for line in out.splitlines()
               if ":" in line
               and line.split(":", 1)[0].endswith("trusted-firmware.elf")]
        self.assertTrue(elf, "no trusted-firmware.elf rule in make -pn")
        self.assertTrue(any("app-config.txt" in line for line in elf),
                        "\n".join(elf))


if __name__ == "__main__":
    unittest.main()
