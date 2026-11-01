#!/usr/bin/env python3
# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Merge LLVM profiles, export an lcov report and enforce coverage gates.

Designed for the LLVM profile instrumentation used by -DVSS_COVERAGE=ON:
Clang, clang-cl and AppleClang all write ``.profraw`` files, which are merged
into one ``.profdata`` and reported from the instrumented binaries.

Usage:
    python tools/coverage_report.py --build build-cov \
        --objects build-cov/bin/vss_unit_tests.exe \
        --min-lines 90 --min-branches 80
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SF_RE = re.compile(r"^SF:(?P<path>.*)$")
DA_RE = re.compile(r"^DA:(?P<line>\d+),(?P<count>\d+)")
BRF_RE = re.compile(r"^BRF:(?P<n>\d+)$")
BRH_RE = re.compile(r"^BRH:(?P<n>\d+)$")
LF_RE = re.compile(r"^LF:(?P<n>\d+)$")
LH_RE = re.compile(r"^LH:(?P<n>\d+)$")

DEFAULT_TOOLS = [
    Path(r"C:\Program Files\LLVM\bin"),
    Path("/usr/lib/llvm-18/bin"),
    Path("/usr/local/opt/llvm/bin"),
    Path("/opt/homebrew/opt/llvm/bin"),
]


def find_tool(name: str) -> str:
    found = shutil.which(name)
    if found:
        return found
    suffix = ".exe" if os.name == "nt" else ""
    for directory in DEFAULT_TOOLS:
        candidate = directory / f"{name}{suffix}"
        if candidate.exists():
            return str(candidate)
    raise SystemExit(f"cannot find {name}; install LLVM or put it on PATH")


def merge_profiles(profiles: list[Path], profdata: Path) -> None:
    command = [find_tool("llvm-profdata"), "merge", "-sparse"]
    command += [str(path) for path in profiles]
    command += ["-o", str(profdata)]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)


def export_lcov(objects: list[Path], profdata: Path) -> str:
    command = [find_tool("llvm-cov"), "export", "-format=lcov",
               f"-instr-profile={profdata}"]
    command += [str(path) for path in objects]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    return result.stdout


def parse_lcov(text: str) -> dict[str, dict[str, int]]:
    files: dict[str, dict[str, int]] = {}
    current: dict[str, int] | None = None

    for line in text.splitlines():
        match = SF_RE.match(line)
        if match:
            current = {"lines": 0, "hit": 0, "branches": 0, "branch_hit": 0}
            files[match.group("path")] = current
            continue
        if current is None:
            continue
        if match := DA_RE.match(line):
            current["lines"] += 1
            if int(match.group("count")) > 0:
                current["hit"] += 1
        elif match := BRF_RE.match(line):
            current["branches"] = int(match.group("n"))
        elif match := BRH_RE.match(line):
            current["branch_hit"] = int(match.group("n"))
        elif match := LF_RE.match(line):
            current["lines"] = max(current["lines"], int(match.group("n")))
        elif match := LH_RE.match(line):
            current["hit"] = int(match.group("n"))

    return files


def percent(hit: int, total: int) -> float:
    return 100.0 * hit / total if total else 100.0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-cov"))
    parser.add_argument("--profiles", type=Path,
                        help="directory holding .profraw files "
                             "(default: <build>/profiles)")
    parser.add_argument("--objects", nargs="+", type=Path,
                        help="instrumented executables or shared libraries")
    parser.add_argument("--include", default="libs/",
                        help="only report files whose path contains this "
                             "(default: libs/)")
    parser.add_argument("--exclude", action="append", default=[],
                        help="drop files whose path contains this (repeatable)")
    parser.add_argument("--gate-exclude", action="append", default=[],
                        help="keep the file in the report but leave it out of "
                             "the gated totals (repeatable)")
    parser.add_argument("--min-lines", type=float, default=90.0)
    parser.add_argument("--min-branches", type=float, default=80.0)
    parser.add_argument("--keep", action="store_true",
                        help="keep the merged .profdata next to the build")
    parser.add_argument("--markdown", type=Path,
                        help="also write a Markdown report to this path")
    args = parser.parse_args(argv)

    profile_dir = args.profiles or (args.build / "profiles")
    profiles = [Path(path) for path in glob.glob(str(profile_dir / "*.profraw"))]
    if not profiles:
        raise SystemExit(f"no .profraw files under {profile_dir}")

    objects = args.objects
    if not objects:
        objects = sorted(
            path for pattern in ("bin/vss_unit_tests*", "bin/vss_system_tests*")
            for path in args.build.glob(pattern)
        )
    objects = [path for path in objects if path.exists()]
    if not objects:
        raise SystemExit("no instrumented binaries found; pass --objects")

    with tempfile.TemporaryDirectory() as temporary:
        profdata = Path(temporary) / "vss.profdata"
        merge_profiles(profiles, profdata)
        lcov = export_lcov(objects, profdata)
        if args.keep:
            target = args.build / "vss.profdata"
            shutil.copy2(profdata, target)
            print(f"profile data: {target}")

    files = parse_lcov(lcov)
    selected = {
        path: stats for path, stats in files.items()
        if args.include in path.replace("\\", "/")
        and not any(pattern in path.replace("\\", "/") for pattern in args.exclude)
    }
    if not selected:
        raise SystemExit(f"no coverage data matching {args.include!r}")

    gated = {
        path: stats for path, stats in selected.items()
        if not any(pattern in path.replace("\\", "/")
                   for pattern in args.gate_exclude)
    }

    total_lines = sum(stats["lines"] for stats in gated.values())
    hit_lines = sum(stats["hit"] for stats in gated.values())
    total_branches = sum(stats["branches"] for stats in gated.values())
    hit_branches = sum(stats["branch_hit"] for stats in gated.values())

    rows = []
    for path, stats in sorted(selected.items()):
        gated_mark = "" if path in gated else " (not gated)"
        rows.append(
            f"| {Path(path).name}{gated_mark} | "
            f"{percent(stats['hit'], stats['lines']):5.1f}% | "
            f"{percent(stats['branch_hit'], stats['branches']):5.1f}% | "
            f"{stats['hit']}/{stats['lines']} | "
            f"{stats['branch_hit']}/{stats['branches']} |"
        )

    line_percent = percent(hit_lines, total_lines)
    branch_percent = percent(hit_branches, total_branches)

    print("| File | Lines | Branches | Line hits | Branch hits |")
    print("|---|---:|---:|---:|---:|")
    print("\n".join(rows))
    print()
    print(f"statements: {hit_lines}/{total_lines} = {line_percent:.2f}% "
          f"(gate {args.min_lines:.0f}%)")
    print(f"branches  : {hit_branches}/{total_branches} = {branch_percent:.2f}% "
          f"(gate {args.min_branches:.0f}%)")

    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(
            "### Coverage\n\n"
            f"Statements: **{line_percent:.2f}%** "
            f"({hit_lines}/{total_lines}, gate {args.min_lines:.0f}%)\n\n"
            f"Branches: **{branch_percent:.2f}%** "
            f"({hit_branches}/{total_branches}, gate {args.min_branches:.0f}%)\n\n"
            "| File | Lines | Branches |\n|---|---:|---:|\n"
            + "\n".join(rows) + "\n",
            encoding="utf-8",
        )

    failed = False
    if line_percent < args.min_lines:
        print(f"FAIL: statements below {args.min_lines:.0f}%", file=sys.stderr)
        failed = True
    if branch_percent < args.min_branches:
        print(f"FAIL: branches below {args.min_branches:.0f}%", file=sys.stderr)
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
