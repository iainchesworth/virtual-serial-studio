#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the libFuzzer targets of the linux-fuzz build.

    python3 tools/fuzz.py --mode regress
    python3 tools/fuzz.py --mode short
    python3 tools/fuzz.py --mode nightly

regress replays the seed corpus and the regression inputs once (the same check
the fuzz_* CTest tests make, now under libFuzzer's AddressSanitizer). short and
nightly fuzz each target for a fixed time. A target's working corpus lives in
--work-dir, which CI keeps between runs, and its crash files go to
<work-dir>/artifacts. The exit status is 1 when any target fails.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Seconds of fuzzing per target (standard section 6.6).
SECONDS = {"short": 60, "nightly": 600}


def harnesses(root: Path = ROOT) -> list[Path]:
    """Every harness source: libs/<lib>/fuzz/fuzz_*.cpp."""
    return sorted(root.glob("libs/*/fuzz/fuzz_*.cpp"))


def command(
    harness: Path, build_dir: Path, mode: str, work_dir: Path, seconds: int
) -> list[str]:
    """The libFuzzer command line for one harness."""
    seeds = harness.parent / "corpus"
    regressions = harness.parent / "regressions"
    binary = str(build_dir / "bin" / harness.stem)
    if mode == "regress":
        return [binary, "-runs=0", str(seeds), str(regressions)]
    return [
        binary,
        f"-max_total_time={seconds}",
        "-timeout=10",
        "-rss_limit_mb=2048",
        f"-artifact_prefix={work_dir / 'artifacts'}/",
        str(work_dir / harness.stem),
        str(seeds),
        str(regressions),
    ]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--mode", choices=["regress", "short", "nightly"], required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "linux-fuzz")
    parser.add_argument("--work-dir", type=Path, default=ROOT / ".cache" / "fuzz")
    parser.add_argument("--seconds", type=int, help="override the time per target")
    args = parser.parse_args(argv)

    seconds = args.seconds or SECONDS.get(args.mode, 0)
    sources = harnesses()
    if not sources:
        print("no fuzz harness found", file=sys.stderr)
        return 1

    (args.work_dir / "artifacts").mkdir(parents=True, exist_ok=True)
    failed = []
    for harness in sources:
        (args.work_dir / harness.stem).mkdir(parents=True, exist_ok=True)
        line = command(harness, args.build_dir, args.mode, args.work_dir, seconds)
        print(f"::group::{harness.stem}", flush=True)
        print(" ".join(line), flush=True)
        status = subprocess.run(line, check=False).returncode
        print("::endgroup::", flush=True)
        if status != 0:
            print(f"{harness.stem}: exit status {status}", file=sys.stderr)
            failed.append(harness.stem)
    if failed:
        print("failed: " + ", ".join(failed), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
