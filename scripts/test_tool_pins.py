# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/tool-pins.sh, the toolchain image's one pin file (make scripts-test).

The file is read two ways: build/docker/devcontainer/Dockerfile and
mk/docker.mk source it with a shell, and scripts/deps-check.py's load_pins
parses it. Its header promises plain NAME=value lines, no expansion, a value
with spaces double-quoted; these tests hold it to that, check that the shell
and the parser read the same values, and that the digests are well formed and
the pins that name one tool twice agree. No network.

TOOL_PINS_FILE names another pin file to check, for a mutation run against a
scratch copy.
"""

import importlib.util
import os
import re
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("TOOL_PINS_FILE") or os.path.join(REPO, "scripts", "tool-pins.sh")
DEPS_CHECK = os.path.join(REPO, "scripts", "deps-check.py")
ESP32_MK = os.path.join(REPO, "mk", "arch", "esp32.mk")

LINE = re.compile(r"^([A-Z][A-Z0-9_]*)=(.*)$")
# An unquoted value: no space, no quote, nothing the shell expands or splits on.
UNQUOTED = re.compile(r"^[A-Za-z0-9._:@/+=,-]*$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")


def load_deps_check():
    """scripts/deps-check.py, as a module (its name has a dash)."""
    spec = importlib.util.spec_from_file_location("deps_check", DEPS_CHECK)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_lines():
    with open(SCRIPT, encoding="utf-8") as f:
        return f.read().split("\n")


def assignments():
    """(line number, NAME, raw value) for each NAME=value line."""
    out = []
    for n, line in enumerate(read_lines(), 1):
        m = LINE.match(line)
        if m:
            out.append((n, m.group(1), m.group(2)))
    return out


def unquote(raw):
    if len(raw) >= 2 and raw[0] == raw[-1] == '"':
        return raw[1:-1]
    return raw


def pins():
    return {name: unquote(raw) for _, name, raw in assignments()}


class TestFormat(unittest.TestCase):
    def test_every_line_is_blank_a_comment_or_name_value(self):
        for n, line in enumerate(read_lines(), 1):
            if line == "" or line.startswith("#"):
                continue
            self.assertRegex(line, LINE, f"{SCRIPT}:{n}: not NAME=value")

    def test_no_value_expands_or_splits(self):
        for n, name, raw in assignments():
            where = f"{SCRIPT}:{n}: {name}"
            for c in "$`\\":
                self.assertNotIn(c, raw, f"{where}: holds {c!r}")
            if raw.startswith('"'):
                # Inside double quotes the shell keeps a space or a single
                # quote as it is, and load_pins strips the one pair.
                self.assertGreaterEqual(len(raw), 2, f"{where}: unclosed quote")
                self.assertTrue(raw.endswith('"'), f"{where}: unclosed quote")
                self.assertNotIn('"', raw[1:-1], f"{where}: a quote inside the quotes")
            else:
                self.assertRegex(raw, UNQUOTED, f"{where}: a space, quote or shell character needs double quotes")

    def test_no_name_twice(self):
        seen = {}
        for n, name, _ in assignments():
            self.assertNotIn(name, seen, f"{SCRIPT}:{n}: {name} also on line {seen.get(name)}")
            seen[name] = n


class TestShellAgreesWithParser(unittest.TestCase):
    def test_sourced_values_equal_load_pins(self):
        parsed = load_deps_check().load_pins(SCRIPT)
        self.assertTrue(parsed, f"load_pins read no pins from {SCRIPT}")
        r = subprocess.run(
            ["env", "-i", "PATH=/usr/bin:/bin", "sh", "-c", 'set -a; . "$1"; env -0', "sh", SCRIPT],
            capture_output=True, timeout=30, check=False)
        self.assertEqual(r.returncode, 0, r.stderr.decode(errors="replace"))
        shell = {}
        for entry in r.stdout.decode("utf-8").split("\0"):
            if "=" in entry:
                k, v = entry.split("=", 1)
                shell[k] = v
        for name, value in parsed.items():
            self.assertIn(name, shell, f"{name}: the shell did not set it")
            self.assertEqual(shell[name], value, f"{name}: the shell and load_pins disagree")


class TestValues(unittest.TestCase):
    def setUp(self):
        self.pins = pins()

    def test_base_digest(self):
        self.assertRegex(self.pins.get("BASE_DIGEST", ""), r"^sha256:[0-9a-f]{64}$")

    def test_go_image_names_go_version(self):
        go = self.pins.get("GO_VERSION", "")
        self.assertTrue(go, "GO_VERSION is empty")
        self.assertRegex(self.pins.get("GO_IMAGE", ""),
                         r"^golang:" + re.escape(go) + r"-[a-z0-9.]+@sha256:[0-9a-f]{64}$",
                         "GO_IMAGE must name GO_VERSION's Go, pinned by digest")

    def test_sha256_pins_are_hex(self):
        names = [n for n in self.pins if n == "GO_SHA256" or "_SHA256_" in n]
        self.assertIn("GO_SHA256", names)
        for name in names:
            self.assertRegex(self.pins[name], HEX64, name)

    def test_versions_are_set(self):
        names = [n for n in self.pins if n.endswith("_VERSION")]
        self.assertTrue(names)
        for name in names:
            self.assertTrue(self.pins[name].strip(), f"{name} is empty")

    def test_esp_idf_version_matches_esp32_mk(self):
        with open(ESP32_MK, encoding="utf-8") as f:
            m = re.search(r"^ESP_IDF_VERSION\s*:=\s*(\S+)\s*$", f.read(), re.M)
        self.assertIsNotNone(m, f"{ESP32_MK}: no ESP_IDF_VERSION :=")
        self.assertEqual(self.pins.get("ESP_IDF_VERSION"), m.group(1),
                         "ESP_IDF_VERSION differs from mk/arch/esp32.mk")

    def test_held_reasons_are_quoted_lines(self):
        held = [(n, name, raw) for n, name, raw in assignments() if name.startswith("HELD_")]
        self.assertTrue(held)
        for n, name, raw in held:
            self.assertTrue(len(raw) >= 2 and raw[0] == raw[-1] == '"', f"{SCRIPT}:{n}: {name} is not quoted")
            self.assertTrue(raw[1:-1].strip(), f"{SCRIPT}:{n}: {name} gives no reason")


if __name__ == "__main__":
    unittest.main()
