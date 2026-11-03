#!/usr/bin/env python3
"""Render the QSS template for both themes and fail on any unresolved token.

Usage: python3 tools/gen_qss.py [--out DIR]
The default output directory is ``build/qss``; generated previews are never
committed.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOKENS_DIR = ROOT / "docs" / "gui-design" / "tokens"
if not (TOKENS_DIR / "tokens.json").exists():
    TOKENS_DIR = ROOT / "tokens"

out = (
    pathlib.Path(sys.argv[sys.argv.index("--out") + 1])
    if "--out" in sys.argv
    else ROOT / "build" / "qss"
)
out.mkdir(parents=True, exist_ok=True)

tokens = json.loads((TOKENS_DIR / "tokens.json").read_text())
template = (TOKENS_DIR / "style.qss.in").read_text()
used = set(re.findall(r"%([A-Za-z0-9]+)%", template))
bad = 0
for theme, colours in tokens["color"].items():
    missing = sorted(used - set(colours))
    if missing:
        print(f"[{theme}] missing tokens: {missing}")
        bad += 1
        continue
    text = re.sub(r"%([A-Za-z0-9]+)%", lambda m: colours[m.group(1)], template)
    (out / f"style.{theme}.qss").write_text(text)
    print(
        f"[{theme}] wrote style.{theme}.qss "
        f"({len(text)} bytes, {len(used)} tokens used)"
    )

sys.exit(1 if bad else 0)
