#!/usr/bin/env python3
"""Render tokens/style.qss.in into light and dark QSS files and fail on any unresolved placeholder.
Usage: python3 tools/gen_qss.py [--out DIR]   (default DIR = tokens/)"""
import json, re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
out = pathlib.Path(sys.argv[sys.argv.index("--out")+1]) if "--out" in sys.argv else root/"tokens"
tokens = json.loads((root/"tokens/tokens.json").read_text())
tpl = (root/"tokens/style.qss.in").read_text()
# radius.<name>, space.<index> and theme resolve to sizes and the theme name, not colours.
PATTERN = r"%([A-Za-z0-9_.]+)%"
used = set(re.findall(PATTERN, tpl))
def resolve(name, theme, colours):
    if name == "theme":
        return theme
    if name.startswith("radius."):
        return f"{tokens['radius'][name[7:]]}px"
    if name.startswith("space."):
        return f"{tokens['space'][int(name[6:])]}px"
    return colours[name]
def is_colour(name):
    return name != "theme" and not name.startswith(("radius.", "space."))
bad = 0
for theme, colours in tokens["color"].items():
    missing = sorted(n for n in used if is_colour(n) and n not in colours)
    if missing:
        print(f"[{theme}] missing tokens: {missing}"); bad += 1; continue
    text = re.sub(PATTERN, lambda m: resolve(m.group(1), theme, colours), tpl)
    (out/f"style.{theme}.qss").write_text(text)
    print(f"[{theme}] wrote style.{theme}.qss ({len(text)} bytes, {len(used)} tokens used)")
sys.exit(1 if bad else 0)
