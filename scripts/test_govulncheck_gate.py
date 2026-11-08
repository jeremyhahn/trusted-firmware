#!/usr/bin/env python3
"""Tests for govulncheck-gate.py: a reachable finding fails unless the
allowlist names it for that module, an imported-only finding and a stale entry
are reported without failing, and the allowlist in this tree parses. No
govulncheck, no network."""

import importlib.util
import json
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("govulncheck_gate", os.path.join(HERE, "govulncheck-gate.py"))
gg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gg)

CONFIG = {"config": {"protocol_version": "v1.0.0", "scanner_name": "govulncheck"}}


def called(osv):
    return {"finding": {"osv": osv, "fixed_version": "", "trace": [
        {"module": "github.com/x/vuln", "version": "v1.5.0", "package": "github.com/x/vuln/transport",
         "function": "NewStreamableHTTP"},
        {"module": "example.com/m", "package": "example.com/m/avr", "receiver": "Backend",
         "function": "PossessionSigner", "position": {"filename": "avr/possession.go", "line": 53}}]}}


def imported(osv):
    return {"finding": {"osv": osv, "trace": [{"module": "github.com/x/vuln", "version": "v1.5.0"}]}}


def stream(*msgs):
    # govulncheck indents each object over several lines, one after another.
    return "\n".join(json.dumps(m, indent=2) for m in (CONFIG,) + msgs)


class Gate(unittest.TestCase):
    def test_a_reachable_finding_not_in_the_allowlist_fails(self):
        rc, out = gg.gate(stream(called("GO-1")), "xkms", {})
        self.assertEqual(rc, 1)
        self.assertIn("FAIL GO-1", out[0])
        self.assertIn("avr.Backend.PossessionSigner (possession.go:53) -> github.com/x/vuln/transport.NewStreamableHTTP", out[0])

    def test_a_reachable_finding_in_the_allowlist_passes_and_prints_its_reason(self):
        rc, out = gg.gate(stream(called("GO-1"), called("GO-1")), "xkms", {"GO-1": "why"})
        self.assertEqual(rc, 0)
        self.assertEqual(out[0][:13], "allowed GO-1:")
        self.assertEqual(out[1], "  reason: why")

    def test_an_imported_only_finding_is_reported_and_passes(self):
        rc, out = gg.gate(stream(imported("GO-2")), "xkms", {})
        self.assertEqual((rc, out), (0, ["imported, not called: GO-2"]))

    def test_an_entry_with_no_reachable_finding_is_reported_stale(self):
        rc, out = gg.gate(stream(imported("GO-1")), "xkms", {"GO-1": "why"})
        self.assertEqual(rc, 0)
        self.assertIn("stale entry: xkms GO-1 has no reachable finding; delete it", out)

    def test_no_govulncheck_output_fails(self):
        self.assertEqual(gg.gate("", "xkms", {})[0], 1)


class Allowlist(unittest.TestCase):
    def write(self, text):
        f = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf-8")
        f.write(text)
        f.close()
        self.addCleanup(os.unlink, f.name)
        return f.name

    def test_entries_are_read_for_their_module_only(self):
        path = self.write("# comment\n\nxkms GO-1 the reason, in words\nsdk/go GO-2 another\n")
        self.assertEqual(gg.read_allow(path, "xkms"), {"GO-1": "the reason, in words"})

    def test_an_entry_without_a_reason_is_refused(self):
        with self.assertRaises(ValueError):
            gg.read_allow(self.write("xkms GO-1\n"), "xkms")

    def test_the_allowlist_in_this_tree_parses(self):
        for module in ("xkms", "sdk/go", "test/integration/esp32"):
            for osv, reason in gg.read_allow(gg.ALLOW, module).items():
                self.assertRegex(osv, r"^GO-\d{4}-\d+$")
                self.assertGreater(len(reason), 40, osv)


if __name__ == "__main__":
    unittest.main()
