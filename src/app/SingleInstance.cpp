#include "app/SingleInstance.h"

#include "app/Logger.h"
#include "util/Str.h"
#include "util/Win32.h"

namespace qe {
namespace {

// The window class name is not exclusive to this program. Before waking a
// window found by class name, confirm the process behind it runs this
// executable: a same-desktop squatter with our class name would otherwise
// swallow the wake message and the second instance's request would vanish.
bool OwnedByThisExecutable(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid) return false;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) return false;
    wchar_t image[1024];
    DWORD chars = 1024;
    if (!QueryFullProcessImageNameW(process.Get(), 0, image, &chars)) return false;
    const wchar_t* their = wcsrchr(image, L'\\');
    their = their ? their + 1 : image;
    wchar_t ours[1024];
    if (!GetModuleFileNameW(nullptr, ours, 1024)) return false;
    const wchar_t* mine = wcsrchr(ours, L'\\');
    mine = mine ? mine + 1 : ours;
    return str::EqualsI(their, mine);
}

}  // namespace

bool SingleInstance::Acquire(std::wstring_view uniqueName) {
    std::wstring name = L"Local\\" + std::wstring(uniqueName);
    mutex_ = CreateMutexW(nullptr, TRUE, name.c_str());
    if (mutex_ == nullptr) {
        QE_LOG_WARN(L"CreateMutex failed (%lu); continuing without single-instance guard",
                    GetLastError());
        return true;  // Fail open: a tray tool must not refuse to start.
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        QE_LOG_INFO(L"another instance is already running");
        return false;
    }
    return true;
}

void SingleInstance::Release() noexcept {
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstance::SignalExisting(std::wstring_view windowClassName, bool openSettings) {
    const std::wstring name(windowClassName);
    // The first process can own the mutex before its tray window exists, so a
    // --settings request waits for the window. Windows failing the ownership
    // check are skipped and the search continues.
    HWND existing = nullptr;
    for (int i = 0; openSettings ? i < 40 : i < 1; ++i) {
        if (i) Sleep(50);
        HWND found = FindWindowW(name.c_str(), nullptr);
        if (found && OwnedByThisExecutable(found)) {
            existing = found;
            break;
        }
    }
    if (!existing) return false;
    if (openSettings) {
        DWORD pid = 0;
        GetWindowThreadProcessId(existing, &pid);
        if (pid) AllowSetForegroundWindow(pid);
    }
    return PostMessageW(existing, kWakeMessage, openSettings ? 1 : 0, 0) != FALSE;
}

}  // namespace qe
