#pragma once
// Single-instance guard.
//
// A tray tool has no window to activate, so a second launch is only useful to
// nudge the first instance. The mutex is per-session (Local\), which is what a
// per-user tray application wants.

#include <string>
#include <string_view>

#include <windows.h>

namespace qe {

// Message a second instance posts to the first instance's hidden window.
constexpr UINT kWakeMessage = WM_APP + 1;

class SingleInstance {
public:
    // Returns false when another instance of this session already holds the
    // named mutex; in that case the caller should exit quietly.
    bool Acquire(std::wstring_view uniqueName);
    void Release() noexcept;

    // Releases the mutex if it is still held.
    ~SingleInstance() { Release(); }

    // Posts a "wake up" message to the running instance's window.
    static bool SignalExisting(std::wstring_view windowClassName, bool openSettings);

private:
    HANDLE mutex_ = nullptr;
};

}  // namespace qe
