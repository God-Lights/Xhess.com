#!/usr/bin/env python3
"""Tests for apply_params.py:  python3 engine/tools/apply_params_test.py"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import apply_params as ap  # noqa: E402

DEF = os.path.join(os.path.dirname(__file__), "..", "src", "eval", "params.def")

SAMPLE = """// header comment (with parentheses) stays
SCORE(doubledPawn, -8, -18)   // trailing comment stays
INT(shieldNear, 10)
ARRAY(materialMg, 7, 0, 100, 320, 330, 500, 900, 0)
ARRAY(pstKnight, 64,
    1, 2, 3, 4, 5, 6, 7, 8,
    1, 2, 3, 4, 5, 6, 7, 8,
    1, 2, 3, 4, 5, 6, 7, 8,
    1, 2, 3, 4, 5, 6, 7, 8,
    1, 2, 3, 4, 5, 6, 7, 8,
    1, 2, 3, 4, 5, 6, 7, 8,
    1, 2, 3, 4, 5, 6, 7, 8,
    1, 2, 3, 4, 5, 6, 7, 8,
)
"""


class ApplyParams(unittest.TestCase):
    def test_parse_values(self):
        v = ap.parse_values("# c\n\ndoubledPawn = -8 -18\nshieldNear = 10\nmaterialMg = 0 100 320\n")
        self.assertEqual(v, {"doubledPawn": [-8, -18], "shieldNear": [10], "materialMg": [0, 100, 320]})
        with self.assertRaises(ValueError):
            ap.parse_values("no equals sign")

    def test_values_are_replaced_and_comments_kept(self):
        new = ap.apply(SAMPLE, {"doubledPawn": [-5, -9], "shieldNear": [12]})
        self.assertIn("SCORE(doubledPawn, -5, -9)   // trailing comment stays", new)
        self.assertIn("INT(shieldNear, 12)", new)
        self.assertIn("// header comment (with parentheses) stays", new)
        self.assertIn("ARRAY(materialMg, 7, 0, 100, 320, 330, 500, 900, 0)", new)   # untouched
        self.assertEqual(ap.current_values(new)["doubledPawn"], [-5, -9])

    def test_arrays_and_tables(self):
        mat = [0, 100, 321, 331, 501, 901, 0]
        table = list(range(64))
        new = ap.apply(SAMPLE, {"materialMg": mat, "pstKnight": table})
        self.assertIn("ARRAY(materialMg, 7, 0, 100, 321, 331, 501, 901, 0)", new)
        self.assertEqual(ap.current_values(new)["pstKnight"], table)
        self.assertEqual(new.count("ARRAY(pstKnight, 64,\n"), 1)    # still eight rows of eight
        self.assertIn("      0,    1,    2,    3,    4,    5,    6,    7,\n", new.replace("    ", "    "))

    def test_rejects_unknown_names_and_wrong_sizes(self):
        with self.assertRaises(ValueError):
            ap.apply(SAMPLE, {"noSuchWeight": [1]})
        with self.assertRaises(ValueError):
            ap.apply(SAMPLE, {"doubledPawn": [1]})
        with self.assertRaises(ValueError):
            ap.apply(SAMPLE, {"materialMg": [1, 2, 3]})
        with self.assertRaises(ValueError):
            ap.apply(SAMPLE, {"shieldNear": [1, 2]})

    def test_applying_the_current_values_changes_nothing(self):
        text = open(DEF, encoding="utf-8").read()
        same = ap.apply(text, ap.current_values(text))
        self.assertEqual(same, text, "formatting of params.def must be reproducible by apply_params.py")

    def test_command_line(self):
        with tempfile.TemporaryDirectory() as d:
            d_path, t_path = os.path.join(d, "p.def"), os.path.join(d, "t.txt")
            open(d_path, "w").write(SAMPLE)
            open(t_path, "w").write("shieldNear = 99\n")
            self.assertEqual(ap.main(["x", d_path, t_path]), 0)
            self.assertIn("INT(shieldNear, 99)", open(d_path).read())
            open(t_path, "w").write("bogus = 1\n")
            before = open(d_path).read()
            self.assertEqual(ap.main(["x", d_path, t_path]), 1)
            self.assertEqual(open(d_path).read(), before)    # nothing written on error


if __name__ == "__main__":
    unittest.main()
