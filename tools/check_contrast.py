#!/usr/bin/env python3
"""Recompute WCAG 2.2 contrast for every token pair that carries meaning.

The authoritative tokens live in ``docs/gui-design/tokens/tokens.json`` (the
bundle installed by WP0).  Exit 1 on any failure: text pairs need 4.5:1,
non-text boundaries and focus need 3:1.
"""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOKENS_DIR = ROOT / "docs" / "gui-design" / "tokens"
if not (TOKENS_DIR / "tokens.json").exists():
    TOKENS_DIR = ROOT / "tokens"

tokens = json.loads((TOKENS_DIR / "tokens.json").read_text())["color"]


def lin(c):
    c /= 255
    return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4


def lum(h):
    h = h.lstrip("#")
    r, g, b = (int(h[i : i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def ratio(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


TEXT = [
    ("ink", "panel"),
    ("ink", "ground"),
    ("ink", "sunken"),
    ("ink", "accentSoft"),
    ("ink2", "panel"),
    ("ink2", "sunken"),
    ("ink3", "panel"),
    ("ink3", "ground"),
    ("accentStrong", "panel"),
    ("accentStrong", "accentSoft"),
    ("onAccent", "accent"),
    ("onDanger", "bad"),
    ("ok", "okSoft"),
    ("amber", "amberSoft"),
    ("bad", "badSoft"),
    ("noteInfoText", "accentSoft"),
    ("noteOkText", "okSoft"),
    ("noteWarnText", "amberSoft"),
    ("noteDangerText", "badSoft"),
    ("toastText", "toastBg"),
    ("toastLink", "toastBg"),
]
NONTEXT = [
    ("control", "panel"),
    ("control", "ground"),
    ("control", "sunken"),
    ("control", "accentSoft"),
    ("focus", "panel"),
    ("focus", "ground"),
    ("trackOff", "panel"),
]

fail = 0
for theme, colour in tokens.items():
    for pairs, need in ((TEXT, 4.5), (NONTEXT, 3.0)):
        for a, b in pairs:
            value = ratio(colour[a], colour[b])
            ok = value >= need
            fail += not ok
            print(
                f"{theme:5s} {a:>13s} on {b:<10s} {value:5.2f}  "
                f"need {need}  {'PASS' if ok else 'FAIL'}"
            )
print("FAILURES:", fail)
sys.exit(1 if fail else 0)
