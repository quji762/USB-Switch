#pragma once
// Hot-plug notifications.
//
// The window registers for disk device-interface notifications; volume
// arrivals/removals (DBT_DEVTYP_VOLUME) and device tree changes
// (DBT_DEVNODES_CHANGED) reach every top-level window without registration, so
// together those cover everything the tray list cares about.

#include <string>

#include <windows.h>

namespace qe {

class DeviceNotifier {
public:
    // Registers notifications for `hwnd`. Returns false on failure - the app
    // still runs, it just stops reacting to hot-plug events.
    bool Register(HWND hwnd);
    void Unregister() noexcept;

    // True when a WM_DEVICECHANGE message should trigger a device rescan.
    static bool IsRelevantChange(UINT event, LPARAM data);

    // Short human readable description of the change, for the log.
    static std::wstring DescribeChange(UINT event, LPARAM data);

private:
    HWND hwnd_ = nullptr;
    HDEVNOTIFY notify_ = nullptr;
};

}  // namespace qe
