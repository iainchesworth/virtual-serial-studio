#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Choose which legs of ci.yml a run needs (standard sections 11.2 and 11.5).

    python3 tools/ci/plan_legs.py --event pull_request --draft false \\
        --plan plan.txt --changed changed.txt --config .github/ci/plan.json >> "$GITHUB_OUTPUT"

`plan.txt` is the output of the shared `plan_gate.py` (build, gui, machinery, docs_only) and
`changed.txt` is the list of changed paths, one per line. The script prints key=value lines for
$GITHUB_OUTPUT:

    tier               draft, pr, queue, post-merge or full
    build_matrix       {"include": [...]} for the `build` job, an empty list when it has no leg
    run_<job>          true or false for each job, with dashes as underscores (run_asan_ubsan)

ci.yml runs a job when its `run_<job>` is true and gives the same value to the `CI Status`
aggregator, so the job and the check on it cannot disagree.

The tiers (docs/migration.md has the reasons):

    draft        nothing: the static jobs of ci.yml do not depend on this script
    pr           Linux GCC build and tests, ASan/UBSan, fuzz replay; TSan when threading code changed
    queue        the same on the merged tree, plus Windows MSVC, TSan, the Qt GUI build, the
                 performance floors when library code changed, macOS when a change can affect it,
                 and the Windows driver when the driver changed
    post-merge   the legs the queue does not run: Linux Clang, macOS, coverage, Tracy, the driver
    full         every leg (workflow_dispatch)

Config keys read from .github/ci/plan.json, besides the ones plan_gate.py reads: `driver`,
`perf`, `platform`, `threads_always` (path globs) and `threads_scan` (path globs) with
`threads_tokens` (a regular expression a scanned file matches when it uses threads).
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import sys
from pathlib import Path

JOBS = (
    "build",
    "macos",
    "gui",
    "coverage",
    "performance",
    "profiling",
    "asan-ubsan",
    "fuzz-regress",
    "tsan",
    "driver",
)

GCC = {"os": "ubuntu-26.04", "name": "linux-gcc", "compiler": "gcc", "preset": "linux-release"}
CLANG = {"os": "ubuntu-26.04", "name": "linux-clang", "compiler": "clang", "preset": "linux-release"}
MSVC = {"os": "windows-2025", "name": "windows-msvc", "compiler": "msvc", "preset": "windows-msvc-release"}

CPP_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".mm")


def matches(path: str, patterns: list[str]) -> str | None:
    """The first glob that matches, with `**` matching across directories."""
    for pat in patterns:
        if fnmatch.fnmatch(path, pat) or fnmatch.fnmatch(path, pat.replace("**/", "*").replace("/**", "/*")):
            return pat
    return None


def tier_of(event: str, draft: bool) -> str:
    if event == "workflow_dispatch":
        return "full"
    if event == "push":
        return "post-merge"
    if event == "merge_group":
        return "queue"
    if event == "pull_request":
        return "draft" if draft else "pr"
    raise ValueError(f"unexpected event {event!r}")


def uses_threads(changed: list[str], cfg: dict, root: Path) -> str:
    """The first changed path that can affect threading, or an empty string."""
    for path in changed:
        if matches(path, cfg.get("threads_always", [])):
            return path
    tokens = re.compile(cfg.get("threads_tokens", r"std::thread"))
    for path in changed:
        if not path.endswith(CPP_SUFFIXES) or not matches(path, cfg.get("threads_scan", [])):
            continue
        file = root / path
        if not file.is_file():
            return f"{path} (removed)"
        if tokens.search(file.read_text(encoding="utf-8", errors="replace")):
            return path
    return ""


def decide(event: str, draft: bool, plan: dict[str, str], changed: list[str], cfg: dict, root: Path) -> dict:
    tier = tier_of(event, draft)
    build = plan.get("build") == "true"
    machinery = plan.get("machinery") == "true"
    gui = plan.get("gui") == "true"

    flags = {job: False for job in JOBS}
    matrix: list[dict] = []
    why: dict[str, str] = {}

    if tier == "full":
        matrix = [GCC, CLANG, MSVC]
        flags = {job: True for job in JOBS}
        why["all"] = "workflow_dispatch asks for every leg"
    elif tier == "draft":
        pass
    else:
        threads = uses_threads(changed, cfg, root) if build else ""
        if machinery and build:
            threads = threads or "the gate's own files changed"
        perf = next((p for p in changed if matches(p, cfg.get("perf", []))), "")
        platform = next((p for p in changed if matches(p, cfg.get("platform", []))), "")
        driver = next((p for p in changed if matches(p, cfg.get("driver", []))), "")
        if machinery:
            perf = perf or "the gate's own files changed"
            platform = platform or "the gate's own files changed"
            driver = driver or "the gate's own files changed"

        if tier == "pr" and build:
            matrix = [GCC]
            flags["asan-ubsan"] = flags["fuzz-regress"] = True
            flags["tsan"] = bool(threads)
            if threads:
                why["tsan"] = threads
        elif tier == "queue":
            if build:
                matrix = [GCC, MSVC]
                flags["asan-ubsan"] = flags["fuzz-regress"] = flags["tsan"] = True
                flags["gui"] = gui
                flags["performance"] = bool(perf)
                flags["macos"] = bool(threads or platform)
                if gui:
                    why["gui"] = plan.get("gui_reason", "")
                if perf:
                    why["performance"] = perf
                if threads or platform:
                    why["macos"] = threads or platform
            flags["driver"] = bool(driver)
            if driver:
                why["driver"] = driver
        elif tier == "post-merge":
            matrix = [CLANG]
            for job in ("macos", "coverage", "profiling", "driver"):
                flags[job] = True
    flags["build"] = bool(matrix)
    return {"tier": tier, "matrix": matrix, "flags": flags, "why": why}


def render_outputs(result: dict) -> str:
    lines = [f"tier={result['tier']}", "build_matrix=" + json.dumps({"include": result["matrix"]}, separators=(",", ":"))]
    for job in JOBS:
        lines.append(f"run_{job.replace('-', '_')}={'true' if result['flags'][job] else 'false'}")
    return "\n".join(lines) + "\n"


def render_summary(result: dict) -> str:
    names = {"build": "Build and test (" + ", ".join(m["name"] for m in result["matrix"]) + ")" if result["matrix"] else "Build and test"}
    rows = ["### Plan", "", f"Tier: **{result['tier']}**", "", "| Job | Runs | Because |", "|---|---|---|"]
    for job in JOBS:
        rows.append(f"| {names.get(job, job)} | {'yes' if result['flags'][job] else 'no'} | {result['why'].get(job, '')} |")
    return "\n".join(rows) + "\n"


def parse_plan(text: str) -> dict[str, str]:
    out = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            out[key.strip()] = value.strip()
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--event", required=True)
    ap.add_argument("--draft", default="false")
    ap.add_argument("--plan", required=True, help="output of plan_gate.py")
    ap.add_argument("--changed", required=True, help="changed paths, one per line")
    ap.add_argument("--config", default=".github/ci/plan.json")
    ap.add_argument("--root", default=".")
    ap.add_argument("--summary", help="append a Markdown table here (GITHUB_STEP_SUMMARY)")
    args = ap.parse_args(argv)

    cfg = json.loads(Path(args.config).read_text(encoding="utf-8"))
    plan = parse_plan(Path(args.plan).read_text(encoding="utf-8"))
    changed = [p.strip() for p in Path(args.changed).read_text(encoding="utf-8").splitlines() if p.strip()]
    result = decide(args.event, args.draft.lower() == "true", plan, changed, cfg, Path(args.root))
    sys.stdout.write(render_outputs(result))
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as handle:
            handle.write(render_summary(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
