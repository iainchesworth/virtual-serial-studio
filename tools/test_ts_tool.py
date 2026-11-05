#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for tools/ts_tool.py.

    python -m unittest discover -s tools -p "test_ts_tool.py" -v
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import ts_tool as tool

CATALOGUE = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE">
<context>
    <name>Dialog</name>
    <message>
        <location filename="../a.cpp" line="1"/>
        <source>Remove %1?</source>
        <translation type="unfinished"></translation>
    </message>
    <message numerus="yes">
        <source>Shared with %n programs</source>
        <translation type="unfinished">
            <numerusform></numerusform>
            <numerusform></numerusform>
        </translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
</context>
</TS>
"""


def catalogue() -> Path:
    path = Path(tempfile.mkdtemp()) / "vss_gui_de.ts"
    path.write_text(CATALOGUE, encoding="utf-8")
    return path


class Export(unittest.TestCase):
    def test_only_unfinished_messages_are_listed(self):
        todo = tool.export(catalogue())
        self.assertEqual([item["source"] for item in todo], ["Remove %1?", "Shared with %n programs"])
        self.assertEqual(todo[1]["numerus"], True)
        self.assertEqual(todo[1]["forms"], 2)
        self.assertEqual(todo[0]["contexts"], ["Dialog"])


class Apply(unittest.TestCase):
    def test_answers_are_written_and_finished(self):
        path = catalogue()
        problems = tool.apply(path, {"0": "%1 entfernen?", "1": ["Mit %n Programm geteilt", "Mit %n Programmen geteilt"]}, "machine translation")
        self.assertEqual(problems, [])
        self.assertEqual(tool.check(path), [])
        text = path.read_text(encoding="utf-8")
        self.assertIn("<!-- Translation origin: machine translation -->", text)
        self.assertIn("Mit %n Programmen geteilt", text)
        self.assertNotIn("unfinished", text)

    def test_a_missing_placeholder_is_refused_and_not_applied(self):
        path = catalogue()
        problems = tool.apply(path, {"0": "Entfernen?"}, None)
        self.assertEqual(len(problems), 1)
        self.assertEqual(len(tool.check(path)), 2)

    def test_a_plural_needs_the_right_number_of_forms(self):
        path = catalogue()
        problems = tool.apply(path, {"1": ["nur eine"]}, None)
        self.assertEqual(len(problems), 1)
        self.assertIn("2 plural forms", problems[0])

    def test_a_plural_form_may_leave_out_n_but_not_add_a_placeholder(self):
        self.assertEqual(tool.problems_for("A %n b", ["ein Ding", "%n Dinge"], True, 2), [])
        self.assertEqual(len(tool.problems_for("A %n b", ["%1 Ding", "%n Dinge"], True, 2)), 1)

    def test_the_origin_comment_survives_a_later_apply(self):
        path = catalogue()
        tool.apply(path, {"0": "%1 entfernen?"}, "machine translation")
        tool.apply(path, {"1": ["a %n", "b %n"]}, None)
        self.assertIn("Translation origin: machine translation", path.read_text(encoding="utf-8"))

    def test_the_order_of_placeholders_may_change(self):
        self.assertEqual(tool.problems_for("%1 to %2", "an %2 von %1", False, 0), [])
        self.assertEqual(len(tool.problems_for("%1 to %2", "an %2", False, 0)), 1)


class Groups(unittest.TestCase):
    def test_one_answer_fills_every_context_with_the_same_text(self):
        path = Path(tempfile.mkdtemp()) / "vss_gui_de.ts"
        path.write_text(
            CATALOGUE.replace(
                "</context>",
                """</context>
<context>
    <name>Other</name>
    <message>
        <source>Remove %1?</source>
        <translation type="unfinished"></translation>
    </message>
</context>""",
            ),
            encoding="utf-8",
        )
        todo = tool.export(path)
        remove = [item for item in todo if item["source"] == "Remove %1?"]
        self.assertEqual(len(remove), 1)
        self.assertEqual(remove[0]["contexts"], ["Dialog", "Other"])
        self.assertEqual(tool.apply(path, {str(remove[0]["id"]): "%1 entfernen?"}, None), [])
        text = path.read_text(encoding="utf-8")
        self.assertEqual(text.count("%1 entfernen?"), 2)


class Fill(unittest.TestCase):
    def test_a_new_context_takes_the_translation_of_the_same_text(self):
        path = Path(tempfile.mkdtemp()) / "vss_gui_de.ts"
        path.write_text(
            CATALOGUE.replace(
                "</context>",
                """</context>
<context>
    <name>Other</name>
    <message>
        <source>Cancel</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Something new</source>
        <translation type="unfinished"></translation>
    </message>
</context>""",
            ),
            encoding="utf-8",
        )
        self.assertEqual(tool.fill(path), 1)
        text = path.read_text(encoding="utf-8")
        self.assertEqual(text.count("Abbrechen"), 2)
        self.assertEqual([item["source"] for item in tool.export(path)],
                         ["Remove %1?", "Shared with %n programs", "Something new"])


class Check(unittest.TestCase):
    def test_an_unfinished_catalogue_fails(self):
        self.assertEqual(len(tool.check(catalogue())), 2)


if __name__ == "__main__":
    unittest.main()
