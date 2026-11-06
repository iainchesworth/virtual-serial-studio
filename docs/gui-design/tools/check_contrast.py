#!/usr/bin/env python3
"""Recompute WCAG 2.2 contrast for every token pair that carries meaning. Exit 1 on any failure.
Text pairs need 4.5:1, non-text boundaries and focus need 3:1."""
import json, pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent
tokens = json.loads((root/"tokens/tokens.json").read_text())["color"]
def lin(c):
    c /= 255
    return c/12.92 if c <= 0.03928 else ((c+0.055)/1.055)**2.4
def lum(h):
    h = h.lstrip("#"); r, g, b = (int(h[i:i+2], 16) for i in (0, 2, 4))
    return 0.2126*lin(r) + 0.7152*lin(g) + 0.0722*lin(b)
def ratio(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la+0.05)/(lb+0.05)
TEXT = [("ink","panel"),("ink","ground"),("ink","sunken"),("ink","accentSoft"),("ink2","panel"),("ink2","sunken"),("ink3","panel"),("ink3","ground"),
        ("accentStrong","panel"),("accentStrong","accentSoft"),("onAccent","accent"),("onDanger","bad"),("ok","okSoft"),("amber","amberSoft"),("bad","badSoft"),
        ("noteInfoText","accentSoft"),("noteOkText","okSoft"),("noteWarnText","amberSoft"),("noteDangerText","badSoft"),("toastText","toastBg"),("toastLink","toastBg"),
        ("ink","hover"),("ink","pressed"),("ink2","hover"),("accentStrong","hover"),("accentText","panel"),("accentText","accentSoft"),("violet","violetSoft"),("ink","fill"),("ink2","fill"),("ink","fillHover"),("onAccent","accentHover")]
NONTEXT = [("control","panel"),("control","ground"),("control","sunken"),("control","accentSoft"),("focus","panel"),("focus","ground"),("trackOff","panel")]
fail = 0
for theme, c in tokens.items():
    for pairs, need in ((TEXT, 4.5), (NONTEXT, 3.0)):
        for a, b in pairs:
            r = ratio(c[a], c[b]); ok = r >= need
            fail += (not ok)
            print(f"{theme:5s} {a:>13s} on {b:<10s} {r:5.2f}  need {need}  {'PASS' if ok else 'FAIL'}")
print("FAILURES:", fail); sys.exit(1 if fail else 0)
