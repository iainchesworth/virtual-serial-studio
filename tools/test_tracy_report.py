#!/usr/bin/env python3
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Unit tests for tools/tracy_report.py.

Run with:

    python -m unittest discover -s tools -p "test_*.py" -v
"""

from __future__ import annotations

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tracy_report

HEADER = (
    "name,src_file,src_line,total_ns,total_perc,counts,mean_ns,min_ns,max_ns,std_ns"
)

CSV = "\n".join(
    [
        HEADER,
        "perf.e2e.tcp,perf_main.cpp,354,26851137,3.965384,1,26851137,26851137,26851137,0.0",
        "hub.route,hub.cpp,427,14465447,2.136262,8448,1712,30,99329,8339.9",
        "endpoint.read,endpoint.cpp,155,2879591,0.425259,257,11204,50,75409,7638.3",
    ]
)

REQUIRED = "perf.e2e.tcp,hub.route,endpoint.read"


class tracy_report_test(unittest.TestCase):
    def setUp(self) -> None:
        self._directory = tempfile.TemporaryDirectory()
        self.root = Path(self._directory.name)
        self.csv = self.root / "trace.csv"
        self.csv.write_text(CSV + "\n", encoding="utf-8")

    def tearDown(self) -> None:
        self._directory.cleanup()

    def test_loads_every_zone(self) -> None:
        zones = tracy_report.load(self.csv)
        self.assertEqual([zone.name for zone in zones],
                         ["perf.e2e.tcp", "hub.route", "endpoint.read"])
        self.assertEqual(zones[0].counts, 1)
        self.assertEqual(zones[1].mean_ns, 1712)

    def test_table_is_sorted_by_total_time(self) -> None:
        zones = tracy_report.load(self.csv)
        table = tracy_report.render_markdown(zones, 2)
        self.assertLess(table.index("perf.e2e.tcp"), table.index("hub.route"))
        self.assertNotIn("endpoint.read", table)
        self.assertIn("3.97%", table)

    def test_required_zones_are_checked(self) -> None:
        zones = tracy_report.load(self.csv)
        self.assertEqual(
            tracy_report.missing_required(zones, ["perf.e2e.tcp", "absent"]),
            ["absent"],
        )

    def test_main_writes_markdown_and_json(self) -> None:
        markdown = self.root / "report.md"
        as_json = self.root / "report.json"
        code = tracy_report.main(
            [
                str(self.csv),
                "--require",
                REQUIRED,
                "--markdown",
                str(markdown),
                "--json",
                str(as_json),
            ]
        )
        self.assertEqual(code, 0)
        self.assertIn("perf.e2e.tcp", markdown.read_text(encoding="utf-8"))
        payload = json.loads(as_json.read_text(encoding="utf-8"))
        self.assertEqual(payload[0]["name"], "perf.e2e.tcp")

    def test_missing_required_zone_fails(self) -> None:
        code = tracy_report.main([str(self.csv), "--require", "nowhere"])
        self.assertEqual(code, 1)
        self.assertEqual(
            tracy_report.main([str(self.csv), "--require", REQUIRED]), 0
        )

    def test_bad_header_is_rejected(self) -> None:
        broken = self.root / "broken.csv"
        broken.write_text("alpha,beta\n1,2\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            tracy_report.load(broken)
        self.assertEqual(tracy_report.main([str(broken)]), 2)

    def test_empty_file_is_rejected(self) -> None:
        empty = self.root / "empty.csv"
        empty.write_text("", encoding="utf-8")
        self.assertEqual(tracy_report.main([str(empty)]), 2)

    def test_default_required_list_covers_the_pipeline(self) -> None:
        required = tracy_report.DEFAULT_REQUIRED.split(",")
        self.assertIn("perf.e2e.tcp", required)
        self.assertIn("hub.route", required)


if __name__ == "__main__":
    unittest.main()
