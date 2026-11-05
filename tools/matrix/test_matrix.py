#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for run_cell.py and merge_results.py.

    python3 -m unittest discover -s tools/matrix -p "test_*.py" -v

They need no built program: a cell with nothing built must report not_run, never pass.
"""

from __future__ import annotations

import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import merge_results
import run_cell


def cell(name: str, statuses: dict[str, str], commit: str = "abcdef0123") -> dict:
    return {
        "schema": 1,
        "cell": name,
        "kind": "hosted",
        "commit": commit,
        "results": [{"id": test, "status": status, "detail": f"{test} detail"} for test, status in statuses.items()],
    }


class RunCellTests(unittest.TestCase):
    def run_empty(self, extra: list[str]) -> tuple[int, dict]:
        with tempfile.TemporaryDirectory() as scratch:
            build, out = Path(scratch, "build"), Path(scratch, "out")
            build.mkdir()
            with redirect_stdout(io.StringIO()):
                code = run_cell.main(["--cell", "empty", "--build-dir", str(build), "--out", str(out)] + extra)
            return code, json.loads((out / "result-empty.json").read_text())

    def test_nothing_built_means_not_run_and_a_failing_exit(self) -> None:
        code, result = self.run_empty(["--t1-exit", "0"])
        by_id = {r["id"]: r["status"] for r in result["results"]}
        self.assertEqual(by_id["T1"], "pass")
        for test in ("T4", "T5", "T6"):
            self.assertEqual(by_id[test], "not_run", test)
        self.assertEqual(code, 1)

    def test_every_test_id_is_reported_once_and_in_order(self) -> None:
        _, result = self.run_empty([])
        ids = [r["id"] for r in result["results"]]
        self.assertEqual(ids, [f"T{n}" for n in range(1, 12)])
        self.assertEqual(result["schema"], 1)

    def test_a_missing_ctest_result_is_not_a_pass(self) -> None:
        _, result = self.run_empty([])
        self.assertEqual(result["results"][0]["status"], "not_run")

    def test_a_failed_ctest_is_a_fail(self) -> None:
        _, result = self.run_empty(["--t1-exit", "3"])
        self.assertEqual(result["results"][0]["status"], "fail")


class MergeTests(unittest.TestCase):
    def write(self, directory: Path, data: dict) -> None:
        (directory / f"result-{data['cell']}.json").write_text(json.dumps(data))

    def test_table_and_check(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            directory = Path(scratch)
            self.write(directory, cell("green", {"T1": "pass", "T2": "skip"}))
            self.write(directory, cell("red", {"T1": "pass", "T2": "fail", "T3": "not_run"}))
            cells = merge_results.load(directory)
            table = merge_results.render(cells)
            self.assertIn("| green | abcdef0 | pass | - |", table)
            self.assertIn("FAIL", table)
            self.assertIn("NOT RUN", table)
            self.assertIn("**red**", table)
            self.assertNotIn("**green**", table)
            with redirect_stdout(io.StringIO()):
                self.assertEqual(merge_results.main([scratch, "--check"]), 1)
                self.assertEqual(merge_results.main([scratch]), 0)

    def test_all_green_passes_the_check(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            self.write(Path(scratch), cell("green", {"T1": "pass", "T2": "skip"}))
            with redirect_stdout(io.StringIO()):
                self.assertEqual(merge_results.main([scratch, "--check"]), 0)

    def test_no_files_fails_the_check(self) -> None:
        with tempfile.TemporaryDirectory() as scratch, redirect_stdout(io.StringIO()):
            self.assertEqual(merge_results.main([scratch, "--check"]), 1)

    def test_a_bad_file_is_a_problem_not_a_crash(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            (Path(scratch) / "result-broken.json").write_text("{not json")
            cells = merge_results.load(Path(scratch))
            self.assertTrue(merge_results.problems(cells[0]))


class LineReaderTests(unittest.TestCase):
    def test_finds_a_line_that_arrives_later(self) -> None:
        read, write = io.StringIO("first\nopen this from your application: /dev/pts/7\n"), None
        reader = run_cell.LineReader(read)
        found = reader.wait_for(r"application: (\S+)", 5)
        self.assertIsNotNone(found)
        self.assertEqual(found.group(1), "/dev/pts/7")

    def test_times_out_when_the_line_never_comes(self) -> None:
        reader = run_cell.LineReader(io.StringIO("nothing useful\n"))
        self.assertIsNone(reader.wait_for(r"never", 0.5))


if __name__ == "__main__":
    unittest.main()
