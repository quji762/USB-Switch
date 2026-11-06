#!/usr/bin/env python3
"""Verify the resources embedded in usb-switch.exe.

Checks that the resource compiler produced what the product needs:
RT_GROUP_ICON + RT_ICON (tray icon), RT_VERSION (file properties) and
RT_MANIFEST (DPI awareness + Common-Controls 6).

Run:  python tools/check-resources.py <path-to-exe>
"""

from __future__ import annotations

from pathlib import Path
import re
import struct
import sys

RT_ICON = 3
RT_GROUP_ICON = 14
RT_VERSION = 16
RT_MANIFEST = 24

RESOURCE_NAMES = {
    RT_ICON: "RT_ICON",
    RT_GROUP_ICON: "RT_GROUP_ICON",
    RT_VERSION: "RT_VERSION",
    RT_MANIFEST: "RT_MANIFEST",
}


def rva_to_offset(rva: int, sections: list[tuple[int, int, int]]) -> int | None:
    for virtual_address, virtual_size, raw_offset in sections:
        if virtual_address <= rva < virtual_address + virtual_size:
            return raw_offset + (rva - virtual_address)
    return None


def parse_resources(data: bytes) -> dict[int, list[tuple[int, int, int]]]:
    if data[:2] != b"MZ":
        raise SystemExit("not a PE file")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise SystemExit("not a PE file")

    number_of_sections = struct.unpack_from("<H", data, pe_offset + 6)[0]
    optional_header_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    optional_header = pe_offset + 24
    magic = struct.unpack_from("<H", data, optional_header)[0]
    pe32_plus = magic == 0x20B
    data_directory = optional_header + (112 if pe32_plus else 96)
    resource_rva, resource_size = struct.unpack_from("<II", data, data_directory + 2 * 8)

    section_table = optional_header + optional_header_size
    sections: list[tuple[int, int, int]] = []
    for index in range(number_of_sections):
        entry = section_table + index * 40
        virtual_size = struct.unpack_from("<I", data, entry + 8)[0]
        virtual_address = struct.unpack_from("<I", data, entry + 12)[0]
        raw_size = struct.unpack_from("<I", data, entry + 16)[0]
        raw_offset = struct.unpack_from("<I", data, entry + 20)[0]
        sections.append((virtual_address, virtual_size or raw_size, raw_offset))

    base = rva_to_offset(resource_rva, sections)
    if base is None:
        raise SystemExit("no resource directory")

    found: dict[int, list[tuple[int, int, int]]] = {}

    def walk(offset: int, type_id: int | None, name_id: int | None) -> None:
        # IMAGE_RESOURCE_DIRECTORY: Characteristics, TimeDateStamp,
        # MajorVersion, MinorVersion, NumberOfNamedEntries, NumberOfIdEntries.
        (_characteristics, _timestamp, _major, _minor, named_entries,
         id_entries) = struct.unpack_from("<IIHHHH", data, offset)
        total = named_entries + id_entries
        for index in range(total):
            entry = offset + 16 + index * 8
            name_field, offset_to_data = struct.unpack_from("<II", data, entry)
            entry_id = name_field & 0x7FFFFFFF  # integer id (high bit clear)
            if offset_to_data & 0x80000000:
                # Subdirectory: level 1 carries the type id, level 2 the name id.
                child = base + (offset_to_data & 0x7FFFFFFF)
                if type_id is None:
                    walk(child, entry_id, None)
                else:
                    walk(child, type_id, entry_id)
            else:
                leaf = base + offset_to_data
                rva, size, _codepage, _reserved = struct.unpack_from("<IIII", data, leaf)
                file_offset = rva_to_offset(rva, sections)
                if file_offset is None:
                    continue
                found.setdefault(type_id, []).append((name_id or 0, file_offset, size))

    walk(base, None, None)
    return found


def read_version_strings(data: bytes, offset: int, size: int) -> dict[str, str]:
    """Pull the StringFileInfo values out of an RT_VERSION block.

    Each String entry is { wLength, wValueLength, wType, szKey, Value } with
    UTF-16 text padded to a DWORD boundary, so after the key's terminator any
    number of NUL padding bytes may precede the value.
    """
    block = data[offset:offset + size]
    text = block.decode("utf-16le", "ignore")
    values: dict[str, str] = {}
    for key in ("CompanyName", "FileDescription", "FileVersion", "InternalName",
                "LegalCopyright", "OriginalFilename", "ProductName", "ProductVersion"):
        position = text.find(key)
        if position < 0:
            continue
        rest = text[position + len(key):]
        # Skip the key terminator plus DWORD alignment padding.
        stripped = rest.lstrip("\x00")
        value = stripped.split("\x00", 1)[0]
        if value:
            values[key] = value
    return values


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check-resources.py <exe>")
        return 2
    data = open(sys.argv[1], "rb").read()
    resources = parse_resources(data)

    print(f"file: {sys.argv[1]} ({len(data)} bytes)")
    for type_id in sorted(resources):
        entries = resources[type_id]
        label = RESOURCE_NAMES.get(type_id, f"type {type_id}")
        print(f"  {label}: {len(entries)} entr{'y' if len(entries) == 1 else 'ies'}")

    problems: list[str] = []
    if not resources.get(RT_ICON):
        problems.append("no RT_ICON entries (tray icon missing)")
    if not resources.get(RT_GROUP_ICON):
        problems.append("no RT_GROUP_ICON entry (icon resource group missing)")
    if not resources.get(RT_VERSION):
        problems.append("no RT_VERSION (version resource missing)")
    if not resources.get(RT_MANIFEST):
        problems.append("no RT_MANIFEST (manifest not embedded)")

    # Manifest content sanity check.
    #
    # MinGW links a default-manifest.o that also registers an RT_MANIFEST with
    # id 1 (longPathAware + asInvoker). Windows loads the first matching entry,
    # which is the one from our resource script - verified at runtime by the
    # UTF-8 active code page (GetACP() == 65001) that only our manifest sets.
    # So the check looks for *a* manifest carrying our settings.
    manifests = resources.get(RT_MANIFEST, [])
    primary = [entry for entry in manifests
               if b"Common-Controls" in data[entry[1]:entry[1] + entry[2]]]
    if not primary:
        problems.append("no RT_MANIFEST entry carries Common-Controls 6")
    for name, offset, size in primary:
        manifest = data[offset:offset + size].decode("utf-8", "replace")
        if "dpiAware" not in manifest:
            problems.append(f"manifest id={name} without dpiAwareness")
        print(f"  manifest id={name} size={size}: Common-Controls + dpiAwareness present")
    if len(manifests) > len(primary):
        print(f"  (+{len(manifests) - len(primary)} extra manifest from the MinGW runtime, "
              f"shadowed by ours)")

    cmake = (Path(__file__).resolve().parents[1] / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(usb-switch\s+VERSION\s+(\d+\.\d+\.\d+)", cmake).group(1)
    # Version strings sanity check.
    for _name, offset, size in resources.get(RT_VERSION, []):
        values = read_version_strings(data, offset, size)
        for key, expected in (("CompanyName", "USB-Switch Project"),
                              ("ProductName", "USB-Switch"),
                              ("FileVersion", version)):
            if values.get(key) != expected:
                problems.append(f"version resource: {key} is {values.get(key)!r}, expected {expected!r}")
        print("  version strings: " + ", ".join(f"{k}={v}" for k, v in values.items()))

    if problems:
        print("\nFAIL:")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    print("\nOK: icon, version info and manifest are embedded")
    return 0


if __name__ == "__main__":
    sys.exit(main())
