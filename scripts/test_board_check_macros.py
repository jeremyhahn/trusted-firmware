# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/board-check.py's C preprocessor subset (make scripts-test).

board-check reads a board's board_profile.h and board.c through four helpers:
strip_c_comments, read_defines (object-like #defines, the first one wins),
board_macros (object- and function-like, only the first branch of an #if group
at every depth) and expand_macros (longest name first, argument count checked,
nesting bounded). These cases call them directly. No network, no toolchain.

BOARD_CHECK_SCRIPT names another copy of the script, for a mutation run only.
"""

import importlib.util
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.environ.get("BOARD_CHECK_SCRIPT") or os.path.join(REPO, "scripts", "board-check.py")
spec = importlib.util.spec_from_file_location("board_check", SCRIPT)
bc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bc)

NESTED = """#if A
#define X 1
#else
#if B
#define Y 2
#endif
#define Z 3
#endif
#ifdef C
#define P 4
#elif D
#define Q 5
#endif
#define X 9
#define F(a, b) ((a) + (b))
#define G() 7
"""


class StripCommentsTest(unittest.TestCase):
    def test_block_and_line_comments_go_text_stays(self):
        text = "a /* one\ntwo */ b\nc // three\nd"
        self.assertEqual(bc.strip_c_comments(text), "a   b\nc \nd")


class ReadDefinesTest(unittest.TestCase):
    def defines(self, header):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "board_profile.h")
            with open(path, "w") as f:
                f.write(header)
            return bc.read_defines(path)

    def test_header(self):
        got = self.defines(
            "/* #define GONE 1 */\n"
            "// #define ALSO_GONE 2\n"
            "#  define SPACED 3\n"
            "#define EMPTY\n"
            "#define FN(x) ((x) + 1)\n"
            "#define TWICE 4\n"
            "#define TWICE 5\n")
        self.assertNotIn("GONE", got)
        self.assertNotIn("ALSO_GONE", got)
        self.assertEqual(got["SPACED"], "3")
        self.assertEqual(got["EMPTY"], "")
        self.assertNotIn("FN", got)
        self.assertEqual(got["TWICE"], "4")


class BoardMacrosTest(unittest.TestCase):
    def setUp(self):
        self.macros = bc.board_macros(NESTED)

    def test_first_branch_at_every_depth(self):
        self.assertIn("X", self.macros)
        self.assertNotIn("Y", self.macros, "an #if nested in a dropped #else counted")
        self.assertNotIn("Z", self.macros)

    def test_ifdef_first_branch_elif_dropped(self):
        self.assertEqual(self.macros["P"], (None, "4"))
        self.assertNotIn("Q", self.macros)

    def test_first_define_wins(self):
        self.assertEqual(self.macros["X"], (None, "1"))

    def test_function_like(self):
        self.assertEqual(self.macros["F"], (["a", "b"], "((a) + (b))"))
        self.assertEqual(self.macros["G"], ([], "7"))


class ExpandMacrosTest(unittest.TestCase):
    MACROS = {
        "X": (None, "1"),
        "F": (["a", "b"], "((a) + (b))"),
        "G": ([], "7"),
        "TFW_PIN": (None, "short"),
        "TFW_PIN_PWM": (None, "long"),
    }

    def expand(self, text):
        return bc.expand_macros(text, self.MACROS)

    def test_function_like(self):
        self.assertEqual(self.expand("F(1u, X)"), "(((1u) + (1)))")
        self.assertEqual(self.expand("G()"), "(7)")

    def test_function_like_without_parens_is_left(self):
        self.assertEqual(self.expand("F"), "F")

    def test_object_like_keeps_following_parens(self):
        self.assertEqual(self.expand("X(3)"), "1(3)")

    def test_longest_name_first(self):
        self.assertEqual(self.expand("TFW_PIN_PWM"), "long")
        self.assertEqual(self.expand("TFW_PIN"), "short")

    def test_argument_count(self):
        with self.assertRaises(ValueError) as cm:
            self.expand("F(1u)")
        self.assertIn("takes 2 arguments", str(cm.exception))

    def test_nesting_bounded(self):
        with self.assertRaises(ValueError) as cm:
            bc.expand_macros("S", {"S": (None, "S + 1")})
        self.assertTrue(str(cm.exception).startswith("macros nest too deep"), str(cm.exception))

    def test_empty_table(self):
        self.assertEqual(bc.expand_macros("F(1u, X)", {}), "F(1u, X)")


if __name__ == "__main__":
    unittest.main()
