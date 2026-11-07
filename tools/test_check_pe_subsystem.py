#!/usr/bin/env python3
# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_pe_subsystem.py.

    python3 -m unittest discover -s tools -p "test_check_pe_subsystem.py" -v
"""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_pe_subsystem as pe  # noqa: E402


def image(subsystem: int, magic: int = 0x20B) -> bytes:
    """A PE header with nothing else: enough for the subsystem field."""
    pe_offset = 0x80
    data = bytearray(pe_offset + 4 + 20 + 240)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, pe_offset)
    data[pe_offset : pe_offset + 4] = b"PE\0\0"
    optional = pe_offset + 4 + 20
    struct.pack_into("<H", data, optional, magic)
    struct.pack_into("<H", data, optional + 68, subsystem)
    return bytes(data)


class ReadSubsystem(unittest.TestCase):
    def test_windows_and_console_in_both_formats(self) -> None:
        for magic in (0x10B, 0x20B):
            self.assertEqual(pe.read_subsystem(image(2, magic)), "windows")
            self.assertEqual(pe.read_subsystem(image(3, magic)), "console")

    def test_unknown_value_is_named(self) -> None:
        self.assertEqual(pe.read_subsystem(image(99)), "unknown(99)")

    def test_rejects_other_files(self) -> None:
        for data in (b"", b"#!/bin/sh\n" * 20, b"MZ" + bytes(100)):
            with self.assertRaises(pe.NotPortableExecutable):
                pe.read_subsystem(data)

    def test_rejects_truncated_header(self) -> None:
        with self.assertRaises(pe.NotPortableExecutable):
            pe.read_subsystem(image(2)[:0x80 + 30])


class Main(unittest.TestCase):
    def run_main(self, data: bytes, expected: str) -> int:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "program.exe"
            path.write_bytes(data)
            return pe.main(["check", str(path), expected])

    def test_exit_status(self) -> None:
        self.assertEqual(self.run_main(image(2), "windows"), 0)
        self.assertEqual(self.run_main(image(3), "windows"), 1)
        self.assertEqual(self.run_main(image(3), "console"), 0)
        self.assertEqual(self.run_main(b"not an image", "windows"), 2)

    def test_missing_file_and_bad_usage(self) -> None:
        self.assertEqual(pe.main(["check", "no-such-file.exe", "windows"]), 2)
        self.assertEqual(pe.main(["check"]), 2)
        self.assertEqual(pe.main(["check", "x.exe", "gui"]), 2)


if __name__ == "__main__":
    unittest.main()
