#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the documentation against the repository.

    python3 tools/check_docs.py

Two checks, on top of what `mkdocs build --strict` does for links:

* every preset a page names with `--preset NAME` or in the preset table of
  docs/building.md is a preset in CMakePresets.json;
* every workflow a badge in README.md names under .github/workflows exists,
  which catches a badge left behind when a workflow is renamed or removed.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRESET_OPTION = re.compile(r"--preset[ =]+([A-Za-z0-9_.-]+)")
TABLE_ROW = re.compile(r"^\|(.+?)\|", re.M)
CODE_SPAN = re.compile(r"`([a-z0-9][a-z0-9-]*)`")
BADGE_WORKFLOW = re.compile(r"actions/workflows/([A-Za-z0-9_.-]+\.ya?ml)")


def preset_names(root: Path = ROOT) -> set[str]:
    presets = json.loads((root / "CMakePresets.json").read_text(encoding="utf-8"))
    names: set[str] = set()
    for kind in ("configurePresets", "buildPresets", "testPresets"):
        names.update(preset["name"] for preset in presets.get(kind, []))
    return names


def table_presets(text: str, known: set[str]) -> set[str]:
    """Names in the first column of the preset table that look like presets.

    A name is taken from a code span that starts like a known preset (an
    operating system prefix) so that prose such as `ICL_SANITIZERS` is left out.
    """
    prefixes = {name.split("-", 1)[0] for name in known}
    found: set[str] = set()
    for row in TABLE_ROW.findall(text):
        for span in CODE_SPAN.findall(row):
            if span.split("-", 1)[0] in prefixes:
                found.add(span)
    return found


def check_presets(root: Path = ROOT) -> list[str]:
    known = preset_names(root)
    problems = []
    for page in sorted((root / "docs").glob("*.md")) + [root / "README.md"]:
        text = page.read_text(encoding="utf-8")
        used = set(PRESET_OPTION.findall(text))
        if page.name == "building.md":
            used |= table_presets(text, known)
        for name in sorted(used - known):
            problems.append(f"{page.relative_to(root)}: preset '{name}' is not in CMakePresets.json")
    return problems


def check_badges(root: Path = ROOT) -> list[str]:
    text = (root / "README.md").read_text(encoding="utf-8")
    workflows = root / ".github" / "workflows"
    return [
        f"README.md: a badge names .github/workflows/{name}, which does not exist"
        for name in sorted(set(BADGE_WORKFLOW.findall(text)))
        if not (workflows / name).is_file()
    ]


def main() -> int:
    problems = check_presets() + check_badges()
    for problem in problems:
        print(problem, file=sys.stderr)
    if not problems:
        print("documentation checks passed")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
