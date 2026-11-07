#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the static analysers locally, the way CI runs them, and write SARIF.

    python tools/analyze.py clang-tidy              # every first-party translation unit
    python tools/analyze.py clang-tidy --changed    # only the files changed since origin/develop
    python tools/analyze.py cppcheck zizmor
    python tools/analyze.py msvc                    # MSVC /analyze (PREfast), Windows only
    python tools/analyze.py all                     # everything this machine can run

The analysers are clang-tidy, cppcheck, zizmor (workflows) and msvc (PREfast). CodeQL has no local
wrapper here: it needs the CodeQL CLI and a full build, so it runs on GitHub (docs/testing.md).

clang-tidy and cppcheck read a build tree's compile_commands.json (default: the newest tree under
build/ that has one, or --build DIR). The tree must be built first, because the Qt GUI sources
include generated headers. Configure with a `*-gui` preset so the GUI and the tests are in the
database. On Windows this script loads the Visual Studio environment itself when INCLUDE is unset.

Each analyser writes <out>/<name>.sarif (default build/analysis/), the same files CI uploads to
GitHub code scanning. The exit status is 1 when any analyser reports a finding or fails to run.
A tool that is not installed is reported as SKIP and does not fail the run, unless it was named
on the command line. Without --changed, the whole tree is analysed.

The check set lives in this file (CLANG_TIDY_CHECKS) and is the only place CI reads it from, so a
clean local run means a clean job. Five clang-tidy checks are left out on purpose; the reasons
are in docs/testing.md.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import sysconfig
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IS_WINDOWS = os.name == "nt"

# Directories whose sources are first-party. Generated files (moc, rcc) live under build/ and are
# not ours to fix; the fetched Boost and the vcpkg tree are not either.
FIRST_PARTY = ("libs/", "apps/", "tests/", "perf/", "drivers/")
CPP_SUFFIXES = (".cpp", ".cc", ".cxx")
HEADER_SUFFIXES = (".hpp", ".h", ".hxx")

CLANG_TIDY_CHECKS = (
    "-*,"
    "bugprone-*,-bugprone-easily-swappable-parameters,-bugprone-signed-bitwise,-bugprone-unused-return-value,"
    "concurrency-*,-concurrency-mt-unsafe,"
    "performance-*,-performance-enum-size,"
    "portability-*,-portability-avoid-pragma-once,"
    "clang-analyzer-*,"
    "misc-*,-misc-include-cleaner,-misc-const-correctness,-misc-non-private-member-variables-in-classes,"
    "-misc-no-recursion,"
    "readability-identifier-naming"
)

CPPCHECK_ARGS = (
    "--enable=warning,performance,portability",
    "--inline-suppr",
    "--std=c++23",
    "--suppress=missingIncludeSystem",
    "--suppress=unmatchedSuppression",
    "--suppress=normalCheckLevelMaxBranches",
    "--check-level=exhaustive",
    "--quiet",
)

# One line of clang-tidy or cppcheck text output: path:line:col: severity: message [rule]
DIAGNOSTIC = re.compile(
    r"^(?P<path>(?:[A-Za-z]:)?[^:\n]+):(?P<line>\d+):(?P<col>\d+): "
    r"(?P<severity>warning|error|style|performance|portability|information): "
    r"(?P<message>.*?)(?: \[(?P<rule>[\w.,+-]+)\])?$"
)


@dataclass(frozen=True)
class Finding:
    rule: str
    level: str  # SARIF level: error, warning or note
    message: str
    path: str  # relative to the repository, with forward slashes
    line: int
    col: int = 1


# ---------------------------------------------------------------------------------------------
# SARIF


def to_sarif(tool: str, findings: list[Finding], uri: str = "https://github.com/iainchesworthlabs/virtual-serial-studio"):
    rules = sorted({f.rule for f in findings})
    return {
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "version": "2.1.0",
        "runs": [
            {
                "tool": {"driver": {"name": tool, "informationUri": uri, "rules": [{"id": r} for r in rules]}},
                "results": [
                    {
                        "ruleId": f.rule,
                        "ruleIndex": rules.index(f.rule),
                        "level": f.level,
                        "message": {"text": f.message},
                        "locations": [
                            {
                                "physicalLocation": {
                                    "artifactLocation": {"uri": f.path, "uriBaseId": "%SRCROOT%"},
                                    "region": {"startLine": max(f.line, 1), "startColumn": max(f.col, 1)},
                                }
                            }
                        ],
                    }
                    for f in findings
                ],
            }
        ],
    }


def write_sarif(out: Path, name: str, tool: str, findings: list[Finding]) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    target = out / f"{name}.sarif"
    target.write_text(json.dumps(to_sarif(tool, findings), indent=1) + "\n", encoding="utf-8")
    return target


# ---------------------------------------------------------------------------------------------
# Helpers


def relative(path: str) -> str | None:
    """The repository-relative form of a diagnostic's path, or None when it is not first-party."""
    p = Path(path)
    if not p.is_absolute():
        p = ROOT / p
    try:
        rel = p.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return None
    if rel.startswith("build/") or not rel.startswith(FIRST_PARTY):
        return None
    return rel


def parse_diagnostics(text: str) -> list[Finding]:
    """Parse clang-tidy and cppcheck text output. Findings outside the repository are dropped."""
    found: dict[tuple, Finding] = {}
    for raw in text.splitlines():
        m = DIAGNOSTIC.match(raw.strip())
        if not m:
            continue
        rel = relative(m["path"])
        if rel is None:
            continue
        level = "error" if m["severity"] == "error" else "warning"
        rule = m["rule"] or "unknown"
        key = (rule, rel, int(m["line"]), m["message"])
        found.setdefault(key, Finding(rule, level, m["message"], rel, int(m["line"]), int(m["col"])))
    return sorted(found.values(), key=lambda f: (f.path, f.line, f.rule))


def git(*args: str) -> str:
    res = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True, check=False)
    return res.stdout.strip() if res.returncode == 0 else ""


def changed_files(base: str | None) -> list[str]:
    ref = base or next((r for r in ("origin/develop", "develop") if git("rev-parse", "--verify", "--quiet", r)), "HEAD~1")
    names = git("diff", "--name-only", "--diff-filter=d", f"{ref}...HEAD").splitlines()
    names += git("diff", "--name-only", "--diff-filter=d", "HEAD").splitlines()
    return sorted({n for n in names if n})


def find_build_dir(explicit: str | None) -> Path | None:
    if explicit:
        d = Path(explicit)
        return d if (d / "compile_commands.json").is_file() else None
    trees = [p.parent for p in (ROOT / "build").glob("*/compile_commands.json")]
    trees = [t for t in trees if t.name != "analysis"]
    return max(trees, key=lambda t: (t / "compile_commands.json").stat().st_mtime, default=None)


def source_files(build: Path, changed: list[str] | None) -> list[str]:
    """First-party translation units of the compile database, narrowed to the changed files."""
    db = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    units = sorted({rel for e in db if (rel := relative(str(Path(e.get("directory", ".")) / e["file"]))) is not None
                    and rel.endswith(CPP_SUFFIXES)})
    if changed is None:
        return units
    wanted = set(changed)
    # A changed header is analysed through the translation units that sit beside it or in the
    # same library, which is a close and cheap approximation of "the files that include it".
    dirs = {c.split("/include/")[0] if "/include/" in c else Path(c).parent.as_posix() for c in changed
            if c.endswith(HEADER_SUFFIXES)}
    return [u for u in units if u in wanted or any(u.startswith(d + "/") for d in dirs)]


def find_tool(*names: str) -> str | None:
    for name in names:
        if (hit := shutil.which(name)):
            return hit
    roots = [os.environ.get("LLVM_ROOT", ""), r"C:\Program Files\LLVM", r"C:\Program Files\Cppcheck"]
    for root in roots:
        for name in names:
            for suffix in ("", ".exe"):
                candidate = Path(root) / "bin" / (name + suffix) if root else None
                if candidate and candidate.is_file():
                    return str(candidate)
                candidate = Path(root) / (name + suffix) if root else None
                if candidate and candidate.is_file():
                    return str(candidate)
    return None


def load_msvc_env() -> None:
    """Import the x64 Visual Studio environment into this process when it is not there yet."""
    if not IS_WINDOWS or os.environ.get("INCLUDE"):
        return
    pf = os.environ.get("ProgramFiles", r"C:\Program Files")
    pattern = "Microsoft Visual Studio/*/*/VC/Auxiliary/Build/vcvars64.bat"
    candidates = sorted(Path(pf).glob(pattern), reverse=True)
    if not candidates:
        return
    res = subprocess.run(f'"{candidates[0]}" >nul && set', shell=True, capture_output=True, text=True, check=False)
    for line in res.stdout.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            os.environ[key] = value


def need_build(args: argparse.Namespace) -> Path | None:
    build = find_build_dir(args.build)
    if build is None:
        print("      no build tree with compile_commands.json; configure and build a *-gui preset first",
              file=sys.stderr)
    return build


# ---------------------------------------------------------------------------------------------
# Analysers. Each returns (findings or None when it could not run, note).


def run_clang_tidy(args: argparse.Namespace):
    tool = find_tool("clang-tidy", "clang-tidy-23", "clang-tidy-22")
    if not tool:
        return None, "clang-tidy not found (install LLVM, or set LLVM_ROOT)"
    build = need_build(args)
    if build is None:
        return None, "no compile database"
    files = source_files(build, changed_files(args.base) if args.changed else None)
    if not files:
        return [], "no files to analyse"
    load_msvc_env()
    print(f"      {len(files)} file(s), {build.name}")

    def one(rel: str) -> str:
        cmd = [tool, "-p", str(build), f"-checks={CLANG_TIDY_CHECKS}", "--quiet", str(ROOT / rel)]
        res = subprocess.run(cmd, capture_output=True, text=True, check=False)
        return res.stdout + res.stderr

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        text = "\n".join(pool.map(one, files))
    return parse_diagnostics(text), f"{len(files)} file(s)"


def run_cppcheck(args: argparse.Namespace):
    tool = find_tool("cppcheck")
    if not tool:
        return None, "cppcheck not found (winget install Cppcheck.Cppcheck, or apt install cppcheck)"
    build = need_build(args)
    if build is None:
        return None, "no compile database"
    files = source_files(build, changed_files(args.base) if args.changed else None)
    if not files:
        return [], "no files to analyse"
    load_msvc_env()
    listing = build / "analysis-cppcheck-files.txt"
    listing.write_text("\n".join(str(ROOT / f) for f in files), encoding="utf-8")
    cmd = [tool, *CPPCHECK_ARGS, f"-j{args.jobs}", "--template={file}:{line}:{column}: {severity}: {message} [{id}]",
           f"--file-list={listing}", "-I", str(ROOT / "libs"), "-I", str(ROOT / "apps")]
    for define in ("Q_OBJECT=", "Q_SIGNALS=public", "Q_SLOTS=", "Q_EMIT="):
        cmd.append(f"-D{define}")
    print(f"      {len(files)} file(s), cppcheck {subprocess.run([tool, '--version'], capture_output=True, text=True).stdout.strip()}")
    res = subprocess.run(cmd, capture_output=True, text=True, check=False, cwd=ROOT)
    return parse_diagnostics(res.stdout + res.stderr), f"{len(files)} file(s)"


def run_zizmor(args: argparse.Namespace):
    user_scripts = sysconfig.get_path("scripts", scheme="nt_user" if IS_WINDOWS else "posix_user")
    tool = shutil.which("zizmor") or shutil.which("zizmor", path=user_scripts)
    if not tool:
        return None, "zizmor not found (python -m pip install -r requirements/zizmor.txt)"
    cmd = [tool]
    res = subprocess.run([*cmd, "--min-severity", "medium", "--format", "sarif", "--no-exit-codes", "."],
                         capture_output=True, text=True, check=False, cwd=ROOT)
    if res.returncode != 0 or not res.stdout.strip():
        return None, f"zizmor failed: {res.stderr.strip()[-300:]}"
    # zizmor writes SARIF itself; keep it as it is and count its results.
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    (out / "zizmor.sarif").write_text(res.stdout, encoding="utf-8")
    sarif = json.loads(res.stdout)
    findings = [Finding(r.get("ruleId", "zizmor"), r.get("level", "warning"), r["message"]["text"], "", 0)
                for run in sarif.get("runs", []) for r in run.get("results", [])]
    return findings, "workflows"


def run_msvc(args: argparse.Namespace):
    if not IS_WINDOWS:
        return None, "MSVC code analysis runs on Windows only"
    load_msvc_env()
    if not shutil.which("cl"):
        return None, "cl.exe not found (install Visual Studio with the C++ workload)"
    cmake = shutil.which("cmake")
    if not cmake:
        return None, "cmake not found"
    cfg = [cmake, "--preset", "windows-msvc-analysis"]
    if args.virtual_serial_dir:
        cfg.append(f"-DVSS_VIRTUAL_SERIAL_DIR={args.virtual_serial_dir}")
    for cmd in (cfg, [cmake, "--build", "--preset", "windows-msvc-analysis", "--", "-k", "0"]):
        print(f"      {' '.join(cmd)}")
        res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=False)
        if res.returncode != 0:
            log = args.out / "msvc-build.log"
            args.out.mkdir(parents=True, exist_ok=True)
            log.write_text(res.stdout + res.stderr, encoding="utf-8")
            errors = [ln for ln in (res.stdout + res.stderr).splitlines() if re.search(r"\berror\b", ln)]
            step = "configure" if cmd is cfg else "build"
            return None, f"{step} failed (log: {log}):\n" + "\n".join(errors[:15])
    tree = ROOT / "build" / "windows-msvc-analysis"
    findings: dict[tuple, Finding] = {}
    for path in tree.rglob("*.sarif"):
        if path.parent == args.out:
            continue
        for run in json.loads(path.read_text(encoding="utf-8")).get("runs", []):
            artifacts = run.get("artifacts", [])
            for r in run.get("results", []):
                loc = r["locations"][0]["physicalLocation"]
                art = loc["artifactLocation"]
                uri = art.get("uri") or artifacts[art["index"]]["location"]["uri"]
                uri = re.sub(r"^file:///", "", uri).replace("%20", " ")
                rel = relative(uri)
                if rel is None:
                    continue
                region = loc.get("region", {})
                f = Finding(r.get("ruleId", "msvc"), r.get("level", "warning"), r["message"]["text"], rel,
                            region.get("startLine", 1), region.get("startColumn", 1))
                findings.setdefault((f.rule, f.path, f.line, f.message), f)
    return sorted(findings.values(), key=lambda f: (f.path, f.line, f.rule)), "build/windows-msvc-analysis"


ANALYSERS = {
    "clang-tidy": ("clang-tidy", run_clang_tidy),
    "cppcheck": ("cppcheck", run_cppcheck),
    "zizmor": ("zizmor", run_zizmor),
    "msvc": ("PREfast", run_msvc),
}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("analysers", nargs="+", choices=[*ANALYSERS, "all"])
    ap.add_argument("--build", help="build tree holding compile_commands.json (default: the newest under build/)")
    ap.add_argument("--changed", action="store_true", help="analyse only files changed since the base")
    ap.add_argument("--base", help="base ref for --changed (default origin/develop)")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "analysis", help="directory for the SARIF files")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--virtual-serial-dir", default=os.environ.get("VSS_VIRTUAL_SERIAL_DIR"),
                    help="passed to CMake as VSS_VIRTUAL_SERIAL_DIR by the msvc analyser")
    ap.add_argument("--limit", type=int, default=40, help="findings to print per analyser")
    args = ap.parse_args(argv)
    explicit = "all" not in args.analysers
    names = list(ANALYSERS) if "all" in args.analysers else args.analysers
    status = 0
    for name in names:
        tool_name, fn = ANALYSERS[name]
        print(f"{name}")
        findings, note = fn(args)
        if findings is None:
            print(f"{'FAIL' if explicit else 'SKIP'}  {name}: {note}")
            status |= 1 if explicit else 0
            continue
        if name != "zizmor":
            write_sarif(args.out, name, tool_name, findings)
        print(f"{'FAIL' if findings else 'PASS'}  {name}: {len(findings)} finding(s), {note}; {args.out / (name + '.sarif')}")
        by_rule: dict[str, int] = {}
        for f in findings:
            by_rule[f.rule] = by_rule.get(f.rule, 0) + 1
        if len(by_rule) > 1:
            print("      by rule: " + ", ".join(f"{r} {n}" for r, n in sorted(by_rule.items(), key=lambda kv: -kv[1])))
        for f in findings[: args.limit]:
            print(f"      {f.path}:{f.line}: {f.message} [{f.rule}]")
        if len(findings) > args.limit:
            print(f"      ... {len(findings) - args.limit} more in the SARIF file")
        status |= 1 if findings else 0
    return status


if __name__ == "__main__":
    sys.exit(main())
