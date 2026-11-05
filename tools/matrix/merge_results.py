#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Merge the result-<cell>.json files of a matrix run into one table.

    merge_results.py results/ [--check]

It prints a Markdown table (one row per cell, one column per test) followed by the
detail of every test that did not pass. With --check the exit status is 1 when any
applicable test failed or was left unrun, or when no result file was found.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SYMBOL = {"pass": "pass", "fail": "FAIL", "skip": "-", "not_run": "NOT RUN"}
ORDER = [f"T{n}" for n in range(1, 12)]


def load(directory: Path) -> list[dict]:
    results = []
    for path in sorted(directory.rglob("result-*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            results.append({"cell": path.stem, "error": f"cannot read {path.name}: {error}", "results": []})
            continue
        if data.get("schema") != 1:
            results.append({"cell": data.get("cell", path.stem), "error": "unknown schema", "results": []})
            continue
        results.append(data)
    return results


def problems(cell: dict) -> list[dict]:
    if cell.get("error"):
        return [{"id": "-", "status": "fail", "detail": cell["error"]}]
    return [r for r in cell["results"] if r["status"] in ("fail", "not_run")]


def render(cells: list[dict]) -> str:
    lines = ["| Cell | Commit | " + " | ".join(ORDER) + " |", "|---|---|" + "---|" * len(ORDER)]
    for cell in cells:
        by_id = {r["id"]: r["status"] for r in cell.get("results", [])}
        row = [SYMBOL.get(by_id.get(test, ""), "") for test in ORDER]
        commit = (cell.get("commit") or "")[:7]
        lines.append(f"| {cell['cell']} | {commit} | " + " | ".join(row) + " |")
    lines.append("")
    for cell in cells:
        bad = problems(cell)
        if not bad:
            continue
        lines.append(f"**{cell['cell']}**")
        for entry in bad:
            lines.append(f"- {entry['id']} {SYMBOL[entry['status']]}: {entry['detail']}")
        lines.append("")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)

    cells = load(args.directory)
    if not cells:
        print(f"no result files under {args.directory}", file=sys.stderr)
        return 1
    print(render(cells))
    if args.check and any(problems(cell) for cell in cells):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
