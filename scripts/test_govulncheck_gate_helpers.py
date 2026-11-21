# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/govulncheck-gate.py's stream and frame helpers (make scripts-test).

`messages` decodes govulncheck's JSON stream, objects one after another with
whitespace between them; `frame` names a trace frame as package, receiver
without its star, function and the file's basename and line; `gate` prints a
reachable path outermost caller first. scripts/test_govulncheck_gate.py holds
the verdicts. GOVULNCHECK_GATE_SCRIPT names another copy of the script, for a
mutation run. No govulncheck, no network.
"""

import importlib.util
import json
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("GOVULNCHECK_GATE_SCRIPT", os.path.join(REPO, "scripts", "govulncheck-gate.py"))

spec = importlib.util.spec_from_file_location("govulncheck_gate", SCRIPT)
gg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gg)

CONFIG = {"config": {"protocol_version": "v1.0.0", "scanner_name": "govulncheck"}}

VULN = {"module": "github.com/x/vuln", "version": "v1.5.0", "package": "github.com/x/vuln/transport",
        "function": "NewStreamableHTTP"}
OURS = {"module": "example.com/m", "package": "example.com/m/avr", "receiver": "*Backend",
        "function": "PossessionSigner", "position": {"filename": "/src/m/avr/possession.go", "line": 53}}
PATH = "example.com/m/avr.Backend.PossessionSigner (possession.go:53) -> github.com/x/vuln/transport.NewStreamableHTTP"


def stream(*msgs):
    # govulncheck indents each object over several lines, one after another.
    return "\n".join(json.dumps(m, indent=2) for m in (CONFIG,) + msgs)


def called(osv, trace):
    return {"finding": {"osv": osv, "fixed_version": "", "trace": trace}}


class Messages(unittest.TestCase):
    def test_objects_come_out_in_order_whatever_the_whitespace(self):
        text = ' \n\t{"a": 1}{"b": 2} {"c": 3}\n' + json.dumps({"d": [4, {"e": 5}]}, indent=2) + "\n\n "
        self.assertEqual(list(gg.messages(text)), [{"a": 1}, {"b": 2}, {"c": 3}, {"d": [4, {"e": 5}]}])

    def test_empty_and_whitespace_only_text_give_nothing(self):
        for text in ("", " ", "\n\t \n"):
            with self.subTest(text=text):
                self.assertEqual(list(gg.messages(text)), [])

    def test_a_truncated_object_raises(self):
        with self.assertRaises(json.JSONDecodeError):
            list(gg.messages('{"a": 1}\n{"b": '))


class Frame(unittest.TestCase):
    POS = {"filename": "a/b/conn.go", "line": 7}

    def test_a_pointer_receiver_loses_its_star(self):
        f = {"package": "p", "receiver": "*Conn", "function": "Read", "position": self.POS}
        self.assertEqual(gg.frame(f), "p.Conn.Read (conn.go:7)")

    def test_a_value_receiver_reads_the_same(self):
        f = {"package": "p", "receiver": "Conn", "function": "Read", "position": self.POS}
        self.assertEqual(gg.frame(f), "p.Conn.Read (conn.go:7)")

    def test_no_receiver_names_the_function_alone(self):
        self.assertEqual(gg.frame({"package": "p", "function": "Read", "position": self.POS}), "p.Read (conn.go:7)")

    def test_the_position_prints_only_when_it_names_a_file(self):
        for pos, want in ((None, "p.Read"), ({"line": 7}, "p.Read"), ({"filename": "a/b/conn.go"}, "p.Read (conn.go:0)")):
            with self.subTest(pos=pos):
                f = {"package": "p", "function": "Read"}
                if pos is not None:
                    f["position"] = pos
                self.assertEqual(gg.frame(f), want)

    def test_no_package_falls_back_to_the_module(self):
        self.assertEqual(gg.frame({"module": "m", "function": "Read"}), "m.Read")


class Path(unittest.TestCase):
    def test_a_failing_path_reads_from_our_code_to_the_vulnerable_function(self):
        rc, out = gg.gate(stream(called("GO-1", [VULN, OURS])), "xkms", {})
        self.assertEqual(rc, 1)
        self.assertTrue(out[0].startswith("FAIL GO-1 "), out[0])
        self.assertTrue(out[0].endswith(": " + PATH), out[0])
        self.assertNotIn("*", out[0])

    def test_an_allowed_path_prints_the_same_after_its_id(self):
        rc, out = gg.gate(stream(called("GO-1", [VULN, OURS])), "xkms", {"GO-1": "why"})
        self.assertEqual((rc, out), (0, ["allowed GO-1: " + PATH, "  reason: why"]))

    def test_a_second_finding_with_the_same_id_keeps_the_first_trace(self):
        other = {"package": "example.com/m/cli", "function": "Run"}
        rc, out = gg.gate(stream(called("GO-1", [VULN, OURS]), called("GO-1", [VULN, other])), "xkms", {"GO-1": "why"})
        self.assertEqual(out[0], "allowed GO-1: " + PATH)


if __name__ == "__main__":
    unittest.main()
