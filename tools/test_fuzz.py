#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for tools/fuzz.py.

    python -m unittest discover -s tools -p "test_fuzz.py" -v
"""

from __future__ import annotations

import unittest
from pathlib import Path

import fuzz


class HarnessDiscovery(unittest.TestCase):
    def test_every_harness_has_a_corpus_and_regressions_directory(self):
        sources = fuzz.harnesses()
        self.assertGreaterEqual(len(sources), 2)
        for source in sources:
            self.assertTrue((source.parent / "corpus").is_dir(), source)
            self.assertTrue((source.parent / "regressions").is_dir(), source)
            self.assertTrue(any((source.parent / "corpus").iterdir()), source)

    def test_every_harness_is_declared_in_cmake(self):
        declared = (fuzz.ROOT / "tests" / "fuzz" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        for source in fuzz.harnesses():
            self.assertIn(source.name, declared)


class CommandLines(unittest.TestCase):
    harness = Path("libs/x/fuzz/fuzz_thing.cpp")
    build = Path("build/linux-fuzz")
    work = Path(".cache/fuzz")

    def test_regress_replays_once_and_writes_nothing(self):
        line = fuzz.command(self.harness, self.build, "regress", self.work, 0)
        self.assertEqual(line[1], "-runs=0")
        self.assertNotIn(str(self.work / "fuzz_thing"), line)
        self.assertEqual(Path(line[0]).name, "fuzz_thing")

    def test_fuzzing_writes_new_inputs_to_the_working_corpus_first(self):
        line = fuzz.command(self.harness, self.build, "short", self.work, 60)
        self.assertIn("-max_total_time=60", line)
        first_directory = [word for word in line if not word.startswith("-")][1]
        self.assertEqual(Path(first_directory), self.work / "fuzz_thing")

    def test_the_times_are_the_standards(self):
        self.assertEqual(fuzz.SECONDS, {"short": 60, "nightly": 600})


if __name__ == "__main__":
    unittest.main()
