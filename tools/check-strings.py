#!/usr/bin/env python3
"""Consistency checks for USB-Switch sources.

Verifies the two things that are easy to get wrong by hand:

1. The StrId enum in src/i18n/Strings.h and the table in src/i18n/Strings.cpp
   have the same entries in the same order (a static_assert only guards the
   count), and both translations are non-empty and use the same %1/%2/%3
   placeholders.
2. Every source file compiled by CMakeLists.txt actually exists, and every
   .cpp under src/ is listed there.

Run:  python tools/check-strings.py        (exit 0 = clean)
"""

from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STRINGS_H = os.path.join(ROOT, "src", "i18n", "Strings.h")
STRINGS_CPP = os.path.join(ROOT, "src", "i18n", "Strings.cpp")
CMAKE = os.path.join(ROOT, "CMakeLists.txt")


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def enum_ids() -> list[str]:
    text = read(STRINGS_H)
    body = text.split("enum class StrId", 1)[1]
    body = body.split("};", 1)[0]
    ids = re.findall(r"^\s{4}(\w+),", body, re.MULTILINE)
    return [i for i in ids if i != "kCount"]


def strip_comments(text: str) -> str:
    """Drop // and /* */ comments without disturbing string literals.

    The table only carries wide string literals, so a double quote is the one
    delimiter that matters; a backslash escape inside a literal is skipped so a
    \" can never end the string early. A URL such as "https://..." inside a
    literal is therefore left alone, while a real comment is removed.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    in_string = False
    while i < n:
        c = text[i]
        if in_string:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_string = False
            i += 1
            continue
        if c == '"':
            in_string = True
            out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i = min(i + 2, n)
            continue
        out.append(c)
        i += 1
    return "".join(out)


def split_columns(rest: str) -> tuple[str, str] | None:
    """Split an entry body at the first comma that is not inside a literal.

    Everything before that comma is the Chinese column and everything after it
    the English one. A comma inside a string - or a fullwidth comma in Chinese
    text - can never be mistaken for the separator, because the scan tracks
    whether it is currently inside a quoted literal.
    """
    in_string = False
    i = 0
    n = len(rest)
    while i < n:
        c = rest[i]
        if in_string:
            if c == "\\" and i + 1 < n:
                i += 2
                continue
            if c == '"':
                in_string = False
            i += 1
            continue
        if c == '"':
            in_string = True
            i += 1
            continue
        if c == ",":
            return rest[:i], rest[i + 1:]
        i += 1
    return None


def join_literals(source: str) -> str:
    """Concatenate the wide-string literals in one column.

    A macro such as QUICKEJECT_VERSION_STRING_W between two literals is simply
    absent from the match: it expands to a version number and never carries a
    %N placeholder, so skipping it keeps the two columns directly comparable.
    """
    return "".join(re.findall(r'L"((?:[^"\\]|\\.)*)"', source))


def table_entries() -> list[tuple[str, str, str]]:
    """Parse the table into (id, zh, en) triples.

    The two columns are found structurally: the first top-level comma in the
    entry separates them, and each column may be several adjacent literals
    joined together. The result no longer depends on guessing a language from
    CJK content, so entries whose two columns share a language (e.g. the
    language names themselves) parse correctly.
    """
    text = strip_comments(read(STRINGS_CPP))
    body = text.split("const Entry kStrings[] = {", 1)[1].split("\n};", 1)[0]
    positions = [m.start() for m in re.finditer(r"\{StrId::\w+,", body)]

    entries: list[tuple[str, str, str]] = []
    for index, start in enumerate(positions):
        end = positions[index + 1] if index + 1 < len(positions) else len(body)
        span = body[start:end]
        header = re.match(r"\{StrId::(\w+),", span)
        identifier = header.group(1)
        columns = split_columns(span[header.end():])
        if columns is None:
            entries.append((identifier, "", ""))
            continue
        entries.append((identifier, join_literals(columns[0]), join_literals(columns[1])))
    return entries


def placeholders(literal: str) -> set[str]:
    return set(re.findall(r"%[123]", literal))


def main() -> int:
    problems: list[str] = []

    # --- 1. enum vs table -------------------------------------------------
    ids = enum_ids()
    entries = table_entries()

    if len(entries) != len(ids):
        problems.append(
            f"count mismatch: {len(ids)} StrId values vs {len(entries)} table entries"
        )
    for index, (identifier, _zh, _en) in enumerate(entries):
        if index < len(ids) and identifier != ids[index]:
            problems.append(
                f"order mismatch at #{index}: enum has '{ids[index]}', table has '{identifier}'"
            )

    seen: set[str] = set()
    for identifier, zh, en in entries:
        if identifier in seen:
            problems.append(f"duplicate table entry: {identifier}")
        seen.add(identifier)
        for language, literal in (("zh", zh), ("en", en)):
            if not literal:
                problems.append(f"{identifier}: empty {language} text")
        zh_placeholders = placeholders(zh)
        en_placeholders = placeholders(en)
        if zh_placeholders != en_placeholders:
            problems.append(
                f"{identifier}: placeholder mismatch zh={sorted(zh_placeholders)} "
                f"en={sorted(en_placeholders)}"
            )

    # --- 2. CMake source list vs files on disk ----------------------------
    cmake = read(CMAKE)
    listed = re.findall(r"^\s{4}(src/[\w./-]+\.cpp)\s*$", cmake, re.MULTILINE)
    for relative in listed:
        if not os.path.isfile(os.path.join(ROOT, relative.replace("/", os.sep))):
            problems.append(f"CMakeLists.txt lists missing file: {relative}")

    on_disk = set()
    for directory, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for name in files:
            if name.endswith(".cpp"):
                full = os.path.join(directory, name)
                on_disk.add(os.path.relpath(full, ROOT).replace(os.sep, "/"))
    for relative in sorted(on_disk - set(listed)):
        problems.append(f"source file not in CMakeLists.txt: {relative}")

    # --- report ------------------------------------------------------------
    print(f"StrId values : {len(ids)}")
    print(f"Table entries: {len(entries)}")
    print(f"Sources listed in CMakeLists.txt: {len(listed)}")
    if problems:
        print("\nFAIL:")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    print("\nOK: string table and build list are consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
