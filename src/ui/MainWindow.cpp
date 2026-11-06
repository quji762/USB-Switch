#include "ui/MainWindow.h"

#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <algorithm>

#include "app/AppInfo.h"
#include "app/AutoStart.h"
#include "app/Logger.h"
#include "app/Settings.h"
#include "app/SingleInstance.h"
#include "core/Ejector.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "resource.h"
#include "ui/Dialogs.h"
#include "util/Str.h"

namespace qe {
namespace {

// Name of the icon resource declared in version.rc (a *named* resource, so it
// cannot drift from resources/resource.h).
constexpr wchar_t kIconResourceName[] = L"USBSwitchIcon";

// Tray callback message. kWakeMessage (WM_APP + 1) is reserved by
// SingleInstance for "another instance started".
constexpr UINT kTrayMessage = WM_APP + 2;

// Tray menu command ids. Device ids are allocated per menu build and mapped
// back to devices through menuCommands_.
constexpr UINT kDeviceCommandBase = 0x1001;
constexpr UINT kCommandRefresh = 0x2001;
constexpr UINT kCommandSettings = 0x2002;
constexpr UINT kCommandAbout = 0x2003;
constexpr UINT kCommandExit = 0x2004;

// One physical insertion produces several WM_DEVICECHANGE messages (disk
// interface, volume, device tree); a short timer collapses them into one scan.
constexpr UINT_PTR kRescanTimerId = 1;
constexpr UINT kRescanDelayMs = 250;

}  // namespace

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {}

MainWindow::~MainWindow() { Destroy(); }

bool MainWindow::Create() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &MainWindow::WndProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        QE_LOG_ERROR(L"RegisterClassEx failed: %lu", GetLastError());
        return false;
    }

    // A hidden window: no WS_VISIBLE, so the app lives only in the tray.
    hwnd_ = CreateWindowExW(0, kClassName, QUICKEJECT_APP_NAME_W, WS_OVERLAPPED, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance_,
                            this);
    if (hwnd_ == nullptr) {
        QE_LOG_ERROR(L"CreateWindowEx failed: %lu", GetLastError());
        return false;
    }
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    return true;
}

void MainWindow::Destroy() noexcept {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        // If the instance pointer cannot be stored, every later message would
        // find self == nullptr and silently fall through to DefWindowProcW,
        // leaving a window that does nothing. Aborting the creation is the
        // documented response.
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)) == 0 &&
            GetLastError() != ERROR_SUCCESS) {
            return FALSE;
        }
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    // Always the parameter: during creation hwnd_ is still null.
    if (self == nullptr) return DefWindowProcW(hwnd, message, wParam, lParam);
    return self->HandleMessage(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            return OnCreate(hwnd) ? 0 : -1;
        case WM_TIMER:
            OnRescanTimer();
            return 0;
        case WM_DPICHANGED: {
            // The manifest declares PerMonitorV2, so the icon is reloaded at the
            // new DPI instead of staying at the size captured at startup.
            const UINT dpi = HIWORD(wParam);
            const int size = dpi > 0 ? MulDiv(16, static_cast<int>(dpi), 96) : 16;
            if (icon_ != nullptr && ownsIcon_) {
                DestroyIcon(icon_);
                icon_ = nullptr;
            }
            icon_ = LoadTrayIconAt(size);
            tray_.UpdateIcon(icon_);
            return 0;
        }
        case WM_DEVICECHANGE:
            OnDeviceChange(static_cast<UINT>(wParam), lParam);
            return 0;
        case kTrayMessage:
            OnTrayNotify(lParam);
            return 0;
        case kWakeMessage:
            HandleSecondInstance();
            return 0;
        case WM_DESTROY:
            OnDestroy();
            return 0;
        case WM_NCDESTROY:
            hwnd_ = nullptr;
            return DefWindowProcW(hwnd, message, wParam, lParam);
        default:
            break;
    }

    // Explorer restarts drop every tray icon; re-add ours, then restore the
    // device-count suffix in the tooltip (Recreate alone would lose it).
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        QE_LOG_INFO(L"taskbar restarted; recreating tray icon");
        tray_.Recreate(icon_, i18n::I18n::Instance().Get(i18n::StrId::AppTooltip));
        RebuildTrayTip();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool MainWindow::OnCreate(HWND hwnd) {
    // The handle is valid from WM_CREATE onwards - store it before anything
    // else so the handlers below can use hwnd_.
    hwnd_ = hwnd;

    icon_ = LoadTrayIcon();
    if (icon_ == nullptr) {
        QE_LOG_WARN(L"tray icon resource missing; using the default application icon");
    }

    if (!tray_.Create(hwnd_, kTrayMessage, icon_, i18n::I18n::Instance().Get(i18n::StrId::AppTooltip))) {
        QE_LOG_ERROR(L"could not create the tray icon");
        trayCreationFailed_ = true;
        if (icon_ != nullptr && ownsIcon_) DestroyIcon(icon_);
        icon_ = nullptr;
        return false;
    }
    notifier_.Register(hwnd_);
    RefreshDevices();
    return true;
}

void MainWindow::OnDestroy() {
    notifier_.Unregister();
    tray_.Remove();
    if (icon_ != nullptr) {
        // LoadImage (not LoadIcon) gives us an icon we own; the stock fallback
        // icon is shared and must not be destroyed.
        if (ownsIcon_) DestroyIcon(icon_);
        icon_ = nullptr;
    }
    QE_LOG_INFO(L"shutting down");
    PostQuitMessage(0);
}

HICON MainWindow::LoadTrayIcon() const { return LoadTrayIconAt(GetTrayIconSize()); }

int MainWindow::GetTrayIconSize() {
    // GetSystemMetrics is DPI aware for PerMonitorV2 processes, but on a
    // mixed-DPI setup it reports the primary monitor's value; callers that
    // know the target DPI (WM_DPICHANGED) pass it explicitly.
    const int size = GetSystemMetrics(SM_CXSMICON);
    return size > 0 ? size : 16;
}

HICON MainWindow::LoadTrayIconAt(int size) const {
    HICON icon = static_cast<HICON>(
        LoadImageW(instance_, kIconResourceName, IMAGE_ICON, size, size, 0));
    if (icon != nullptr) {
        ownsIcon_ = true;
        return icon;
    }
    QE_LOG_WARN(L"icon resource \"%ls\" not found; using the stock application icon",
                kIconResourceName);
    ownsIcon_ = false;
    return LoadIconW(nullptr, IDI_APPLICATION);
}

void MainWindow::OnTrayNotify(LPARAM lParam) {
    // WM_CONTEXTMENU carries the icon position in lParam, which the shell
    // supplies when the menu is invoked from the keyboard - using the cursor
    // position instead would open the menu on the wrong monitor.
    POINT fromShell{};
    fromShell.x = GET_X_LPARAM(lParam);
    fromShell.y = GET_Y_LPARAM(lParam);

    switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(&fromShell);
            break;
        case WM_LBUTTONUP:
        case NIN_SELECT:
            ShowTrayMenu();
            break;
        default:
            break;
    }
}

void MainWindow::OnDeviceChange(UINT event, LPARAM data) {
    if (!DeviceNotifier::IsRelevantChange(event, data)) return;
    QE_LOG_DEBUG(L"device change: %ls", DeviceNotifier::DescribeChange(event, data).c_str());
    SetTimer(hwnd_, kRescanTimerId, kRescanDelayMs, nullptr);
}

void MainWindow::OnRescanTimer() {
    KillTimer(hwnd_, kRescanTimerId);
    RefreshDevices();
}

void MainWindow::RefreshDevices() {
    const auto& strings = i18n::I18n::Instance();
    const bool notify = Settings::Instance().Data().showNotifications;

    const auto before = previousNames_;
    if (!devices_.Refresh()) {
        tray_.ShowBalloon(QUICKEJECT_APP_NAME_W, strings.Get(i18n::StrId::ErrorEnumFailed), NIIF_WARNING);
        return;
    }

    if (!firstScan_) {
        for (const auto& device : devices_.Devices()) {
            if (before.find(device.instanceId) == before.end()) {
                QE_LOG_INFO(L"device connected: %ls", device.displayName.c_str());
                if (notify) {
                    tray_.ShowBalloon(QUICKEJECT_APP_NAME_W,
                                      strings.Format(i18n::StrId::DeviceInserted, device.displayName));
                }
            }
        }
        for (const auto& entry : before) {
            if (!devices_.FindByInstanceId(entry.first)) {
                QE_LOG_INFO(L"device disconnected: %ls", entry.second.c_str());
                if (notify) {
                    tray_.ShowBalloon(QUICKEJECT_APP_NAME_W,
                                      strings.Format(i18n::StrId::DeviceRemoved, entry.second));
                }
            }
        }
    }
    firstScan_ = false;

    previousNames_.clear();
    for (const auto& device : devices_.Devices()) {
        previousNames_[device.instanceId] = device.displayName;
    }
    RebuildTrayTip();
}

void MainWindow::RebuildTrayTip() {
    const auto& strings = i18n::I18n::Instance();
    std::wstring tooltip = strings.Get(i18n::StrId::AppTooltip);
    if (!devices_.Devices().empty()) {
        tooltip += str::Format(L" (%zu)", devices_.Devices().size());
    }
    tray_.UpdateTooltip(tooltip);
}

void MainWindow::ShowTrayMenu(const POINT* position) {
    const auto& strings = i18n::I18n::Instance();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        QE_LOG_ERROR(L"CreatePopupMenu failed: %lu", GetLastError());
        return;
    }

    menuCommands_.clear();
    UINT itemPosition = 0;
    const auto& devices = devices_.Devices();
    if (devices.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, strings.Get(i18n::StrId::MenuNoDevices));
    } else {
        for (const auto& device : devices) {
            const UINT commandId = kDeviceCommandBase + itemPosition;
            menuCommands_[commandId] = device.instanceId;
            AppendMenuW(menu, MF_STRING, commandId,
                        strings.Format(i18n::StrId::MenuEjectItem, device.displayName).c_str());
            ++itemPosition;
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    AppendMenuW(menu, MF_STRING, kCommandRefresh, strings.Get(i18n::StrId::MenuRefresh));
    AppendMenuW(menu, MF_STRING, kCommandSettings, strings.Get(i18n::StrId::MenuSettings));
    AppendMenuW(menu, MF_STRING, kCommandAbout, strings.Get(i18n::StrId::MenuAbout));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, strings.Get(i18n::StrId::MenuExit));

    POINT cursor{};
    if (position != nullptr) {
        cursor = *position;
    } else {
        GetCursorPos(&cursor);
    }
    // Without SetForegroundWindow the popup menu does not dismiss on outside
    // clicks; the WM_NULL post restores correct keyboard focus afterwards.
    SetForegroundWindow(hwnd_);
    const BOOL command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTBUTTON,
                                          cursor.x, cursor.y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command != 0) OnMenuCommand(static_cast<UINT>(command));
}

void MainWindow::OnMenuCommand(UINT commandId) {
    // The fixed commands must be matched first: their ids are numerically
    // larger than kDeviceCommandBase, so a range test first would swallow them.
    switch (commandId) {
        case kCommandRefresh:
            RefreshDevices();
            return;
        case kCommandSettings:
            OpenSettings();
            return;
        case kCommandAbout:
            OpenAbout();
            return;
        case kCommandExit:
            DestroyWindow(hwnd_);
            return;
        default:
            break;
    }

    if (commandId >= kDeviceCommandBase) {
        const auto it = menuCommands_.find(commandId);
        if (it == menuCommands_.end()) return;
        // A copy: the menu can stay open while a rescan replaces devices_.
        const std::optional<Device> device = devices_.FindByInstanceId(it->second);
        if (!device) {
            RefreshDevices();  // the device disappeared between menu and click
            return;
        }
        EjectDevice(*device);
    }
}

void MainWindow::EjectDevice(const Device& device) {
    const auto& strings = i18n::I18n::Instance();
    const AppSettings& settings = Settings::Instance().Data();

    // The modal confirmation pumps messages; the device can disappear while
    // it is open. Resolve the copied instance ID again immediately before the
    // eject request instead of relying on the cached device-node handle.
    const std::wstring instanceId = device.instanceId;
    const std::wstring displayName = device.displayName;

    if (settings.confirmBeforeEject) {
        const std::wstring text = strings.Format(i18n::StrId::ConfirmEjectText, displayName);
        if (MessageBoxW(nullptr, text.c_str(), strings.Get(i18n::StrId::ConfirmEjectTitle),
                        MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) != IDYES) {
            return;
        }
    }

    QE_LOG_INFO(L"ejecting %ls", displayName.c_str());
    const EjectResult result = Ejector::Eject(instanceId);
    if (result.Succeeded()) {
        QE_LOG_INFO(L"eject succeeded for %ls", displayName.c_str());
        if (settings.showNotifications) {
            tray_.ShowBalloon(strings.Get(i18n::StrId::EjectOkTitle),
                              strings.Format(i18n::StrId::EjectOkText, displayName), NIIF_INFO);
        }
        RefreshDevices();
        return;
    }

    const std::wstring reason = Ejector::Describe(result);
    QE_LOG_WARN(L"eject failed for %ls: %ls", displayName.c_str(), reason.c_str());
    MessageBoxW(nullptr,
                strings.Format(i18n::StrId::EjectFailText, displayName, reason).c_str(),
                strings.Get(i18n::StrId::EjectFailTitle), MB_OK | MB_ICONWARNING | MB_TOPMOST);
}

void MainWindow::OpenSettings() {
    if (Dialogs::ShowSettings(instance_, hwnd_) != IDOK) return;
    if (!ApplyRunAtStartup(Settings::Instance().Data().runAtStartup)) {
        const auto& strings = i18n::I18n::Instance();
        MessageBoxW(hwnd_, strings.Get(i18n::StrId::SettingsStartupFailed),
                    strings.Get(i18n::StrId::SettingsTitle), MB_OK | MB_ICONWARNING);
    }
    RebuildTrayTip();
}

void MainWindow::OpenAbout() { Dialogs::ShowAbout(instance_, hwnd_); }

bool MainWindow::ApplyRunAtStartup(bool enabled) {
    const std::wstring command = AutoStart::CommandLine();
    if (command.empty()) {
        QE_LOG_ERROR(L"cannot determine the executable path; run-at-startup not changed");
        return false;
    }
    const bool ok = enabled ? AutoStart::SetEnabled(QUICKEJECT_APP_NAME_W, command)
                            : AutoStart::Clear(QUICKEJECT_APP_NAME_W);
    // %ls, not %s: in the wprintf family %s already means a wide string, so
    // passing these narrow literals would be undefined behaviour.
    QE_LOG_INFO(L"run-at-startup %ls: %ls", enabled ? L"enabled" : L"disabled",
                ok ? L"ok" : L"failed");
    return ok;
}

void MainWindow::HandleSecondInstance() {
    tray_.ShowBalloon(QUICKEJECT_APP_NAME_W,
                      i18n::I18n::Instance().Get(i18n::StrId::TrayHintRunning), NIIF_INFO);
}

}  // namespace qe
