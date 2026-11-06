#pragma once
// The hidden window behind the tray icon: message pump target, tray menu owner
// and the place where device changes turn into UI updates.

#include <unordered_map>
#include <vector>

#include <windows.h>

#include "core/DeviceManager.h"
#include "core/DeviceNotifier.h"
#include "ui/TrayIcon.h"

namespace qe {

class MainWindow {
public:
    // Unique window class name; also how a second instance finds this window.
    static constexpr wchar_t kClassName[] = L"USBSwitchTrayWindow";

    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Registers the window class, creates the hidden window (which creates the
    // tray icon and registers device notifications) and does the first scan.
    bool Create();
    void Destroy() noexcept;

    HWND Handle() const noexcept { return hwnd_; }
    bool TrayCreationFailed() const noexcept { return trayCreationFailed_; }

    // Called when a second instance starts: show a hint instead of a window.
    void HandleSecondInstance();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // Takes the window handle as a parameter on purpose: during WM_NCCREATE and
    // WM_CREATE the hwnd_ member is still null, and DefWindowProcW must never
    // be called with it.
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool OnCreate(HWND hwnd);
    void OnDestroy();
    void OnTrayNotify(WPARAM wParam, LPARAM lParam);
    void OnDeviceChange(UINT event, LPARAM data);
    void OnRescanTimer();
    // position == nullptr means "use the cursor" (mouse activation).
    void ShowTrayMenu(const POINT* position = nullptr);
    void OnMenuCommand(UINT commandId);
    void RefreshDevices();
    void RestoreTrayIcon();
    void RebuildTrayTip();
    void EjectDevice(const Device& device);
    void OpenSettings();
    void OpenAbout();
    HICON LoadTrayIcon() const;
    static int GetTrayIconSize();
    HICON LoadTrayIconAt(int size) const;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;

    DeviceManager devices_;
    DeviceNotifier notifier_;
    TrayIcon tray_;

    // instance id -> display name of the previous scan, for "device removed".
    std::unordered_map<std::wstring, std::wstring> previousNames_;
    // Suppresses connect/disconnect balloons for the initial scan at startup.
    bool firstScan_ = true;
    bool trayCreationFailed_ = false;
    // True while icon_ came from LoadImage and must be destroyed.
    mutable bool ownsIcon_ = true;
};

}  // namespace qe
