#include "app/SingleInstance.h"

#include "app/Logger.h"
#include "util/Str.h"

namespace qe {

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
    HWND existing = FindWindowW(name.c_str(), nullptr);
    // The first process can own the mutex before its tray window exists.
    for (int i = 0; !existing && openSettings && i < 40; ++i) {
        Sleep(50);
        existing = FindWindowW(name.c_str(), nullptr);
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
