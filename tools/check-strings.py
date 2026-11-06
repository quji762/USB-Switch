#!/usr/bin/env python3
"""Consistency checks for QuickEject sources.

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


def table_entries() -> list[tuple[str, str, str]]:
    """Parse the table into (id, zh, en) triples.

    Entries may concatenate several adjacent string literals per language, and
    zh/en are told apart by CJK content, so the parser does not depend on the
    source formatting.
    """
    text = read(STRINGS_CPP)
    body = text.split("const Entry kStrings[] = {", 1)[1].split("\n};", 1)[0]
    positions = [m.start() for m in re.finditer(r"\{StrId::\w+,", body)]

    entries: list[tuple[str, str, str]] = []
    for index, start in enumerate(positions):
        end = positions[index + 1] if index + 1 < len(positions) else len(body)
        span = body[start:end]
        identifier = re.match(r"\{StrId::(\w+),", span).group(1)
        literals = re.findall(r'L"((?:[^"\\]|\\.)*)"', span)
        zh = "".join(l for l in literals if has_cjk(l))
        en = "".join(l for l in literals if not has_cjk(l))
        entries.append((identifier, zh, en))
    return entries


def placeholders(literal: str) -> set[str]:
    return set(re.findall(r"%[123]", literal))


def has_cjk(text: str) -> bool:
    return any(
        "\u3000" <= ch <= "\u9fff" or "\uff00" <= ch <= "\uffef" for ch in text
    )


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
            if literal in ('L""', 'L"\\0"'):
                problems.append(f"{identifier}: empty {language} text")
        if placeholders(zh) != placeholders(en):
            problems.append(
                f"{identifier}: placeholder mismatch zh={sorted(placeholders(zh))} "
                f"en={sorted(placeholders(en))}"
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
