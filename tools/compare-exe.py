"""Compare two PE files and locate the differing byte ranges.

Used to confirm that a rebuild produced the same code and resources: only the
PE header timestamp (and debug paths, when present) should differ.
"""

from __future__ import annotations

import sys


def ranges(data_a: bytes, data_b: bytes) -> list[tuple[int, int, bytes, bytes]]:
    size = min(len(data_a), len(data_b))
    diffs: list[tuple[int, int, bytes, bytes]] = []
    start = None
    for index in range(size):
        if data_a[index] != data_b[index]:
            if start is None:
                start = index
        elif start is not None:
            diffs.append((start, index, data_a[start:index], data_b[start:index]))
            start = None
    if start is not None:
        diffs.append((start, size, data_a[start:], data_b[start:]))
    if len(data_a) != len(data_b):
        diffs.append((size, max(len(data_a), len(data_b)), data_a[size:], data_b[size:]))
    return diffs


def printable(data: bytes) -> str:
    return "".join(chr(b) if 32 <= b < 127 else f"\\x{b:02x}" for b in data)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: compare-exe.py <a> <b>")
        return 2
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    print(f"A: {sys.argv[1]} ({len(a)} bytes)")
    print(f"B: {sys.argv[2]} ({len(b)} bytes)")

    diffs = ranges(a, b)
    print(f"\n{len(diffs)} differing range(s):")
    for start, end, chunk_a, chunk_b in diffs:
        print(f"  offset {start:#08x}-{end:#08x} ({end - start} bytes)")
        print(f"    A: {printable(chunk_a[:48])}")
        print(f"    B: {printable(chunk_b[:48])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
