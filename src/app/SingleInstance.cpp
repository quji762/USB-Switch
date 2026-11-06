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

void SingleInstance::SignalExisting(std::wstring_view windowClassName) {
    HWND existing = FindWindowW(std::wstring(windowClassName).c_str(), nullptr);
    if (existing == nullptr) {
        QE_LOG_WARN(L"single-instance window not found");
        return;
    }
    // AllowSetForegroundWindow is deliberately not called: we only ask the
    // other instance to show a hint, we never steal focus.
    PostMessageW(existing, kWakeMessage, 0, 0);
}

}  // namespace qe
