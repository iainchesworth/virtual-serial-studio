#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for tools/analyze.py.

    python -m unittest discover -s tools -p "test_analyze.py" -v
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import analyze as tool

ROOT = tool.ROOT


class ParseDiagnostics(unittest.TestCase):
    def test_clang_tidy_line(self):
        text = f"{ROOT}/libs/vss_core/src/hub.cpp:12:5: warning: do not use 'x' [bugprone-foo]\n"
        found = tool.parse_diagnostics(text)
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].path, "libs/vss_core/src/hub.cpp")
        self.assertEqual((found[0].line, found[0].col, found[0].rule, found[0].level), (12, 5, "bugprone-foo", "warning"))

    def test_error_severity_maps_to_error(self):
        found = tool.parse_diagnostics(f"{ROOT}/apps/vss_gui/main.cpp:3:1: error: broken [clang-diagnostic-error]")
        self.assertEqual(found[0].level, "error")

    def test_cppcheck_style_severities_are_warnings(self):
        found = tool.parse_diagnostics(f"{ROOT}/tests/unit/a.cpp:9:2: performance: pass by reference [passedByValue]")
        self.assertEqual((found[0].level, found[0].rule), ("warning", "passedByValue"))

    def test_outside_the_repository_is_dropped(self):
        self.assertEqual(tool.parse_diagnostics("C:/Qt/6.12/include/QtCore/qobject.h:1:1: warning: x [bugprone-foo]"), [])

    def test_generated_and_third_party_files_are_dropped(self):
        for rel in ("build/windows-msvc-debug-gui/mocs_compilation.cpp", "docs/x.cpp", ".cache/boost-root/a.hpp"):
            self.assertEqual(tool.parse_diagnostics(f"{ROOT}/{rel}:1:1: warning: x [bugprone-foo]"), [], rel)

    def test_duplicates_collapse(self):
        line = f"{ROOT}/libs/vss_core/src/hub.cpp:12:5: warning: same [bugprone-foo]"
        self.assertEqual(len(tool.parse_diagnostics(line + "\n" + line)), 1)

    def test_notes_and_other_lines_are_ignored(self):
        text = f"{ROOT}/libs/vss_core/src/hub.cpp:12:5: note: see here\nSuppressed 3 warnings\n"
        self.assertEqual(tool.parse_diagnostics(text), [])


class Sarif(unittest.TestCase):
    def test_shape(self):
        sarif = tool.to_sarif("clang-tidy", [tool.Finding("bugprone-foo", "warning", "msg", "libs/a.cpp", 4, 2)])
        self.assertEqual(sarif["version"], "2.1.0")
        run = sarif["runs"][0]
        self.assertEqual(run["tool"]["driver"]["name"], "clang-tidy")
        result = run["results"][0]
        self.assertEqual(result["ruleId"], "bugprone-foo")
        region = result["locations"][0]["physicalLocation"]["region"]
        self.assertEqual((region["startLine"], region["startColumn"]), (4, 2))

    def test_empty_run_is_valid(self):
        run = tool.to_sarif("cppcheck", [])["runs"][0]
        self.assertEqual(run["results"], [])

    def test_write_creates_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = tool.write_sarif(Path(tmp) / "a" / "b", "x", "x", [])
            self.assertEqual(json.loads(target.read_text(encoding="utf-8"))["version"], "2.1.0")


class SourceFiles(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.build = Path(self.tmp.name)
        entries = [{"directory": str(ROOT), "file": rel, "command": "cl"} for rel in (
            "libs/vss_core/src/hub.cpp", "libs/vss_core/src/endpoint.cpp", "apps/vss_gui/main.cpp",
            "tests/unit/test_hub.cpp", "build/x/mocs_compilation.cpp")]
        (self.build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def test_all_first_party_units_and_not_generated_ones(self):
        files = tool.source_files(self.build, None)
        self.assertEqual(len(files), 4)
        self.assertNotIn("build/x/mocs_compilation.cpp", files)

    def test_changed_source_file(self):
        self.assertEqual(tool.source_files(self.build, ["apps/vss_gui/main.cpp", "README.md"]), ["apps/vss_gui/main.cpp"])

    def test_changed_header_selects_its_neighbours(self):
        files = tool.source_files(self.build, ["libs/vss_core/src/hub.hpp"])
        self.assertEqual(files, ["libs/vss_core/src/endpoint.cpp", "libs/vss_core/src/hub.cpp"])

    def test_nothing_changed(self):
        self.assertEqual(tool.source_files(self.build, ["README.md"]), [])


class CheckSet(unittest.TestCase):
    def test_check_set_is_well_formed(self):
        self.assertTrue(tool.CLANG_TIDY_CHECKS.startswith("-*,"))
        self.assertNotIn(" ", tool.CLANG_TIDY_CHECKS)
        self.assertIn("readability-identifier-naming", tool.CLANG_TIDY_CHECKS)


if __name__ == "__main__":
    unittest.main()
