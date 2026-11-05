#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Work on Qt Linguist catalogues (.ts files) as JSON.

    python3 tools/ts_tool.py export apps/vss_gui/translations/vss_gui_de.ts todo.json
    python3 tools/ts_tool.py apply  apps/vss_gui/translations/vss_gui_de.ts done.json --origin "..."
    python3 tools/ts_tool.py fill   apps/vss_gui/translations/vss_gui_de.ts
    python3 tools/ts_tool.py check  apps/vss_gui/translations/vss_gui_de.ts

`export` writes every group of messages with the same source text that has an
unfinished message as {"id", "contexts", "source", "comment", "numerus", "forms"}. A translator (a person or a program) answers with
a JSON object that maps each id to the translation: a string, or for a message
with "numerus" a list of "forms" strings, one for each plural form of the
language. `apply` writes the answers into the catalogue, refuses an answer whose
placeholders (%1, %2, %n, %L1) differ from the source, and marks the message
finished. `fill` copies a finished translation to every unfinished message with the same source
text, which is what `update_translations` leaves when a text moves to another context.
`check` fails when any message is unfinished or empty or has different
placeholders from its source.

Only the translation elements are touched: the file keeps its layout, the
locations and the comments that lupdate wrote.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from xml.etree import ElementTree

PLACEHOLDER = re.compile(r"%(?:L?\d+|n|Ln)")


def placeholders(text: str) -> list[str]:
    """The placeholders of a text, sorted, so that moving one is allowed."""
    return sorted(PLACEHOLDER.findall(text))


def messages(root: ElementTree.Element):
    """Every (context name, message element) of a catalogue, in file order."""
    for context in root.iter("context"):
        name = context.findtext("name", default="")
        for message in context.iter("message"):
            yield name, message


def is_unfinished(message: ElementTree.Element) -> bool:
    translation = message.find("translation")
    if translation is None:
        return True
    if translation.get("type") in ("unfinished", "vanished", "obsolete"):
        return translation.get("type") == "unfinished"
    return False


def forms_of(message: ElementTree.Element) -> int:
    """How many plural forms the catalogue asks for (0 for a plain message)."""
    if message.get("numerus") != "yes":
        return 0
    translation = message.find("translation")
    forms = translation.findall("numerusform") if translation is not None else []
    return max(len(forms), 1)


def load(path: Path) -> ElementTree.ElementTree:
    return ElementTree.parse(path)


def group_key(message: ElementTree.Element) -> tuple:
    """Messages with the same source, comment and kind get one translation."""
    return (
        message.findtext("source", default=""),
        message.findtext("comment", default=""),
        message.get("numerus") == "yes",
    )


def groups(root: ElementTree.Element) -> dict[tuple, list[int]]:
    """The message indexes of each group, in file order."""
    found: dict[tuple, list[int]] = {}
    for index, (_, message) in enumerate(messages(root)):
        found.setdefault(group_key(message), []).append(index)
    return found


def export(path: Path) -> list[dict]:
    """One item for each group that has an unfinished message. Its id is the
    index of the first message of the group, and an answer to it is applied to
    every message of the group."""
    root = load(path).getroot()
    everything = list(messages(root))
    todo = []
    for members in groups(root).values():
        if not any(is_unfinished(everything[index][1]) for index in members):
            continue
        context, message = everything[members[0]]
        todo.append(
            {
                "id": members[0],
                "contexts": sorted({everything[index][0] for index in members}),
                "source": message.findtext("source", default=""),
                "comment": message.findtext("comment", default=""),
                "numerus": message.get("numerus") == "yes",
                "forms": forms_of(message),
            }
        )
    return todo


def problems_for(source: str, answer: str | list[str], numerus: bool, forms: int) -> list[str]:
    """What is wrong with one answer, as sentences."""
    problems = []
    texts = answer if isinstance(answer, list) else [answer]
    if numerus != isinstance(answer, list):
        problems.append("a plural message needs a list of forms, a plain one a string" )
        return problems
    if numerus and len(texts) != forms:
        problems.append(f"needs {forms} plural forms, got {len(texts)}")
    for text in texts:
        if not isinstance(text, str) or not text.strip():
            problems.append("an empty translation")
            continue
        if not numerus and placeholders(text) != placeholders(source):
            problems.append(f"placeholders {placeholders(text)} differ from the source {placeholders(source)}")
        if numerus:
            # A plural form may drop %n (one -> "one program") but may not add
            # or change any other placeholder.
            extra = [p for p in placeholders(text) if p not in placeholders(source)]
            if extra:
                problems.append(f"placeholders {extra} are not in the source")
    return problems


def apply(path: Path, answers: dict[str, str | list[str]], origin: str | None) -> list[str]:
    """Write the answers into the catalogue; return the problems and apply none
    of the answers that have one. An answer keyed by the id of a group's first
    message is written to every message of the group."""
    tree = load(path)
    root = tree.getroot()
    everything = list(messages(root))
    problems: list[str] = []
    for members in groups(root).values():
        key = str(members[0])
        if key not in answers:
            continue
        context, first = everything[members[0]]
        source = first.findtext("source", default="")
        numerus = first.get("numerus") == "yes"
        found = problems_for(source, answers[key], numerus, forms_of(first))
        if found:
            problems.extend(f"{members[0]} [{context}] {source!r}: {text}" for text in found)
            continue
        for index in members:
            message = everything[index][1]
            translation = message.find("translation")
            if translation is None:
                translation = ElementTree.SubElement(message, "translation")
            translation.attrib.pop("type", None)
            for child in list(translation):
                translation.remove(child)
            translation.text = None
            if numerus:
                for text in answers[key]:
                    ElementTree.SubElement(translation, "numerusform").text = text
            else:
                translation.text = answers[key]
    write(path, tree, origin)
    return problems


def fill(path: Path) -> int:
    """Copy a finished translation to every unfinished message with the same
    source and comment. Returns how many messages were filled."""
    tree = load(path)
    root = tree.getroot()
    everything = list(messages(root))
    filled = 0
    for members in groups(root).values():
        done = [index for index in members if not is_unfinished(everything[index][1])]
        if not done:
            continue
        source = everything[done[0]][1].find("translation")
        for index in members:
            message = everything[index][1]
            if not is_unfinished(message):
                continue
            translation = message.find("translation")
            if translation is None:
                translation = ElementTree.SubElement(message, "translation")
            translation.attrib.pop("type", None)
            for child in list(translation):
                translation.remove(child)
            translation.text = source.text
            for form in source.findall("numerusform"):
                ElementTree.SubElement(translation, "numerusform").text = form.text
            filled += 1
    write(path, tree, None)
    return filled


def write(path: Path, tree: ElementTree.ElementTree, origin: str | None) -> None:
    root = tree.getroot()
    ElementTree.indent(tree, space="    ")
    body = ElementTree.tostring(root, encoding="unicode")
    header = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n'
    comment = ""
    if not origin and path.exists():
        # Keep the origin note an earlier apply wrote: parsing drops comments.
        kept = re.search(r"<!-- Translation origin: (.*?) -->", path.read_text(encoding="utf-8"))
        origin = kept.group(1) if kept else None
    if origin:
        comment = f"<!-- Translation origin: {origin} -->\n"
    path.write_text(header + comment + body + "\n", encoding="utf-8", newline="\n")


def check(path: Path) -> list[str]:
    problems = []
    for index, (context, message) in enumerate(messages(load(path).getroot())):
        source = message.findtext("source", default="")
        if is_unfinished(message):
            problems.append(f"{index} [{context}] {source!r}: unfinished")
            continue
        translation = message.find("translation")
        numerus = message.get("numerus") == "yes"
        if numerus:
            answer: str | list[str] = [form.text or "" for form in translation.findall("numerusform")]
        else:
            answer = translation.text or ""
        problems.extend(
            f"{index} [{context}] {source!r}: {text}"
            for text in problems_for(source, answer, numerus, forms_of(message))
        )
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    one = sub.add_parser("export")
    one.add_argument("ts", type=Path)
    one.add_argument("json", type=Path)
    two = sub.add_parser("apply")
    two.add_argument("ts", type=Path)
    two.add_argument("json", type=Path)
    two.add_argument("--origin", help="where the translation came from, kept as a comment in the file")
    four = sub.add_parser("fill")
    four.add_argument("ts", type=Path)
    three = sub.add_parser("check")
    three.add_argument("ts", type=Path)
    args = parser.parse_args(argv)

    if args.command == "export":
        todo = export(args.ts)
        args.json.write_text(json.dumps(todo, ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"{len(todo)} unfinished messages written to {args.json}")
        return 0
    if args.command == "apply":
        answers = json.loads(args.json.read_text(encoding="utf-8"))
        problems = apply(args.ts, answers, args.origin)
        for problem in problems:
            print(problem, file=sys.stderr)
        print(f"{len(answers) - len(problems)} applied, {len(problems)} refused")
        return 1 if problems else 0
    if args.command == "fill":
        print(f"{fill(args.ts)} messages filled from a translation of the same text")
        return 0
    problems = check(args.ts)
    for problem in problems[:50]:
        print(problem, file=sys.stderr)
    if len(problems) > 50:
        print(f"... and {len(problems) - 50} more", file=sys.stderr)
    print("complete" if not problems else f"{len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
