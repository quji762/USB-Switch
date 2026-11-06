"""Check the shipped Windows executable without requesting a real ejection."""
from pathlib import Path
import json
import subprocess
import sys

exe = Path(sys.argv[1]).resolve()
for args, status, marker in [
    (["--version"], 0, "usb-switch"),
    (["--help"], 0, "--eject-media"),
    (["--help"], 0, "--files"),
    (["--eject", "missing-device", "--files"], 2, "--list"),
    (["--eject-media", "missing-device", "--files", "--json"], 2, "--list"),
    (["--eject-media"], 2, "--list"),
    (["--eject-media", "--json"], 2, "--list"),
    (["--eject-media", "missing-device", "--eject", "another-device"], 2, "--list"),
    (["--eject="], 2, "--list"),
    (["--eject", ""], 2, "--list"),
    (["--eject=   "], 2, "--list"),
    (["--eject", "missing-device-A", "-e", "missing-device-B"], 2, "--list"),
    (["--list", "--eject=missing-device"], 2, "--list"),
    (["--version", "--list"], 2, "--list"),
    (["--json"], 2, "--list"),
    (["--list", "--json", "--eject=missing-device"], 2, "--list"),
]:
    run = subprocess.run([exe, *args], capture_output=True, encoding="utf-8", timeout=10)
    if run.returncode != status or marker not in run.stdout.lower():
        raise SystemExit(f"{args!r}: unexpected exit {run.returncode}, output {run.stdout!r}")
print("CLI command and ambiguous-target checks passed")
run = subprocess.run([exe, "--list", "--json"], capture_output=True, encoding="utf-8", check=True, timeout=20)
rows = json.loads(run.stdout)
assert isinstance(rows, list)
for row in rows:
    assert isinstance(row["instanceId"], str) and row["instanceId"]
    assert isinstance(row["driveLetters"], list)
    assert isinstance(row["ejectable"], bool)
    assert row["driverMediaRemovable"] is None or isinstance(row["driverMediaRemovable"], bool)
print(f"UTF-8 JSON inventory parsed: {len(rows)} device(s)")

for action in ("--eject", "--eject-media"):
    run = subprocess.run([exe, action, "USB-Switch-missing-device", "--json"], capture_output=True, encoding="utf-8", timeout=20)
    assert run.returncode == 2 and json.loads(run.stdout) == {"status": "not_found", "retryable": False}
