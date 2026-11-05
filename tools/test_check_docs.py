#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for tools/check_docs.py.

    python -m unittest discover -s tools -p "test_check_docs.py" -v
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import check_docs as tool

PRESETS = {
    "configurePresets": [{"name": "linux-debug"}, {"name": "linux-debug-gui"}],
    "buildPresets": [{"name": "linux-debug"}],
}


def tree(readme: str, building: str) -> Path:
    root = Path(tempfile.mkdtemp())
    (root / "docs").mkdir()
    (root / ".github" / "workflows").mkdir(parents=True)
    (root / ".github" / "workflows" / "ci.yml").write_text("name: CI\n", encoding="utf-8")
    (root / "CMakePresets.json").write_text(json.dumps(PRESETS), encoding="utf-8")
    (root / "README.md").write_text(readme, encoding="utf-8")
    (root / "docs" / "building.md").write_text(building, encoding="utf-8")
    return root


class Presets(unittest.TestCase):
    def test_a_known_preset_passes(self):
        root = tree("", "cmake --preset linux-debug\ncmake --build --preset linux-debug-gui\n")
        self.assertEqual(tool.check_presets(root), [])

    def test_an_unknown_preset_in_a_command_is_named(self):
        root = tree("", "cmake --preset linux-releese\n")
        problems = tool.check_presets(root)
        self.assertEqual(len(problems), 1)
        self.assertIn("linux-releese", problems[0])

    def test_the_preset_table_is_checked(self):
        root = tree("", "| Preset | Builds |\n|---|---|\n| `linux-debug`, `linux-gone` | tests |\n| `VSS_TRACY` | an option |\n")
        problems = tool.check_presets(root)
        self.assertEqual(len(problems), 1)
        self.assertIn("linux-gone", problems[0])


class Badges(unittest.TestCase):
    def test_a_badge_for_an_existing_workflow_passes(self):
        root = tree("[![CI](https://github.com/o/r/actions/workflows/ci.yml/badge.svg)](x)\n", "")
        self.assertEqual(tool.check_badges(root), [])

    def test_a_badge_for_a_missing_workflow_is_named(self):
        root = tree("[![X](https://github.com/o/r/actions/workflows/gone.yml/badge.svg)](x)\n", "")
        problems = tool.check_badges(root)
        self.assertEqual(len(problems), 1)
        self.assertIn("gone.yml", problems[0])


class ThisRepository(unittest.TestCase):
    def test_the_documentation_matches_the_repository(self):
        self.assertEqual(tool.check_presets() + tool.check_badges(), [])


if __name__ == "__main__":
    unittest.main()
