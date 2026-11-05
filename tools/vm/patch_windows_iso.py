#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a copy of a Windows install ISO that boots without "Press any key to boot from CD".

    patch_windows_iso.py --iso E:/ISOs/Win10_22H2_English_x64.iso --out Win10-noprompt.iso

Windows media boot through a UEFI image, efi/microsoft/boot/efisys.bin, that asks for a key press, and
nobody is at the console of a test guest. The same media carries efisys_noprompt.bin, a boot image of
the same size without the question. This script copies the ISO and overwrites the bytes of the UEFI boot
image in the copy with the contents of efisys_noprompt.bin. It writes at the place the El Torito boot
catalog points to, and it reads the bytes back to check them, so a layout that is not what it expects
fails instead of producing a bad ISO. The original is never modified. The guest must boot with UEFI.
It needs the pycdlib package, to read the original.
"""

from __future__ import annotations

import argparse
import io
import shutil
import sys
from pathlib import Path

BOOT_IMAGE = "/efi/microsoft/boot/efisys.bin"
NOPROMPT_IMAGE = "/efi/microsoft/boot/efisys_noprompt.bin"
SECTOR = 2048


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

    source = pycdlib.PyCdlib()
    source.open(str(args.iso))
    try:
        udf = source.has_udf()

        def kwargs(path: str) -> dict:
            # Windows media are UDF; older ones are ISO 9660 with Joliet.
            return {"udf_path": path} if udf else {"joliet_path": path}

        prompting = io.BytesIO()
        quiet = io.BytesIO()
        source.get_file_from_iso_fp(prompting, **kwargs(BOOT_IMAGE))
        source.get_file_from_iso_fp(quiet, **kwargs(NOPROMPT_IMAGE))
        catalog = source.eltorito_boot_catalog
        entries = [entry for section in catalog.sections for entry in section.section_entries]
    finally:
        source.close()

    prompting_bytes = prompting.getvalue()
    quiet_bytes = quiet.getvalue()
    if len(prompting_bytes) != len(quiet_bytes):
        print("efisys.bin and efisys_noprompt.bin differ in size; refusing to patch", file=sys.stderr)
        return 1
    if prompting_bytes == quiet_bytes:
        print("efisys.bin is already the no-prompt image", file=sys.stderr)
        return 1

    # The UEFI entry is the one whose image is efisys.bin. Find it by its bytes.
    offset = None
    with open(args.iso, "rb") as original:
        for entry in entries:
            original.seek(entry.load_rba * SECTOR)
            if original.read(len(prompting_bytes)) == prompting_bytes:
                offset = entry.load_rba * SECTOR
                break
    if offset is None:
        print("no El Torito entry points at efisys.bin; refusing to patch", file=sys.stderr)
        return 1

    args.out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.iso, args.out)
    try:
        with open(args.out, "r+b") as patched:
            patched.seek(offset)
            patched.write(quiet_bytes)
            patched.flush()
            patched.seek(offset)
            if patched.read(len(quiet_bytes)) != quiet_bytes:
                raise RuntimeError("the patched bytes did not read back")
    except Exception:
        args.out.unlink(missing_ok=True)
        raise

    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
