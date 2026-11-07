#!/usr/bin/env python3
# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the subsystem a Windows executable declares in its PE header.

A program of the console subsystem gets a console window from Windows whenever
it is not started from a console. ``vss_gui`` belongs to the Windows subsystem
for that reason, and CTest runs this against the built program (``gui_subsystem``)
so that a change to the link options cannot bring the console back.

    python3 tools/check_pe_subsystem.py build/windows-msvc-debug-gui/bin/vss_gui.exe windows
    python3 tools/check_pe_subsystem.py build/windows-msvc-debug-gui/bin/vss_cli.exe console

It needs no Windows tools and no desktop session. Exit status: 0 when the
subsystem is the expected one, 1 when it is not, 2 when the file cannot be read
as a PE image.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

SUBSYSTEMS = {
    1: "native",
    2: "windows",
    3: "console",
    5: "os2",
    7: "posix",
    9: "wince",
    10: "efi",
}


class NotPortableExecutable(Exception):
    pass


def read_subsystem(data: bytes) -> str:
    """Return the name of the subsystem in the optional header of a PE image."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise NotPortableExecutable("no MZ header")
    (pe_offset,) = struct.unpack_from("<I", data, 0x3C)
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise NotPortableExecutable("no PE signature")
    # COFF header (20 bytes) follows the signature; the optional header follows
    # it. Subsystem is at offset 68 in both PE32 and PE32+.
    optional = pe_offset + 4 + 20
    if len(data) < optional + 70:
        raise NotPortableExecutable("truncated optional header")
    (magic,) = struct.unpack_from("<H", data, optional)
    if magic not in (0x10B, 0x20B):
        raise NotPortableExecutable(f"unknown optional header magic {magic:#x}")
    (subsystem,) = struct.unpack_from("<H", data, optional + 68)
    return SUBSYSTEMS.get(subsystem, f"unknown({subsystem})")


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[2] not in ("windows", "console"):
        print(__doc__, file=sys.stderr)
        return 2
    path = Path(argv[1])
    try:
        found = read_subsystem(path.read_bytes())
    except (OSError, NotPortableExecutable) as error:
        print(f"{path}: {error}", file=sys.stderr)
        return 2
    if found != argv[2]:
        print(f"{path}: subsystem is {found}, expected {argv[2]}", file=sys.stderr)
        return 1
    print(f"{path}: subsystem is {found}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
