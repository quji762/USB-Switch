#!/usr/bin/env python3
"""Package a locally built release and its exact Git working-tree sources.

Ordinary releases require a clean working tree. --allow-dirty is for local
review builds and is prominently recorded in the package metadata.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def source_files() -> list[tuple[str, bytes]]:
    # Includes modified, staged, and untracked non-ignored files, excludes
    # deleted files and all build products ignored by Git.
    paths = sorted(set(git("ls-files", "--cached", "--others", "--exclude-standard", "-z").decode("utf-8").split("\0")) - {""})
    files = []
    for name in paths:
        path = ROOT / name
        if path.is_symlink():
            raise ValueError(f"source symlink not supported: {name}")
        if path.is_file():
            files.append((name, path.read_bytes()))
    required = {"LICENSE", "README.md", "CMakeLists.txt", "src/main.cpp"}
    if not required.issubset({name for name, _ in files}):
        raise ValueError("required source files are missing")
    return files


def fingerprint(files: list[tuple[str, bytes]]) -> str:
    digest = hashlib.sha256()
    for name, data in files:
        digest.update(name.encode("utf-8") + b"\0")
        digest.update(hashlib.sha256(data).digest())
    return digest.hexdigest()


def require_x64(data: bytes) -> None:
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("release executable is not a Windows PE image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 26 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("release executable has an invalid PE header")
    machine = struct.unpack_from("<H", data, pe + 4)[0]
    magic = struct.unpack_from("<H", data, pe + 24)[0]
    if (machine, magic) != (0x8664, 0x20B):
        raise ValueError(f"x64 release requires AMD64 PE32+, got machine={machine:#06x}, magic={magic:#05x}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--fingerprint", action="store_true")
    parser.add_argument("--expected-fingerprint")
    args = parser.parse_args()
    files = source_files()
    digest = fingerprint(files)
    if args.fingerprint:
        print(digest)
        return 0
    if not args.expected_fingerprint or args.expected_fingerprint != digest:
        raise ValueError("source files changed during the build or no build fingerprint was supplied")
    dirty = bool(git("status", "--porcelain", "--untracked-files=all"))
    if dirty and not args.allow_dirty:
        raise ValueError("working tree is dirty; commit changes or use --allow-dirty for a local review build")
    match = re.search(r"project\(usb-switch\s+VERSION\s+(\d+\.\d+\.\d+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not match:
        raise ValueError("project version not found")
    version = match.group(1)
    dist = ROOT / "dist"
    exe = dist / "usb-switch.exe"
    setup = dist / "installer" / f"usb-switch-setup-{version}.0.exe"
    for path in (exe, setup, dist / "LICENSE", dist / "README.md"):
        if not path.is_file():
            raise ValueError(f"missing release input: {path}")
    if (dist / "LICENSE").read_bytes() != (ROOT / "LICENSE").read_bytes() or (dist / "README.md").read_bytes() != (ROOT / "README.md").read_bytes():
        raise ValueError("dist documentation is stale")
    executable_data = exe.read_bytes()
    require_x64(executable_data)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = dist / "release" / f"{version}-{stamp}"
    out.mkdir(parents=True, exist_ok=False)
    portable = out / f"usb-switch-{version}-windows-x64.zip"
    with zipfile.ZipFile(portable, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("usb-switch.exe", executable_data)
        for name in ("README.md", "LICENSE"):
            archive.write(dist / name, name)
    source = out / f"usb-switch-{version}-source.zip"
    with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in files:
            archive.writestr(f"usb-switch-{version}/{name}", data)
    with zipfile.ZipFile(source) as archive:
        prefix = f"usb-switch-{version}/"
        restored = [(name.removeprefix(prefix), archive.read(name)) for name in sorted(archive.namelist())]
        if fingerprint(restored) != digest:
            raise ValueError("archived sources differ from the build fingerprint")
    shutil.copy2(setup, out / setup.name)
    commit = git("rev-parse", "HEAD").decode().strip()
    (out / "BUILD-INFO.txt").write_text(
        f"Version: {version}\nCommit: {commit}\nWorking tree: {'DIRTY - LOCAL REVIEW ONLY' if dirty else 'clean'}\nSource SHA-256: {digest}\nBuilt UTC: {stamp}\nUnsigned; hardware and clean-system validation required.\n",
        encoding="utf-8")
    payloads = sorted(p for p in out.iterdir() if p.is_file())
    (out / "SHA256SUMS.txt").write_text("".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in payloads), encoding="ascii")
    print(out)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"release packaging failed: {exc}", file=sys.stderr)
        sys.exit(1)
