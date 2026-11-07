#!/usr/bin/env python3
# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn the UI suite's per-journey CSV files into a Markdown table.

``vss_gui_ui_tests`` appends one line per journey to the file named by
``VSS_UI_PERF_REPORT``: journey, outcome, milliseconds, steps, the longest wall
time and the most CPU time of the GUI thread in one step (ms), the budget the
journey was held to, and the step of the CPU peak. CTest gives each group its
own file under ``<build>/ui-test-perf``. This prints them together, the most
CPU-hungry first, with the totals, for the job summary:

    python3 tools/ci/ui_perf_summary.py build/linux-release-gui/ui-test-perf
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path


def read_rows(directory: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in sorted(directory.glob("*.csv")):
        with path.open(newline="", encoding="utf-8") as handle:
            for record in csv.reader(handle):
                if len(record) != 8:
                    continue
                rows.append(
                    {
                        "group": path.stem,
                        "journey": record[0],
                        "outcome": record[1],
                        "ms": record[2],
                        "steps": record[3],
                        "wall": record[4],
                        "cpu": record[5],
                        "budget": record[6],
                        "step": record[7],
                    }
                )
    return rows


def render(rows: list[dict[str, str]]) -> str:
    if not rows:
        return "### UI journey timings\n\nNo journey wrote a timing line.\n"
    rows = sorted(rows, key=lambda row: int(row["cpu"]), reverse=True)
    total = sum(int(row["ms"]) for row in rows)
    lines = [
        "### UI journey timings",
        "",
        f"{len(rows)} journeys, {total / 1000.0:.1f} s in all. CPU is the most the "
        "GUI thread computed inside one step; it is held to the budget.",
        "",
        "| Group | Journey | Result | Time (ms) | Steps | Wall (ms) | CPU (ms) | Budget (ms) | CPU peak in step |",
        "|---|---|---|---:|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        lines.append(
            f"| {row['group']} | {row['journey']} | {row['outcome']} | {row['ms']} | "
            f"{row['steps']} | {row['wall']} | {row['cpu']} | {row['budget']} | "
            f"{row['step']} |"
        )
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    directory = Path(argv[0])
    if not directory.is_dir():
        print(f"no timing directory at {directory}", file=sys.stderr)
        return 1
    sys.stdout.write(render(read_rows(directory)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
