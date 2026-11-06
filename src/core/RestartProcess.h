#pragma once
#include <windows.h>
#include <optional>

namespace qe {
// A process can actually exit with 259 (STILL_ACTIVE). Check the signaled
// state first rather than treating that exit code as a running process.
inline std::optional<DWORD> FinishedRestart(HANDLE process) {
    const DWORD wait = WaitForSingleObject(process, 0);
    if (wait == WAIT_TIMEOUT) return std::nullopt;
    if (wait == WAIT_FAILED) return GetLastError();
    DWORD code{};
    if (!GetExitCodeProcess(process, &code)) return GetLastError();
    return code;
}
}
