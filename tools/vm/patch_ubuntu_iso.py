#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a copy of an Ubuntu live-server ISO that starts the autoinstall without a prompt.

    patch_ubuntu_iso.py --iso E:/ISOs/ubuntu-26.04.1-live-server-amd64.iso --out autoinstall.iso

Subiquity stops at "Continue with autoinstall? (yes|no)" unless "autoinstall" is on the kernel
command line, and nobody is at the console of a test guest. The boot menu is the small file
/boot/grub/grub.cfg. This script copies the ISO, then rewrites that file in place inside the copy:
the command line gains "autoinstall", the timeout drops to three seconds, and the file is padded
with spaces to its old length, so the layout of the image does not change. The original is
never modified. It needs the pycdlib package.
"""

from __future__ import annotations

import argparse
import io
import shutil
import sys
from pathlib import Path

GRUB = "/boot/grub/grub.cfg"


def patched_menu(original: str) -> str:
    """Returns the boot menu with autoinstall added, in no more bytes than the original."""
    lines = original.split("\n")
    out = []
    found = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("linux") and "/casper/vmlinuz" in stripped:
            if "autoinstall" in stripped:
                raise ValueError("the boot menu already says autoinstall")
            line = line.replace("---", "autoinstall ---", 1)
            found = True
        elif stripped.startswith("set timeout="):
            line = "set timeout=3"
        out.append(line)
    if not found:
        raise ValueError("no linux line for /casper/vmlinuz in the boot menu")
    text = "\n".join(out)
    # Room for the longer line comes from the font line, which only affects the menu's looks.
    text = text.replace("loadfont unicode\n\n", "", 1)
    return text


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)

    try:
        import pycdlib
    except ImportError:
        print("this needs the pycdlib package (pip install pycdlib)", file=sys.stderr)
        return 2

    if args.out.exists():
        print(f"{args.out} exists; delete it first", file=sys.stderr)
        return 1

    # Read the menu from the original, which is never opened for writing.
    source = pycdlib.PyCdlib()
    source.open(str(args.iso))
    try:
        buffer = io.BytesIO()
        source.get_file_from_iso_fp(buffer, rr_path=GRUB)
    finally:
        source.close()
    original = buffer.getvalue()
    data = patched_menu(original.decode("utf-8")).encode("utf-8")
    if len(data) > len(original):
        raise ValueError(f"the new menu is {len(data) - len(original)} bytes too long")
    data = data.ljust(len(original), b" ")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.iso, args.out)
    try:
        with pycdlib.InPlaceEditor(str(args.out)) as editor:
            editor.modify_file(io.BytesIO(data), len(data), rr_path=GRUB)
    except Exception:
        args.out.unlink(missing_ok=True)
        raise

    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
