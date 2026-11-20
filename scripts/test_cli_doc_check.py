# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/cli-doc-check.py (make scripts-test). No network."""

import contextlib
import importlib.util
import io
import os
import shutil
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
spec = importlib.util.spec_from_file_location("cli_doc_check", os.path.join(HERE, "cli-doc-check.py"))
cdc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cdc)

MAIN_GO = """package main

func main() {
\tvar (
\t\tbaud    = flag.Int("baud", 115200, "serial baud rate")
\t\ttimeout = flag.Duration("timeout", 10*time.Second, "how long to wait")
\t)
\tflag.Parse()
\tif flag.NArg() >= 1 && flag.Arg(0) == "ports" {
\t\treturn
\t}
\tfs := flag.NewFlagSet("x", flag.ContinueOnError)
\tfs.String("mac", "", "a verb's own flag, not a global one")
}

func run(cmd string, args []string) error {
\tswitch cmd {
\tcase "ping":
\t\treturn nil
\tcase "mode":
\t\tswitch args[1] {
\t\tcase "in", "out":
\t\t}
\tcase "netcfg":
\t\treturn runNetcfg(args)
\tcase "hsm-heartbeat", "hsm-records":
\t\treturn runHSM(cmd, args)
\tdefault:
\t\treturn runPeripheral(cmd, args)
\t}
}
"""

NETCFG_GO = """package main

func runNetcfg(args []string) error {
\tswitch args[0] {
\tcase "set":
\tcase "get":
\t}
\treturn nil
}
"""

HSM_GO = """package main

func runHSM(cmd string, args []string) error {
\tswitch cmd {
\tcase "hsm-heartbeat":
\tcase "hsm-records":
\t}
\treturn nil
}
"""

PERIPHERALS_GO = """package main

func runPeripheral(cmd string, args []string) error {
\tswitch cmd {
\tcase "analog":
\t\treturn nil
\t}
\tswitch other {
\tcase "not-a-verb":
\t}
\treturn nil
}
"""

PAGE = """# Host SDK -- command-line reference

## `tfwctl` -- the reference client

### Flags

| Flag | Default | Meaning |
|---|---|---|
| `-baud` | `115200` | serial baud rate |
| `-timeout` | `10s` | how long to wait |

### Commands

| Command | Args | Does |
|---|---|---|
| `ping` | `[text]` | echoes |
| `mode` | `<pin> <in\\|out>` | a pin's direction |
| `netcfg set` | `--mac <mac>` | writes the block |
| `netcfg get` | -- | reads it |
| `hsm-heartbeat`, `hsm-records` | -- | the engine |
| `analog` | `<pin>` | one conversion |
| `ports` | -- | lists ports |

## `tfwprov` -- another command

### Flags

| Flag | Default | Meaning |
|---|---|---|
| `-commit` | `false` | another command's flag is not tfwctl's |

### Commands

| `erase` | another command's verb is not tfwctl's |
"""


class FixtureTree(unittest.TestCase):
    """A tfwctl whose page passes; each test breaks one fact in it."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="cli-doc-check-")
        self.addCleanup(shutil.rmtree, self.root)
        files = {
            "sdk/go/cmd/tfwctl/main.go": MAIN_GO,
            "sdk/go/cmd/tfwctl/netcfg.go": NETCFG_GO,
            "sdk/go/cmd/tfwctl/hsm.go": HSM_GO,
            "sdk/go/cmd/tfwctl/peripherals.go": PERIPHERALS_GO,
            "sdk/go/cmd/tfwctl/main_test.go": 'package main\n\nfunc t(cmd string) {\n\tswitch cmd {\n\tcase "test-only":\n\t}\n}\n',
            "docs/host-sdk/cli.md": PAGE,
        }
        for rel, text in files.items():
            self.write(rel, text)

    def path(self, rel):
        return os.path.join(self.root, rel)

    def write(self, rel, text):
        os.makedirs(os.path.dirname(self.path(rel)), exist_ok=True)
        with open(self.path(rel), "w", encoding="utf-8") as f:
            f.write(text)

    def edit(self, rel, old, new):
        with open(self.path(rel), encoding="utf-8") as f:
            text = f.read()
        self.assertIn(old, text, "fixture %s has no %r" % (rel, old))
        self.write(rel, text.replace(old, new, 1))

    def assertFails(self, *fragments):
        errors = cdc.check(self.root)
        joined = "\n".join(errors)
        for fragment in fragments:
            self.assertIn(fragment, joined)
        return errors


class TestPasses(FixtureTree):
    def test_verbs_are_exactly_the_dispatched_ones(self):
        self.assertEqual({"ping", "mode", "netcfg", "hsm-heartbeat", "hsm-records", "analog", "ports"},
                         cdc.verbs(self.root))

    def test_flags_are_the_default_flag_sets(self):
        self.assertEqual({"-baud", "-timeout"}, cdc.flags(self.root))

    def test_page_listing_every_verb_passes(self):
        self.assertEqual([], cdc.check(self.root))

    def test_main_reports_and_exits_zero(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(0, cdc.main(["cli-doc-check", self.root]))
        self.assertIn("lists every verb and flag of sdk/go/cmd/tfwctl and nothing else", out.getvalue())

    def test_main_exits_one_naming_the_missing_verb(self):
        self.edit("docs/host-sdk/cli.md", "| `analog` | `<pin>` | one conversion |\n", "")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(1, cdc.main(["cli-doc-check", self.root]))
        self.assertIn("cli-doc-check: docs/host-sdk/cli.md: tfwctl verb analog has no row in ### Commands",
                      out.getvalue())


class TestMissing(FixtureTree):
    def test_new_verb_in_a_dispatched_file_fails(self):
        self.edit("sdk/go/cmd/tfwctl/hsm.go", '\tcase "hsm-records":', '\tcase "hsm-records":\n\tcase "hsm-load":')
        self.assertFails("tfwctl verb hsm-load has no row")

    def test_one_verb_of_a_grouped_row_missing_fails(self):
        self.edit("docs/host-sdk/cli.md", "`hsm-heartbeat`, `hsm-records`", "`hsm-heartbeat`")
        self.assertFails("tfwctl verb hsm-records has no row")

    def test_new_file_with_a_verb_switch_counts(self):
        self.write("sdk/go/cmd/tfwctl/extra.go", 'package main\n\nfunc x(cmd string) {\n\tswitch cmd {\n\tcase "extra":\n\t}\n}\n')
        self.assertFails("tfwctl verb extra has no row")

    def test_verb_main_answers_before_a_port_fails(self):
        self.edit("docs/host-sdk/cli.md", "| `ports` | -- | lists ports |\n", "")
        self.assertFails("tfwctl verb ports has no row")

    def test_sub_verb_rows_both_gone_fails(self):
        self.edit("docs/host-sdk/cli.md", "| `netcfg set` | `--mac <mac>` | writes the block |\n| `netcfg get` | -- | reads it |\n", "")
        self.assertFails("tfwctl verb netcfg has no row")

    def test_new_flag_without_a_row_fails(self):
        self.edit("sdk/go/cmd/tfwctl/main.go", "\tflag.Parse()", '\tflag.Bool("json", false, "emit JSON")\n\tflag.Parse()')
        self.assertFails("tfwctl flag -json has no row in ### Flags")

    def test_missing_commands_table_fails(self):
        self.edit("docs/host-sdk/cli.md", "### Commands", "### Verbs")
        self.assertFails("no '### Commands' table in the ## `tfwctl` section")

    def test_missing_section_fails(self):
        self.edit("docs/host-sdk/cli.md", "## `tfwctl` -- the reference client", "## tfwctl")
        self.assertFails("no '## `tfwctl`' section")

    def test_missing_page_fails(self):
        os.remove(self.path("docs/host-sdk/cli.md"))
        self.assertFails("docs/host-sdk/cli.md: No such file")


class TestStale(FixtureTree):
    def test_removed_verb_fails(self):
        self.edit("sdk/go/cmd/tfwctl/peripherals.go", '\tcase "analog":', '\tcase "analogue":')
        self.assertFails("tfwctl verb analogue has no row", "### Commands analog is not a tfwctl verb")

    def test_removed_flag_fails(self):
        self.edit("sdk/go/cmd/tfwctl/main.go", 'flag.Int("baud"', 'flag.Int("speed"')
        self.assertFails("### Flags -baud is not a tfwctl flag")


class TestDispatched(unittest.TestCase):
    def test_nested_and_other_switches_are_not_verbs(self):
        self.assertEqual({"a", "b"}, cdc.dispatched(
            'func f() {\n\tswitch cmd {\n\tcase "a", "b":\n\t\tswitch x {\n\t\tcase "c":\n\t\t}\n\t}\n'
            '\tswitch y {\n\tcase "d":\n\t}\n}\n'))


class TestRepository(unittest.TestCase):
    """The tree itself. A failure in test_the_page_matches_tfwctl is a
    missing or stale row in docs/host-sdk/cli.md, not a test to edit."""

    def test_the_page_matches_tfwctl(self):
        self.assertEqual([], cdc.check(REPO))

    def test_tfwctl_was_read(self):
        found = cdc.verbs(REPO)
        for verb in ("ping", "ports", "netcfg", "ota-stage", "attest-csr", "auth", "1wire-read",
                     "hsm-heartbeat", "hsm-report", "hsm-verdict", "hsm-select", "hsm-load",
                     "hsm-record", "hsm-records"):
            self.assertIn(verb, found)
        self.assertNotIn("pullup", found)
        self.assertNotIn("set", found)
        self.assertEqual({"-baud", "-json", "-timeout", "-boot-wait"}, cdc.flags(REPO))


if __name__ == "__main__":
    unittest.main()
