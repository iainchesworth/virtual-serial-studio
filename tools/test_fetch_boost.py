#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the archive extraction in tools/fetch_boost.py.

    python -m unittest discover -s tools -p "test_fetch_boost.py" -v
"""

from __future__ import annotations

import io
import os
import pathlib
import tarfile
import tempfile
import unittest

import fetch_boost

PREFIX = "boost_1_92_0/boost/"
BACKSLASH = chr(92)


def build_archive(members: list[tarfile.TarInfo], payloads: dict[str, bytes]) -> tarfile.TarFile:
    """Return an open, in-memory archive holding `members`."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w") as writer:
        for member in members:
            data = payloads.get(member.name)
            if data is not None:
                member.size = len(data)
                writer.addfile(member, io.BytesIO(data))
            else:
                writer.addfile(member)
    raw.seek(0)
    return tarfile.open(fileobj=raw, mode="r")


def regular(name: str) -> tarfile.TarInfo:
    return tarfile.TarInfo(name)


def other(name: str, kind: bytes, target: str = "") -> tarfile.TarInfo:
    member = tarfile.TarInfo(name)
    member.type = kind
    member.linkname = target
    return member


class Unpack(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.base = pathlib.Path(self._tmp.name).resolve()
        self.dest = self.base / "dest"
        self.outside = self.base / "outside.txt"

    def unpack(self, members: list[tarfile.TarInfo], payloads: dict[str, bytes] | None = None) -> int:
        with build_archive(members, payloads or {}) as tar:
            return fetch_boost.unpack(tar, PREFIX, self.dest)

    def assert_refused(self, members: list[tarfile.TarInfo], payloads: dict[str, bytes] | None = None) -> None:
        with self.assertRaises(fetch_boost.UnsafeArchive):
            self.unpack(members, payloads)
        self.assertFalse(self.outside.exists())

    def test_unpacks_files_and_directories_without_the_prefix(self) -> None:
        directory = other(PREFIX + "asio", tarfile.DIRTYPE)
        header = regular(PREFIX + "asio/io_context.hpp")
        top = regular(PREFIX + "asio.hpp")
        count = self.unpack(
            [other(PREFIX, tarfile.DIRTYPE), directory, header, top],
            {header.name: b"// io_context", top.name: b"// asio"},
        )
        self.assertEqual(count, 2)
        self.assertEqual((self.dest / "asio" / "io_context.hpp").read_bytes(), b"// io_context")
        self.assertEqual((self.dest / "asio.hpp").read_bytes(), b"// asio")

    def test_members_outside_the_prefix_are_skipped(self) -> None:
        libs = regular("boost_1_92_0/libs/readme.txt")
        self.assertEqual(self.unpack([libs], {libs.name: b"x"}), 0)
        self.assertFalse((self.dest / "libs").exists())

    def test_refuses_a_parent_directory_part(self) -> None:
        evil = regular(PREFIX + "../../outside.txt")
        self.assert_refused([evil], {evil.name: b"x"})

    def test_refuses_a_parent_directory_part_in_the_middle(self) -> None:
        evil = regular(PREFIX + "asio/../../../outside.txt")
        self.assert_refused([evil], {evil.name: b"x"})

    def test_refuses_a_backslash_parent_directory_part(self) -> None:
        evil = regular(PREFIX + "..BS..BSoutside.txt".replace("BS", BACKSLASH))
        self.assert_refused([evil], {evil.name: b"x"})

    def test_refuses_an_absolute_path(self) -> None:
        evil = regular(PREFIX + "/" + str(self.outside).replace("\\", "/").lstrip("/"))
        self.assert_refused([evil], {evil.name: b"x"})

    def test_refuses_a_windows_drive_path(self) -> None:
        evil = regular(PREFIX + "C:/outside.txt")
        self.assert_refused([evil], {evil.name: b"x"})

    def test_refuses_a_symbolic_link(self) -> None:
        self.assert_refused([other(PREFIX + "link", tarfile.SYMTYPE, "../outside.txt")])

    def test_refuses_a_hard_link(self) -> None:
        self.assert_refused([other(PREFIX + "link", tarfile.LNKTYPE, "boost_1_92_0/boost/asio.hpp")])

    def test_refuses_a_device_node(self) -> None:
        self.assert_refused([other(PREFIX + "node", tarfile.CHRTYPE)])

    def test_refuses_a_write_through_a_link_already_in_the_destination(self) -> None:
        self.dest.mkdir()
        try:
            os.symlink(self.base, self.dest / "escape", target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("this host cannot create symbolic links")
        evil = regular(PREFIX + "escape/outside.txt")
        self.assert_refused([evil], {evil.name: b"x"})


class DestinationFor(unittest.TestCase):
    def test_a_name_inside_the_destination_resolves_under_it(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            dest = pathlib.Path(tmp).resolve()
            self.assertEqual(fetch_boost.destination_for("a/b.hpp", dest), dest / "a" / "b.hpp")

    def test_an_empty_name_and_a_nul_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            dest = pathlib.Path(tmp).resolve()
            for name in ("", "a\x00b"):
                with self.assertRaises(fetch_boost.UnsafeArchive):
                    fetch_boost.destination_for(name, dest)


if __name__ == "__main__":
    unittest.main()
