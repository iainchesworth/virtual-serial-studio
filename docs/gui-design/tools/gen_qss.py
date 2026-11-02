#!/usr/bin/env python3
"""Render tokens/style.qss.in into light and dark QSS files and fail on any unresolved placeholder.
Usage: python3 tools/gen_qss.py [--out DIR]   (default DIR = tokens/)"""
import json, re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
out = pathlib.Path(sys.argv[sys.argv.index("--out")+1]) if "--out" in sys.argv else root/"tokens"
tokens = json.loads((root/"tokens/tokens.json").read_text())
tpl = (root/"tokens/style.qss.in").read_text()
used = set(re.findall(r"%([A-Za-z0-9]+)%", tpl))
bad = 0
for theme, colours in tokens["color"].items():
    missing = sorted(used - set(colours))
    if missing:
        print(f"[{theme}] missing tokens: {missing}"); bad += 1; continue
    text = re.sub(r"%([A-Za-z0-9]+)%", lambda m: colours[m.group(1)], tpl)
    (out/f"style.{theme}.qss").write_text(text)
    print(f"[{theme}] wrote style.{theme}.qss ({len(text)} bytes, {len(used)} tokens used)")
sys.exit(1 if bad else 0)
