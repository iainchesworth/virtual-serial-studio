#!/usr/bin/env python3
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Summarise a Tracy capture exported with tracy-csvexport.

The CSV has one row per zone name (already aggregated by the exporter):

    name,src_file,src_line,total_ns,total_perc,counts,mean_ns,min_ns,max_ns,std_ns

The report is written as Markdown so CI can paste it into the job summary,
and optionally as JSON for anything that wants to chart it.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass
from pathlib import Path

REQUIRED_COLUMNS = {
    "name",
    "src_file",
    "src_line",
    "total_ns",
    "total_perc",
    "counts",
    "mean_ns",
    "min_ns",
    "max_ns",
}

# Zones the harness always produces. A missing one means the instrumentation
# or the workload regressed, which is worth failing over.
DEFAULT_REQUIRED = "perf.e2e.tcp,perf.route,hub.route,endpoint.read,config.to_json"


@dataclass(frozen=True)
class zone:
    name: str
    source: str
    line: int
    total_ns: int
    share: float
    counts: int
    mean_ns: int
    min_ns: int
    max_ns: int

    @property
    def total_ms(self) -> float:
        return self.total_ns / 1_000_000.0

    @property
    def mean_us(self) -> float:
        return self.mean_ns / 1_000.0

    @property
    def max_us(self) -> float:
        return self.max_ns / 1_000.0

    def as_json(self) -> dict:
        return {
            "name": self.name,
            "source": self.source,
            "line": self.line,
            "total_ns": self.total_ns,
            "share": self.share,
            "counts": self.counts,
            "mean_ns": self.mean_ns,
            "min_ns": self.min_ns,
            "max_ns": self.max_ns,
        }


def _int(row: dict, key: str) -> int:
    value = (row.get(key) or "").strip()
    return int(value) if value else 0


def _float(row: dict, key: str) -> float:
    value = (row.get(key) or "").strip()
    return float(value) if value else 0.0


def load(path: Path) -> list[zone]:
    """Read a tracy-csvexport file.

    Raises ValueError when the file is missing the expected shape; the caller
    turns that into a failed step rather than a traceback.
    """
    try:
        handle = path.open(newline="", encoding="utf-8")
    except OSError as error:
        raise ValueError(f"cannot open {path}: {error}") from error

    with handle:
        reader = csv.reader(handle)
        header = next(reader, None)
        if header is None:
            raise ValueError(f"{path} is empty")
        missing = REQUIRED_COLUMNS - set(header)
        if missing:
            raise ValueError(
                f"{path} is not a tracy-csvexport file; missing "
                f"{', '.join(sorted(missing))}"
            )

        zones = []
        for cells in reader:
            # tracy-csvexport does not quote a name that contains a comma (a
            # test case called "driver client: a, b" does), so the name is
            # everything before the source file and the numeric columns,
            # which always follow it.
            extra = len(cells) - len(header)
            if extra < 0:
                continue
            if extra > 0:
                cells = [",".join(cells[: extra + 1]), *cells[extra + 1 :]]
            row = dict(zip(header, cells))
            name = (row.get("name") or "").strip()
            if not name:
                continue
            zones.append(
                zone(
                    name=name,
                    source=(row.get("src_file") or "").strip(),
                    line=_int(row, "src_line"),
                    total_ns=_int(row, "total_ns"),
                    share=_float(row, "total_perc"),
                    counts=_int(row, "counts"),
                    mean_ns=_int(row, "mean_ns"),
                    min_ns=_int(row, "min_ns"),
                    max_ns=_int(row, "max_ns"),
                )
            )
    return zones


def render_markdown(zones: list[zone], top: int) -> str:
    ordered = sorted(zones, key=lambda item: item.total_ns, reverse=True)[:top]
    lines = [
        "| Zone | Calls | Total (ms) | Mean (us) | Max (us) | Share |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for item in ordered:
        lines.append(
            f"| `{item.name}` | {item.counts} | {item.total_ms:.2f} | "
            f"{item.mean_us:.1f} | {item.max_us:.1f} | {item.share:.2f}% |"
        )
    lines.append("")
    lines.append(
        f"_{len(zones)} zones captured, {len(ordered)} shown; "
        "share is the fraction of the captured trace._"
    )
    return "\n".join(lines)


def missing_required(zones: list[zone], required: list[str]) -> list[str]:
    """Required names with no zone. `area.*` is met by any zone in the area."""
    present = {item.name for item in zones}
    areas = {area_of(item.name) for item in zones}
    missing = []
    for name in required:
        if not name:
            continue
        if name.endswith(".*"):
            if name[:-2] not in areas:
                missing.append(name)
        elif name not in present:
            missing.append(name)
    return missing


def area_of(name: str) -> str:
    """The first word of an `area.thing` zone name.

    A test case is a zone named after the test, in words ("reconnect: it gives
    up"); those all belong to the `test` area.
    """
    if " " in name:
        return "test"
    return name.split(".", 1)[0]


def by_area(zones: list[zone]) -> list[tuple[str, int, int, int]]:
    """(area, zones, calls, total_ns) for every area, hottest first."""
    totals: dict[str, list[int]] = {}
    for item in zones:
        entry = totals.setdefault(area_of(item.name), [0, 0, 0])
        entry[0] += 1
        entry[1] += item.counts
        entry[2] += item.total_ns
    rows = [(area, *values) for area, values in totals.items()]
    return sorted(rows, key=lambda row: row[3], reverse=True)


def render_areas(zones: list[zone]) -> str:
    lines = [
        "| Area | Zones | Calls | Total (ms) |",
        "| --- | ---: | ---: | ---: |",
    ]
    for area, count, calls, total_ns in by_area(zones):
        lines.append(f"| `{area}` | {count} | {calls} | {total_ns / 1e6:.2f} |")
    return "\n".join(lines)


def load_profile(path: Path, name: str) -> list[str]:
    """Zones and areas a named profile requires, from a JSON file.

    The file maps a profile name to {"zones": [...], "areas": [...]}; an area
    is satisfied by any zone whose name starts with `area.`.
    """
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error
    profile = data.get(name)
    if profile is None:
        raise ValueError(f"{path} has no profile named {name}")
    return [*profile.get("zones", []), *[f"{area}.*" for area in profile.get("areas", [])]]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="trace exported by tracy-csvexport")
    parser.add_argument(
        "--require",
        default=None,
        help="comma separated zones that must be present; `area.*` asks for "
        "any zone of an area (default: a small set for vss_perf)",
    )
    parser.add_argument(
        "--profile",
        help="also require everything the named profile in --profiles lists",
    )
    parser.add_argument(
        "--profiles",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "perf" / "required-zones.json",
        help="JSON file of required zones per profile",
    )
    parser.add_argument(
        "--areas",
        action="store_true",
        help="also print the totals per area (the text before the first dot)",
    )
    parser.add_argument("--markdown", type=Path, help="write the table here too")
    parser.add_argument("--json", type=Path, help="write the raw zones here")
    parser.add_argument("--top", type=int, default=20, help="rows in the table")
    arguments = parser.parse_args(argv)

    try:
        zones = load(arguments.csv)
    except ValueError as error:
        print(f"tracy_report: {error}", file=sys.stderr)
        return 2

    if not zones:
        print(f"tracy_report: {arguments.csv} has no zones", file=sys.stderr)
        return 2

    required_text = arguments.require
    if required_text is None:
        required_text = "" if arguments.profile else DEFAULT_REQUIRED
    required = [name.strip() for name in required_text.split(",") if name.strip()]
    if arguments.profile:
        try:
            required += load_profile(arguments.profiles, arguments.profile)
        except ValueError as error:
            print(f"tracy_report: {error}", file=sys.stderr)
            return 2
    missing = missing_required(zones, required)
    if missing:
        print(
            "tracy_report: required zones missing: " + ", ".join(missing),
            file=sys.stderr,
        )
        return 1

    table = render_markdown(zones, max(1, arguments.top))
    if arguments.areas:
        table += "\n\n" + render_areas(zones)
    print(table)

    if arguments.markdown:
        arguments.markdown.write_text(table + "\n", encoding="utf-8")
    if arguments.json:
        arguments.json.write_text(
            json.dumps([item.as_json() for item in zones], indent=2) + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
