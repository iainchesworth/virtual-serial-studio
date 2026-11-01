#!/usr/bin/env python3
# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render and gate performance results.

Usage:
    vss_perf --output=perf.json
    python tools/perf_report.py perf.json --floors perf/floors.json

The report is a Markdown table (for a CI job summary) and the exit code is
non-zero when any measurement falls below its floor.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load(path: Path) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise SystemExit(f"{path}: expected a list of measurements")
    return data


def render(results: list[dict]) -> str:
    lines = [
        "| Benchmark | Value | Unit | Note |",
        "|---|---:|---|---|",
    ]
    for entry in results:
        lines.append(
            f"| {entry['name']} | {entry['value']:.1f} | {entry['unit']} | "
            f"{entry.get('note', '')} |"
        )
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", nargs="+", type=Path)
    parser.add_argument("--floors", type=Path)
    parser.add_argument("--markdown", action="store_true",
                        help="print a Markdown table instead of plain text")
    args = parser.parse_args(argv)

    merged: dict[str, dict] = {}
    for path in args.results:
        for entry in load(path):
            merged.setdefault(entry["name"], entry)

    results = list(merged.values())
    print(render(results) if args.markdown else "\n".join(
        f"{entry['name']:<20} {entry['value']:>12.1f} {entry['unit']}"
        for entry in results
    ))

    if not args.floors:
        return 0

    floors = json.loads(args.floors.read_text(encoding="utf-8"))
    failures = 0
    for floor in floors.get("floors", []):
        entry = merged.get(floor["name"])
        if entry is None:
            print(f"MISSING  {floor['name']} (floor {floor['min']} {floor['unit']})",
                  file=sys.stderr)
            failures += 1
            continue
        if entry["value"] < floor["min"]:
            print(
                f"FAIL     {floor['name']}: {entry['value']:.1f} "
                f"{entry['unit']} < {floor['min']} {floor['unit']}",
                file=sys.stderr,
            )
            failures += 1
        else:
            print(f"ok       {floor['name']}: {entry['value']:.1f} "
                  f"{entry['unit']} >= {floor['min']}")

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
