# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The Go SDK's AppState and APP_INFO widths mirror the firmware's (make scripts-test). No network."""

import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

APP_RUNNER_H = os.path.join(REPO, "src", "core", "app_runner.h")
APP_H = os.path.join(REPO, "include", "tfw", "app.h")
PROTOCOL_H = os.path.join(REPO, "include", "tfw", "protocol.h")
DEVICE_GO = os.path.join(REPO, "sdk", "go", "device.go")
COMMANDS_GO = os.path.join(REPO, "sdk", "go", "commands.go")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def parse_c_states(text):
    """{suffix: value} for every TFW_APP_STATE_* define, underscores dropped."""
    return {m.group(1).replace("_", ""): int(m.group(2))
            for m in re.finditer(r"#define TFW_APP_STATE_(\w+)\s+(\d+)u", text)}


def parse_go_states(text):
    """({suffix: value}, suffixes with a case in AppState.String), upper-cased."""
    states = {m.group(1).upper(): int(m.group(2))
              for m in re.finditer(r"App(\w+)\s+AppState\s*=\s*(\d+)", text)}
    m = re.search(r"func \(s AppState\) String\(\) string \{\n(.*?)\n\}\n", text, re.S)
    body = m.group(1) if m else ""
    cases = {c.upper() for c in re.findall(r"case App(\w+):", body)}
    return states, cases


def _define(text, name):
    m = re.search(r"#define %s\s+(\d+)u" % name, text)
    return int(m.group(1)) if m else None


def parse_c_widths(app_h, protocol_h):
    return {
        "name": _define(app_h, "TFW_APP_NAME_LEN"),
        "profile": _define(app_h, "TFW_APP_PROFILE_LEN"),
        "info": _define(protocol_h, "TFW_APP_INFO_LEN"),
    }


def _const(text, name):
    m = re.search(r"\b%s\s*=\s*(\d+)\b" % name, text)
    return int(m.group(1)) if m else None


def parse_go_widths(commands_go):
    off = _const(commands_go, "appInfoNameOff")
    name = _const(commands_go, "appInfoNameLen")
    profile = _const(commands_go, "appInfoProfileLen")
    info = None if None in (off, name, profile) else off + name + profile
    return {"name": name, "profile": profile, "info": info}


def mismatches(c_states, go_states, go_cases, c_widths, go_widths):
    out = []
    for s in c_states:
        if s not in go_states:
            out.append("state %s: in app_runner.h, no Go constant" % s)
        elif c_states[s] != go_states[s]:
            out.append("state %s: app_runner.h %d, Go %d" % (s, c_states[s], go_states[s]))
    for s in go_states:
        if s not in c_states:
            out.append("state %s: Go constant, not in app_runner.h" % s)
        if s not in go_cases:
            out.append("state %s: no case in AppState.String" % s)
    for k in c_widths:
        if c_widths[k] != go_widths.get(k):
            out.append("width %s: C %s, Go %s" % (k, c_widths[k], go_widths.get(k)))
    return sorted(out)


class AppInfoMirror(unittest.TestCase):
    def setUp(self):
        self.runner = read(APP_RUNNER_H)
        self.app = read(APP_H)
        self.protocol = read(PROTOCOL_H)
        self.device = read(DEVICE_GO)
        self.commands = read(COMMANDS_GO)

    def check(self, runner=None, app=None, protocol=None, device=None, commands=None):
        go_states, go_cases = parse_go_states(device or self.device)
        return mismatches(
            parse_c_states(runner or self.runner), go_states, go_cases,
            parse_c_widths(app or self.app, protocol or self.protocol),
            parse_go_widths(commands or self.commands))

    def mutate(self, text, old, new):
        self.assertIn(old, text)
        return text.replace(old, new)

    def test_main_agrees(self):
        self.assertEqual(self.check(), [])
        go_states, go_cases = parse_go_states(self.device)
        self.assertEqual(len(parse_c_states(self.runner)), 5)
        self.assertEqual(len(go_states), 5)
        self.assertEqual(len(go_cases), 5)
        self.assertEqual(parse_c_widths(self.app, self.protocol),
                         {"name": 16, "profile": 32, "info": 63})

    def test_renumbered_state(self):
        runner = self.mutate(self.runner, "TFW_APP_STATE_STOPPED     4u",
                             "TFW_APP_STATE_STOPPED     5u")
        self.assertEqual(self.check(runner=runner), ["state STOPPED: app_runner.h 5, Go 4"])

    def test_added_state(self):
        old = "#define TFW_APP_STATE_STOPPED     4u   /* the stop hooks ran */\n"
        runner = self.mutate(self.runner, old, old + "#define TFW_APP_STATE_PAUSED      5u\n")
        self.assertEqual(self.check(runner=runner),
                         ["state PAUSED: in app_runner.h, no Go constant"])

    def test_missing_string_case(self):
        device = self.mutate(self.device, "\tcase AppStopped:\n\t\treturn \"stopped\"\n", "")
        self.assertEqual(self.check(device=device), ["state STOPPED: no case in AppState.String"])

    def test_widened_fields(self):
        app = self.mutate(self.app, "TFW_APP_PROFILE_LEN  32u", "TFW_APP_PROFILE_LEN  48u")
        self.assertEqual(self.check(app=app), ["width profile: C 48, Go 32"])
        protocol = self.mutate(self.protocol, "TFW_APP_INFO_LEN     63u", "TFW_APP_INFO_LEN     79u")
        self.assertEqual(self.check(protocol=protocol), ["width info: C 79, Go 63"])


if __name__ == "__main__":
    unittest.main()
