"""Exercise both UTF-8 redirection and the GUI executable's parent-console fallback."""
import ctypes
from ctypes import wintypes
import msvcrt
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
marker = "资料-Δ"

if sys.argv[2:] == ["console"]:
    import traceback
    def report(t, v, tb):
        Path(os.environ["APPDATA"], "console-error").write_text("".join(traceback.format_exception(t, v, tb)), encoding="utf-8")
    sys.excepthook = report
    # The parent created this helper in a private hidden console. Do not
    # change the caller's terminal or depend on its font or active code page.
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    assert kernel.SetConsoleOutputCP(437)
    run = subprocess.run([exe, "--eject", marker], close_fds=True, timeout=15)
    assert run.returncode == 2
    screen = os.open("CONOUT$", os.O_RDWR | os.O_BINARY)
    try:
        handle = wintypes.HANDLE(msvcrt.get_osfhandle(screen))
        buffer = ctypes.create_unicode_buffer(4096)
        read = wintypes.DWORD()
        assert kernel.ReadConsoleOutputCharacterW(handle, buffer, 4096, 0, ctypes.byref(read))
        assert marker in buffer[:read.value], "Unicode text missing from the console buffer"
    finally:
        os.close(screen)
    sys.exit(0)

with tempfile.TemporaryDirectory() as folder:
    env = {**os.environ, "APPDATA": folder, "LOCALAPPDATA": folder}
    target = marker * 2200
    args = [exe, "--eject", target]
    pipe = subprocess.run(args, env=env, capture_output=True, timeout=15)
    assert pipe.returncode == 2 and target in pipe.stdout.decode("utf-8")
    output = Path(folder) / "output.txt"
    with output.open("wb") as stream:
        run = subprocess.run(args, stdout=stream, env=env, timeout=15)
    assert run.returncode == 2 and output.read_bytes() == pipe.stdout
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    helper = subprocess.run([sys.executable, __file__, exe, "console"], env=env,
                   startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE, timeout=25)
    if helper.returncode:
        raise RuntimeError((Path(folder) / "console-error").read_text(encoding="utf-8"))
print("Unicode console, file and pipe output passed")
