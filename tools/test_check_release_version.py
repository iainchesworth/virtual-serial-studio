#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for tools/check_release_version.py.

    python -m unittest discover -s tools -p "test_check_release_version.py" -v
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import check_release_version as tool


def tree(cmake_version: str, manifest: dict) -> Path:
    root = Path(tempfile.mkdtemp())
    (root / "CMakeLists.txt").write_text(
        f"cmake_minimum_required(VERSION 3.21)\nproject(demo\n  VERSION {cmake_version}\n  LANGUAGES CXX)\n",
        encoding="utf-8",
    )
    (root / "vcpkg.json").write_text(json.dumps(manifest), encoding="utf-8")
    return root


class Tags(unittest.TestCase):
    def test_a_matching_tag_passes(self):
        root = tree("1.2.3", {"version-string": "1.2.3"})
        self.assertEqual(tool.check("v1.2.3", root), [])

    def test_a_release_candidate_matches_its_version(self):
        root = tree("1.2.3", {"version": "1.2.3"})
        self.assertEqual(tool.check("v1.2.3-rc.1", root), [])

    def test_a_tag_that_is_not_semver_is_refused(self):
        root = tree("1.2.3", {"version": "1.2.3"})
        self.assertEqual(len(tool.check("1.2.3", root)), 1)
        self.assertEqual(len(tool.check("v1.2", root)), 1)

    def test_each_file_that_disagrees_is_named(self):
        root = tree("1.2.3", {"version-string": "1.2.2"})
        problems = tool.check("v1.2.3", root)
        self.assertEqual(len(problems), 1)
        self.assertIn("vcpkg.json", problems[0])

        root = tree("1.2.2", {"version-string": "1.2.2"})
        self.assertEqual(len(tool.check("v1.2.3", root)), 2)

    def test_this_repository_is_consistent(self):
        versions = tool.declared_versions()
        self.assertEqual(len(set(versions.values())), 1, versions)


if __name__ == "__main__":
    unittest.main()
