# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The empty runner keeps the runner interface and nothing more (#635).

src/core/app_runner_none.c lines 4-12: it is linked instead of app_runner.c by
every build without APP_DIR (mk/app.mk), links no descriptor and no config
span, and keeps every runner entry point a real symbol because main.c, tfwsim
and tfw_sys_poll call the runner on every build. src/core/app_runner.h lines
56-60: tfw_app_cmd_info is defined beside the runner and nowhere else, so an
image that links app_runner_none.c has no APP_INFO handler to reference.

app-link-check reads hooks only, and test_app_none links just the entry points
the host library calls, so nothing else ties the two runner files to the
header. This test does: every tfw_app_runner_* the header declares is defined
in both runners, the empty runner defines no runner call the header lacks,
and it defines no tfw_app_cmd_info and no tfw_app_config_*.
"""

import os
import re
import unittest

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_HEADER = os.path.join("src", "core", "app_runner.h")
_NONE = os.path.join("src", "core", "app_runner_none.c")
_RUNNER = os.path.join("src", "core", "app_runner.c")

# Defined only beside the runner: the APP_INFO handler and the config span.
_RUNNER_ONLY = ("tfw_app_cmd_info", "tfw_app_config_read", "tfw_app_config_write")

_DECL = re.compile(r"^[A-Za-z_][\w \*]*?\b(tfw_app_\w+)\s*\([^;{]*\)\s*;", re.M)
_DEF = re.compile(r"^[A-Za-z_][\w \*]*?\b(tfw_app_\w+)\s*\([^;]*?\)\s*\{", re.M | re.S)


def _strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def declared(header_text):
    """Names of the tfw_app_* functions a header declares."""
    return set(_DECL.findall(_strip_comments(header_text)))


def defined(c_text):
    """Names of the tfw_app_* functions a C file defines."""
    return set(_DEF.findall(_strip_comments(c_text)))


def problems(header_text, none_text, runner_text):
    """Every way the two runners leave the header's runner interface."""
    decl = declared(header_text)
    entry = {n for n in decl if n.startswith("tfw_app_runner_")}
    none_defs = defined(none_text)
    runner_defs = defined(runner_text)
    out = []
    for name in entry - none_defs:
        out.append("%s: declared in app_runner.h, not defined in app_runner_none.c" % name)
    for name in entry - runner_defs:
        out.append("%s: declared in app_runner.h, not defined in app_runner.c" % name)
    for name in {n for n in none_defs if n.startswith("tfw_app_runner_")} - decl:
        out.append("%s: defined in app_runner_none.c, not declared in app_runner.h" % name)
    for name in none_defs & set(_RUNNER_ONLY):
        out.append("%s: defined in app_runner_none.c, which links no APP_INFO handler "
                   "and no config span" % name)
    return sorted(out)


def _read(rel):
    with open(os.path.join(_ROOT, rel), encoding="utf-8") as f:
        return f.read()


class EmptyRunnerSurface(unittest.TestCase):
    def setUp(self):
        self.header = _read(_HEADER)
        self.none = _read(_NONE)
        self.runner = _read(_RUNNER)

    def _replace(self, text, old, new):
        self.assertIn(old, text)
        return text.replace(old, new, 1)

    def test_main_matches(self):
        self.assertEqual(problems(self.header, self.none, self.runner), [])
        entry = {n for n in declared(self.header) if n.startswith("tfw_app_runner_")}
        self.assertEqual(len(entry), 7, sorted(entry))

    def test_new_entry_point_needs_both_runners(self):
        header = self._replace(self.header, "void tfw_app_runner_poll(void);\n",
                               "void tfw_app_runner_poll(void);\n"
                               "void tfw_app_runner_pause(void);\n")
        self.assertEqual(problems(header, self.none, self.runner), [
            "tfw_app_runner_pause: declared in app_runner.h, not defined in app_runner.c",
            "tfw_app_runner_pause: declared in app_runner.h, not defined in app_runner_none.c",
        ])

    def test_missing_empty_twin(self):
        none = self._replace(self.none,
                             "uint16_t tfw_app_runner_overruns(void)\n{\n    return 0u;\n}\n", "")
        self.assertEqual(problems(self.header, none, self.runner), [
            "tfw_app_runner_overruns: declared in app_runner.h, not defined in app_runner_none.c",
        ])

    def test_config_span_refused(self):
        none = self.none + ("\ntfw_status_t tfw_app_config_read(uint16_t off, uint8_t *buf, "
                            "uint8_t len) { (void)off; (void)buf; (void)len; return TFW_OK; }\n")
        got = problems(self.header, none, self.runner)
        self.assertEqual(len(got), 1, got)
        self.assertTrue(got[0].startswith("tfw_app_config_read: defined in app_runner_none.c"), got)

    def test_app_info_handler_refused(self):
        none = self.none + ("\ntfw_status_t tfw_app_cmd_info(tfw_reader_t *r, tfw_writer_t *w) "
                            "{ (void)r; (void)w; return TFW_OK; }\n")
        got = problems(self.header, none, self.runner)
        self.assertEqual(len(got), 1, got)
        self.assertTrue(got[0].startswith("tfw_app_cmd_info: defined in app_runner_none.c"), got)

    def test_comment_is_not_a_definition(self):
        none = self.none + "\n/* tfw_app_config_read(off, buf, len) { } */\n"
        self.assertEqual(problems(self.header, none, self.runner), [])


if __name__ == "__main__":
    unittest.main()
