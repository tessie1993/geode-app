"""Unit tests for run_mutation.py: the android.yml parser, the diff reader, the include graph,
the source mutator and the report. CI runs them (mutation.yml) before the harness itself:
`python3 -m unittest discover -s tools/mutation -p 'test_*.py' -v`.
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_mutation as rm  # noqa: E402

REPO = Path(__file__).resolve().parents[2]

WORKFLOW = """\
name: CI
jobs:
  build:
    steps:
      - name: Native host tests
        working-directory: .
        run: |
          g++ -std=c++20 -O1 -Wall -Icore -Icore/tests/stub core/tests/a_test.cpp core/viz/A.cpp -o a_test
          ./a_test
          # a comment: g++ fake -o nope
          g++ -std=c++20 -O1 -Icore core/tests/b_test.cpp core/sound/*.cpp \\
            core/viz/B.cpp \\
            core/viz/C.cpp -o b_test
          ./b_test --flag core/tests/fixture.txt
      - name: Lonely compile
        run: |
          g++ -Icore core/tests/c_test.cpp -o c_test
      - name: One liner
        run: echo hi
      - name: Other block
        run: |
          ./a_test
"""


class ParserTest(unittest.TestCase):
    def test_pairs_compile_and_run_and_joins_continuations(self):
        tests = rm.parse_host_tests(WORKFLOW)
        self.assertEqual([t.name for t in tests], ["a_test", "b_test"])
        b = tests[1]
        self.assertEqual(b.step, "Native host tests")
        self.assertIn("core/viz/C.cpp", b.raw_sources())
        self.assertEqual(b.run_argv, ["./b_test", "--flag", "core/tests/fixture.txt"])
        self.assertEqual(b.flags(), ["-std=c++20", "-O1", "-Icore"])

    def test_include_dirs(self):
        a = rm.parse_host_tests(WORKFLOW)[0]
        self.assertEqual(a.include_dirs(), ["core", "core/tests/stub"])

    def test_compile_without_run_is_dropped(self):
        self.assertNotIn("c_test", [t.name for t in rm.parse_host_tests(WORKFLOW)])

    def test_logical_lines(self):
        # A continuation joins into one command; the exact whitespace between its tokens is
        # irrelevant (commands are split on whitespace), so assert on the tokens.
        lines = rm.logical_lines("a \\\n  b\n# c\n\nd")
        self.assertEqual([ln.split() for ln in lines], [["a", "b"], ["d"]])

    def test_glob_expansion(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "core/sound").mkdir(parents=True)
            for n in ("Z.cpp", "A.cpp"):
                (root / "core/sound" / n).write_text("")
            b = rm.parse_host_tests(WORKFLOW)[1]
            srcs = rm.expand_sources(b, root)
            self.assertEqual(srcs[1:3], ["core/sound/A.cpp", "core/sound/Z.cpp"])

    def test_real_native_host_tests_yml(self):
        """Guards the parser against native-host-tests.yml's real shape (fails loudly if CI's layout changes)."""
        wf = REPO / ".github/workflows/native-host-tests.yml"
        tests = {t.name: t for t in rm.parse_host_tests(wf.read_text(encoding="utf-8"))}
        for name in ("catalogue_test", "params_schema_test", "customize_test", "mod_targets_test"):
            self.assertIn(name, tests)
        self.assertIn("core/viz/Reactions.cpp", tests["mod_targets_test"].raw_sources())
        self.assertIn("core/viz/VisualSafety.cpp", tests["mod_targets_test"].raw_sources())
        self.assertEqual(tests["catalogue_test"].run_argv[1:], ["core/tests/catalogue.txt"])


class DiffTest(unittest.TestCase):
    DIFF = """\
diff --git a/core/viz/A.cpp b/core/viz/A.cpp
--- a/core/viz/A.cpp
+++ b/core/viz/A.cpp
@@ -10,0 +11,2 @@ void f()
+    x = 1;
+    y = 2;
@@ -20 +22 @@
-old
+new
@@ -30,3 +33,0 @@
-gone
diff --git a/core/tests/a_test.cpp b/core/tests/a_test.cpp
--- a/core/tests/a_test.cpp
+++ b/core/tests/a_test.cpp
@@ -1 +1 @@
+t
diff --git a/core/viz/Gone.hpp b/core/viz/Gone.hpp
--- a/core/viz/Gone.hpp
+++ /dev/null
@@ -1 +0,0 @@
-x
"""

    def test_added_lines_only_outside_tests(self):
        self.assertEqual(rm.parse_diff_added_lines(self.DIFF), {"core/viz/A.cpp": {11, 12, 22}})


class IncludeTest(unittest.TestCase):
    def test_transitive_header_reaches_test(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "core/viz").mkdir(parents=True)
            (root / "core/tests").mkdir(parents=True)
            (root / "core/viz/Inner.hpp").write_text("int inner();\n")
            (root / "core/viz/Outer.hpp").write_text('#include "Inner.hpp"\n')
            (root / "core/viz/A.cpp").write_text('#include "viz/Outer.hpp"\n')
            (root / "core/tests/a_test.cpp").write_text("int main(){}\n")
            (root / "core/viz/Other.hpp").write_text("\n")
            tests = rm.parse_host_tests(WORKFLOW)[:1]
            rel = rm.relevant_tests(tests, ["core/viz/Inner.hpp", "core/viz/A.cpp", "core/viz/Other.hpp"], root)
            self.assertEqual([t.name for t in rel["core/viz/Inner.hpp"]], ["a_test"])
            self.assertEqual([t.name for t in rel["core/viz/A.cpp"]], ["a_test"])
            self.assertEqual(rel["core/viz/Other.hpp"], [])


def muts(line):
    return {new.strip() for _, new in rm.mutate_line(line, rm.mask_source(line))}


class MutatorTest(unittest.TestCase):
    def test_relational_boundary_and_equality(self):
        m = muts("if (a < b && c == d) {")
        self.assertIn("if (a <= b && c == d) {", m)
        self.assertIn("if (a < b || c == d) {", m)
        self.assertIn("if (a < b && c != d) {", m)
        self.assertIn("if (!(a < b && c == d)) {", m)

    def test_templates_shifts_and_arrows_untouched(self):
        self.assertEqual(muts("std::vector<int> v; out << x->y >> z;"), set())

    def test_arithmetic(self):
        m = muts("float y = a * b + c;")
        self.assertIn("float y = a / b + c;", m)
        self.assertIn("float y = a * b - c;", m)

    def test_strings_and_comments_are_masked(self):
        self.assertEqual(muts('log("a < b == 3"); // x < y'), set())

    def test_literals(self):
        m = muts("int n = 0; float k = 2.5f; int big = 16;")
        self.assertIn("int n = 1; float k = 2.5f; int big = 16;", m)
        self.assertIn("int n = 0; float k = (2.5f + 1); int big = 16;", m)
        self.assertIn("int n = 0; float k = 2.5f; int big = (16 + 1);", m)
        self.assertEqual(muts("int v = x1 + 0x1F;"), {"int v = x1 - 0x1F;"})

    def test_return_and_clamp(self):
        m = muts("return std::clamp(v, 0.0f, 1.0f);")
        self.assertIn("return {};", m)
        self.assertIn("return (v);", m)
        m = muts("float z = std::max(a, f(b, c));")
        self.assertIn("float z = (a);", m)
        self.assertIn("float z = (f(b, c));", m)

    def test_preprocessor_skipped(self):
        self.assertEqual(muts("#define N 4"), set())

    def test_block_comment_spanning_lines(self):
        text = "/* a < b\n   c == d */ int x = y;\n"
        mask = rm.mask_source(text).split("\n")
        self.assertEqual(rm.mutate_line(text.split("\n")[1], mask[1]), [])

    def test_interleave_samples_every_file(self):
        ms = [rm.Mutant("a", i, "op", "", str(i)) for i in range(3)] + [rm.Mutant("b", 1, "op", "", "b")]
        self.assertEqual([m.file for m in rm.interleave(ms)[:2]], ["a", "b"])


class ReportTest(unittest.TestCase):
    def test_score_and_summary(self):
        a = rm.Mutant("core/viz/A.cpp", 3, "rel_boundary", "  if (a < b)", "  if (a <= b)", "survived")
        b = rm.Mutant("core/viz/A.cpp", 4, "literal", "x = 0;", "x = 1;", "killed")
        c = rm.Mutant("core/viz/A.cpp", 5, "literal", "y = 0;", "y = 1;", "timeout")
        d = rm.Mutant("core/viz/A.cpp", 6, "return_default", "return r;", "return {};", "invalid")
        per = rm.tally([a, b, c, d])["core/viz/A.cpp"]
        self.assertEqual((per["generated"], per["killed"], per["survived"], per["timeout"], per["invalid"]), (4, 1, 1, 1, 1))
        self.assertAlmostEqual(rm.score(per), 200 / 3)
        text = rm.render_summary([a, b, c, d], {"engine": "source", "base": "origin/main", "notes": [], "min_score": None})
        self.assertIn("core/viz/A.cpp:3  if (a < b) → if (a <= b)", text)
        self.assertIn("| `core/viz/A.cpp` | 4 | 1 | 1 | 1 | 1 | 0 |", text)
        self.assertIn("report only", text)

    def test_empty(self):
        self.assertIsNone(rm.score({"killed": 0, "survived": 0, "timeout": 0}))
        text = rm.render_summary([], {"engine": "source", "base": "origin/main", "notes": [], "min_score": 60.0})
        self.assertIn("No changed C++ lines", text)

    def test_elements_report(self):
        data = {"files": {"/r/core/viz/A.cpp": {"source": "a\nif (x < y)\n", "mutants": [
            {"mutatorName": "cxx_lt_to_le", "replacement": "<=", "status": "Survived", "location": {"start": {"line": 2, "column": 7}}}]}}}
        ms = rm.parse_elements_report(data, Path("/r"))
        self.assertEqual((ms[0].file, ms[0].line, ms[0].status, ms[0].original), ("core/viz/A.cpp", 2, "survived", "if (x < y)"))


if __name__ == "__main__":
    unittest.main()
