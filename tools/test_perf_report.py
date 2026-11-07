#!/usr/bin/env python3
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Unit tests for tools/perf_report.py.

Run with:

    python -m unittest discover -s tools -p "test_*.py" -v
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import perf_report

RESULTS = [
    {"name": "hub_route", "value": 100.0, "unit": "MB/s", "note": ""},
    {"name": "zone_dormant", "value": 2.5, "unit": "ns/zone", "note": ""},
]

FLOORS = {
    "floors": [
        {"name": "hub_route", "min": 20.0, "unit": "MB/s"},
        {"name": "zone_dormant", "max": 15.0, "unit": "ns/zone"},
        {"name": "absent", "min": 1.0, "unit": "ops/s"},
    ]
}


class perf_report_test(unittest.TestCase):
    def setUp(self) -> None:
        self._directory = tempfile.TemporaryDirectory()
        self.root = Path(self._directory.name)
        self.results = self.root / "results.json"
        self.floors = self.root / "floors.json"
        self.floors.write_text(json.dumps(FLOORS), encoding="utf-8")
        self.write(RESULTS)

    def tearDown(self) -> None:
        self._directory.cleanup()

    def write(self, results: list[dict]) -> None:
        self.results.write_text(json.dumps(results), encoding="utf-8")

    def run_report(self, *extra: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = perf_report.main(
                [str(self.results), "--floors", str(self.floors), *extra]
            )
        return code, out.getvalue(), err.getvalue()

    def test_a_missing_measurement_fails(self) -> None:
        code, _, err = self.run_report()
        self.assertEqual(code, 1)
        self.assertIn("MISSING  absent", err)

    def test_only_limits_the_gate_to_the_named_measurements(self) -> None:
        code, out, _ = self.run_report("--only", "zone_dormant")
        self.assertEqual(code, 0)
        self.assertIn("zone_dormant", out)
        self.assertNotIn("hub_route", out)

    def test_a_cost_above_its_max_fails(self) -> None:
        self.write([{"name": "zone_dormant", "value": 40.0, "unit": "ns/zone"}])
        code, _, err = self.run_report("--only", "zone_dormant")
        self.assertEqual(code, 1)
        self.assertIn("FAIL     zone_dormant", err)

    def test_a_rate_below_its_min_fails(self) -> None:
        self.write([{"name": "hub_route", "value": 5.0, "unit": "MB/s"}])
        code, _, err = self.run_report("--only", "hub_route")
        self.assertEqual(code, 1)
        self.assertIn("FAIL     hub_route", err)


if __name__ == "__main__":
    unittest.main()
