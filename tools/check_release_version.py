#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when a release tag and the project version disagree.

    python3 tools/check_release_version.py v0.1.0-rc.1

The part of the tag before any -suffix must equal the VERSION in project() in
CMakeLists.txt and the version in vcpkg.json, so the programs report the
version they were released as. A tag such as v0.1.0-rc.1 is a release
candidate of 0.1.0.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TAG = re.compile(r"^v(\d+\.\d+\.\d+)(?:-[0-9A-Za-z.-]+)?$")
PROJECT_VERSION = re.compile(r"\bproject\s*\([^)]*?\bVERSION\s+(\d+\.\d+\.\d+)", re.S)


def declared_versions(root: Path = ROOT) -> dict[str, str]:
    """The version each file declares, by the name the message uses."""
    cmake = PROJECT_VERSION.search((root / "CMakeLists.txt").read_text(encoding="utf-8"))
    manifest = json.loads((root / "vcpkg.json").read_text(encoding="utf-8"))
    return {
        "CMakeLists.txt project(VERSION)": cmake.group(1) if cmake else "(not found)",
        "vcpkg.json version": manifest.get("version", manifest.get("version-string", "(not found)")),
    }


def check(tag: str, root: Path = ROOT) -> list[str]:
    """The problems with `tag`; empty when it matches."""
    match = TAG.match(tag)
    if not match:
        return [f"{tag} is not vMAJOR.MINOR.PATCH[-suffix]"]
    wanted = match.group(1)
    return [
        f"{tag} releases {wanted}, but {name} is {value}"
        for name, value in declared_versions(root).items()
        if value != wanted
    ]


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: check_release_version.py <tag>", file=sys.stderr)
        return 2
    problems = check(argv[1])
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print("Change the version in a pull request, or tag the version the project declares.", file=sys.stderr)
        return 1
    print(f"{argv[1]} matches the project version")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
