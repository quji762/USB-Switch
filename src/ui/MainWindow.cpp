#include "ui/MainWindow.h"
#include "ui/DialogHeader.h"


#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <algorithm>
#include <unordered_map>

#include "app/AppInfo.h"
#include "app/Logger.h"
#include "app/Settings.h"
#include "app/SingleInstance.h"
#include "core/Ejector.h"
#include "core/MediaEject.h"
#include "core/OpenFiles.h"
#include "core/UsbPort.h"
#include "core/RestartProcess.h"
#include "core/SmartInfo.h"
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

// Device commands index the snapshot belonging to this popup.
constexpr UINT kDeviceCommandBase = 0x3000;
constexpr UINT kCommandRefresh = 0x2001;
constexpr UINT kCommandReconnect = 0x2006;
constexpr UINT kCommandSettings = 0x2002;
constexpr UINT kCommandDevices = 0x2007;
constexpr UINT kAliasBase = 0x100000;
constexpr UINT kMediaBase = 0x200000;
constexpr UINT kHideBase = 0x300000;
constexpr UINT kRestoreBase = 0x400000;

std::wstring MenuLabel(const std::wstring& name) {
    std::wstring label;
    for (auto c : name) {
        if (c == L'&') label += c;
        label += c;
    }
    return label;
}
constexpr int kMenuHotkey = 1;
constexpr UINT kCommandLogs = 0x2005;
constexpr UINT kCommandAbout = 0x2003;
constexpr UINT kCommandExit = 0x2004;

// One physical insertion produces several WM_DEVICECHANGE messages (disk
// interface, volume, device tree); a short timer collapses them into one scan.
constexpr UINT_PTR kRescanTimerId = 1;
constexpr UINT_PTR kTrayRetryTimerId = 2;
constexpr UINT_PTR kReconnectTimer = 3;

constexpr UINT kRescanDelayMs = 250;

struct FilesDialogData { std::wstring text; bool retry; };
INT_PTR CALLBACK OpenFilesProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (auto result = ThemeDialog(hwnd,msg,wp,lp)) return result;
    if (PaintDialogHeader(msg,lp)) return TRUE;
    if (msg == WM_INITDIALOG) {
        const auto& data = *reinterpret_cast<const FilesDialogData*>(lp);
        const auto& strings = i18n::I18n::Instance();
        SetWindowTextW(hwnd,strings.Get(i18n::StrId::InspectFiles));
        SetDlgItemTextW(hwnd,IDC_DIALOG_HEADER,strings.Get(i18n::StrId::InspectFiles));
        SetDlgItemTextW(hwnd,IDC_FILES_TEXT,data.text.c_str());
        SetDlgItemTextW(hwnd,IDRETRY,strings.Get(i18n::StrId::FilesRetry));
        SetDlgItemTextW(hwnd,IDCANCEL,strings.Get(i18n::StrId::FilesClose));
        if (!data.retry) ShowWindow(GetDlgItem(hwnd,IDRETRY),SW_HIDE);
        SetFocus(GetDlgItem(hwnd,IDCANCEL));
        return FALSE;
    }
    if (msg == WM_COMMAND && (LOWORD(wp)==IDCANCEL || LOWORD(wp)==IDRETRY)) {
        EndDialog(hwnd,LOWORD(wp)); return TRUE;
    }
    return FALSE;
}

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
        case WM_HOTKEY:
            if (wParam == kMenuHotkey && IsWindowEnabled(hwnd) && !menuOpen_) ShowTrayMenu();
            return 0;
        case WM_TIMER:
            if (wParam == kReconnectTimer) {
                if (!reconnectDeadline_) return 0;
                RefreshDevices();
                if (!reconnectDeadline_) return 0;
                if (restartProcess_) {
                    auto code = FinishedRestart(restartProcess_.Get());
                    if (!code) return 0; // Still running: keep the menu action disabled.
                    restartProcess_.Reset();
                    if (*code != ERROR_SUCCESS) {
                        KillTimer(hwnd_, kReconnectTimer);
                        reconnectDeadline_ = 0;
                        const auto& strings = i18n::I18n::Instance();
                        QE_LOG_WARN(L"Restart helper exited with %lu", *code);
                        const auto reason = str::Win32ErrorMessage(*code) + L" (" + std::to_wstring(*code) + L")";
                        MessageBoxW(hwnd_, strings.Format(i18n::StrId::ReconnectFailed, reason).c_str(),
                                    QUICKEJECT_APP_NAME_W, MB_ICONWARNING);
                        return 0;
                    }
                    reconnectDeadline_ = GetTickCount64() + 10000;
                }
                if (reconnectDeadline_ && GetTickCount64() >= reconnectDeadline_) {
                    restartProcess_.Reset();
                    KillTimer(hwnd_, kReconnectTimer);
                    reconnectDeadline_ = 0;
                    MessageBoxW(hwnd_, i18n::I18n::Instance().Get(i18n::StrId::ReconnectTimeout),
                                QUICKEJECT_APP_NAME_W, MB_ICONWARNING);
                }
                return 0;
            }
            if (wParam == kRescanTimerId) OnRescanTimer();
            else if (wParam == kTrayRetryTimerId) RestoreTrayIcon();
            return 0;
        case WM_DPICHANGED: {
            // The manifest declares PerMonitorV2, so the icon is reloaded at the
            // new DPI instead of staying at the size captured at startup.
            const UINT dpi = LOWORD(wParam);
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
            OnTrayNotify(wParam, lParam);
            return 0;
        case kWakeMessage:
            if (wParam == 1) {
                if (IsWindowEnabled(hwnd)) OpenSettings();
                else SetForegroundWindow(GetLastActivePopup(hwnd));
            } else HandleSecondInstance();
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
        RestoreTrayIcon();
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
    ApplyMenuHotkey();
    notifier_.Register(hwnd_);
    RefreshDevices();
    return true;
}

void MainWindow::OnDestroy() {
    connectionToast_.Clear();
    restartProcess_.Reset();
    UnregisterHotKey(hwnd_, kMenuHotkey);
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

void MainWindow::OnTrayNotify(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(lParam)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT: {
            // Version 4 packs the anchor in wParam and the event in lParam.
            POINT anchor{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
            ShowTrayMenu(&anchor);
            break;
        }
        case WM_RBUTTONUP:
        case WM_LBUTTONUP:
            // Legacy mouse callbacks supply no reliable anchor.
            ShowTrayMenu();
            break;
        default:
            break;
    }
}

void MainWindow::OnDeviceChange(UINT event, LPARAM data) {
    if (!DeviceNotifier::IsRelevantChange(event, data)) return;
    QE_LOG_DEBUG(L"device change: %ls", DeviceNotifier::DescribeChange(event, data).c_str());
    // While the failure backoff schedules the next scan, do not reset its
    // timer: a device-event storm would otherwise pin the retries at the
    // shortest interval. The pending backoff timer already covers this event.
    if (!scanRetryMs_) SetTimer(hwnd_, kRescanTimerId, kRescanDelayMs, nullptr);
}

void MainWindow::OnRescanTimer() {
    KillTimer(hwnd_, kRescanTimerId);
    RefreshDevices();
}

void MainWindow::RefreshDevices() {
    const auto& strings = i18n::I18n::Instance();
    const bool notify = Settings::Instance().Data().showNotifications;

    if (!devices_.Refresh()) {
        if (!scanRetryMs_ && notify)
            tray_.ShowBalloon(QUICKEJECT_APP_NAME_W, strings.Get(i18n::StrId::MenuScanUnavailable), NIIF_WARNING);
        scanRetryMs_ = scanRetryMs_ ? (std::min)(scanRetryMs_ * 2, 30000u) : 1000u;
        SetTimer(hwnd_, kRescanTimerId, scanRetryMs_, nullptr);
        RebuildTrayTip();
        return;
    }

    scanRetryMs_ = 0;
    KillTimer(hwnd_, kRescanTimerId);
    const auto snapshot = devices_.Devices();
    if (!stoppedDisk_.empty()) {
        if (devices_.FindByInstanceId(stoppedDisk_)) {
            // Right after the eject request the disk can still be enumerated
            // for a moment; clearing the reconnect record then would remove
            // the menu entry before it is ever shown. Only a scan that first
            // saw the disk gone confirms it is really detached - unless a
            // reconnect is in flight, where reappearance means success.
            if (stoppedSeenAbsent_ || reconnectDeadline_) {
                restartProcess_.Reset();
                stoppedDisk_.clear();
                stoppedTarget_.clear();
                reconnectDeadline_ = 0;
                stoppedSeenAbsent_ = false;
                KillTimer(hwnd_, kReconnectTimer);
            }
        } else {
            stoppedSeenAbsent_ = true;
        }
    }
    if (!firstScan_) {
        for (const auto& device : snapshot) {
            if (previousNames_.find(device.instanceId) == previousNames_.end()) {
                QE_LOG_INFO(L"device connected: %ls", device.displayName.c_str());
                if (notify) {
                    ShowConnectionNotice(true, device.displayName);
                }
            }
        }
        for (const auto& entry : previousNames_) {
            if (!devices_.FindByInstanceId(entry.first)) {
                QE_LOG_INFO(L"device disconnected: %ls", entry.second.c_str());
                if (notify) {
                    ShowConnectionNotice(false, entry.second);
                }
            }
        }
    }
    firstScan_ = false;

    previousNames_.clear();
    for (const auto& device : snapshot) {
        previousNames_[device.instanceId] = device.displayName;
    }
    RebuildTrayTip();
}

void MainWindow::ShowConnectionNotice(bool inserted,const std::wstring& name) {
    const auto& strings=i18n::I18n::Instance();
    QUERY_USER_NOTIFICATION_STATE state{};
    const bool quiet=SUCCEEDED(SHQueryUserNotificationState(&state)) && state!=QUNS_ACCEPTS_NOTIFICATIONS;
    // Let the shell deliver or suppress notifications in quiet/fullscreen sessions.
    if (!quiet && !HighContrast()) {
        BOOL motion=FALSE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);
        const auto& mode=Settings::Instance().Data().appearance;
        bool dark=mode==L"dark" || (mode==L"auto" && SystemDark());
        if (connectionToast_.Show(hwnd_,inserted,dark,motion!=FALSE,
            strings.Get(inserted?i18n::StrId::ToastInserted:i18n::StrId::ToastRemoved),name)) return;
    }
    tray_.ShowBalloon(QUICKEJECT_APP_NAME_W,strings.Format(
        inserted?i18n::StrId::DeviceInserted:i18n::StrId::DeviceRemoved,name));
}

void MainWindow::RestoreTrayIcon() {
    if (tray_.Recreate(icon_, i18n::I18n::Instance().Get(i18n::StrId::AppTooltip))) {
        KillTimer(hwnd_, kTrayRetryTimerId);
        RebuildTrayTip();
    } else {
        // Explorer may broadcast TaskbarCreated before it accepts icon requests.
        SetTimer(hwnd_, kTrayRetryTimerId, 5000, nullptr);
    }
}

void MainWindow::RebuildTrayTip() {
    const auto& strings = i18n::I18n::Instance();
    std::wstring tooltip = strings.Get(i18n::StrId::AppTooltip);
    if (scanRetryMs_) {
        tooltip += L" — ";
        tooltip += strings.Get(i18n::StrId::MenuScanUnavailable);
    } else if (!previousNames_.empty()) {
        tooltip += str::Format(L" (%zu)", previousNames_.size());
    }
    tray_.UpdateTooltip(tooltip);
}

void MainWindow::ShowTrayMenu(const POINT* position) {
    if (menuOpen_) return;
    const auto& strings = i18n::I18n::Instance();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        QE_LOG_ERROR(L"CreatePopupMenu failed: %lu", GetLastError());
        return;
    }

    HMENU aliases = nullptr;
    HMENU media = nullptr;
    HMENU hide = nullptr;
    const auto& preferences = Settings::Instance().Data();
    const auto hidden = preferences.hiddenDevices;
    std::vector<std::wstring> restoreIds;
    const auto devices = scanRetryMs_ ? DeviceList{} : devices_.Devices();
    if (scanRetryMs_) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, strings.Get(i18n::StrId::MenuScanUnavailable));
    } else if (devices.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, strings.Get(i18n::StrId::MenuNoDevices));
    } else {
        std::unordered_map<std::wstring, size_t> readerCount;
        for (const auto& device : devices) {
            if (device.readerId.empty() || hidden.count(str::Lower(device.instanceId))) continue;
            ++readerCount[device.readerId];
        }
        std::unordered_map<std::wstring, bool> emitted;
        auto appendDevice = [&](HMENU target, const Device& device, size_t index) {
            if (hidden.count(str::Lower(device.instanceId))) return;
            const auto menuName = MenuLabel(preferences.DeviceName(device.instanceId, device.displayName));
            auto title = device.mediaState == MediaState::Empty ? i18n::StrId::EmptySlot :
                         device.mediaState == MediaState::NotReady ? i18n::StrId::SlotNotReady : i18n::StrId::MenuEjectItem;
            AppendMenuW(target, MF_STRING | (device.MediaUnavailable() ? MF_GRAYED : 0),
                        kDeviceCommandBase + static_cast<UINT>(index),
                        strings.Format(title, menuName).c_str());
            if (!aliases) aliases = CreatePopupMenu();
            if (aliases) AppendMenuW(aliases, MF_STRING, kAliasBase + index, menuName.c_str());
            if (!device.instanceId.empty()) {
                if (!hide) hide = CreatePopupMenu();
                if (hide) AppendMenuW(hide, MF_STRING, kHideBase + index, menuName.c_str());
            }
            if (!device.MediaUnavailable() && device.driverMediaRemovable == true && !device.volumeInterfaces.empty()) {
                if (!media) media = CreatePopupMenu();
                if (media) AppendMenuW(media, MF_STRING, kMediaBase + index, menuName.c_str());
            }
        };
        for (size_t index = 0; index < devices.size(); ++index) {
            const auto& device = devices[index];
            if (hidden.count(str::Lower(device.instanceId))) continue;
            const bool reader = !device.readerId.empty() && readerCount[device.readerId] >= 2;
            if (!reader) {
                appendDevice(menu, device, index);
                continue;
            }
            if (emitted[device.readerId]) continue;
            emitted[device.readerId] = true;
            HMENU slots = CreatePopupMenu();
            if (!slots) {
                appendDevice(menu, device, index);
                continue;
            }
            for (size_t slot = index; slot < devices.size(); ++slot) {
                if (devices[slot].readerId == device.readerId)
                    appendDevice(slots, devices[slot], slot);
            }
            const std::wstring title = device.readerName.empty()
                                           ? std::wstring(strings.Get(i18n::StrId::CardReader))
                                           : device.readerName;
            if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(slots), MenuLabel(title).c_str()))
                DestroyMenu(slots);
        }
        if (!GetMenuItemCount(menu))
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, strings.Get(i18n::StrId::AllHidden));
    }

    if (media && !AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(media), strings.Get(i18n::StrId::MenuMedia)))
        DestroyMenu(media);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU manage = (aliases || hide || !hidden.empty()) ? CreatePopupMenu() : nullptr;
    // If the submenu cannot be created, the original actions remain available.
    HMENU parent = manage ? manage : menu;
    if (aliases && !AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(aliases), strings.Get(i18n::StrId::MenuAliases)))
        DestroyMenu(aliases);
    if (hide && !AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(hide), strings.Get(i18n::StrId::HideDevice)))
        DestroyMenu(hide);
    if (!hidden.empty()) {
        HMENU restore = CreatePopupMenu();
        if (restore) {
            for (const auto& item : hidden) {
                auto current = std::find_if(devices.begin(), devices.end(), [&](const Device& d) {
                    return str::Lower(d.instanceId) == item.first;
                });
                auto name = current == devices.end() ? strings.Format(i18n::StrId::LastSeenDevice,item.second) :
                    preferences.DeviceName(current->instanceId,current->displayName);
                AppendMenuW(restore, MF_STRING, kRestoreBase + restoreIds.size(), MenuLabel(name).c_str());
                restoreIds.push_back(item.first);
            }
            if (!AppendMenuW(parent, MF_POPUP,reinterpret_cast<UINT_PTR>(restore),strings.Get(i18n::StrId::RestoreDevice)))
                DestroyMenu(restore);
        }
    }
    if (manage && !AppendMenuW(menu, MF_POPUP,reinterpret_cast<UINT_PTR>(manage),strings.Get(i18n::StrId::ManageDevices)))
        DestroyMenu(manage);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandDevices, strings.Get(i18n::StrId::MenuConnected));
    const auto reconnectTarget = stoppedTarget_;
    const auto reconnectDisk = stoppedDisk_;
    if (!reconnectTarget.empty() && !hidden.count(str::Lower(stoppedDisk_))) {
        const std::wstring caption = MenuLabel(strings.Format(i18n::StrId::ReconnectDevice, stoppedName_));
        AppendMenuW(menu, MF_STRING | (reconnectDeadline_ ? MF_GRAYED : 0), kCommandReconnect, caption.c_str());
    }
    AppendMenuW(menu, MF_STRING, kCommandRefresh, strings.Get(i18n::StrId::MenuRefresh));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandSettings, strings.Get(i18n::StrId::MenuSettings));
    AppendMenuW(menu, MF_STRING, kCommandLogs, strings.Get(i18n::StrId::MenuLogs));
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
    menuOpen_ = true;
    const BOOL command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTBUTTON,
                                          cursor.x, cursor.y, hwnd_, nullptr);
    menuOpen_ = false;
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (!hwnd_ || command == 0) return;
    const auto id = static_cast<UINT>(command);
    if (id == kCommandReconnect && !reconnectTarget.empty() && reconnectTarget == stoppedTarget_ && reconnectDisk == stoppedDisk_) {
        const bool usePort = Ejector::HeldForEject(reconnectTarget) &&
                             ReconnectUsbPort(reconnectTarget, true) == ERROR_SUCCESS;
        auto error = usePort ? ReconnectUsbPort(reconnectTarget, false) : Ejector::RestartStopped(reconnectDisk, reconnectTarget);
        if (error == ERROR_ACCESS_DENIED && Ejector::HeldForEject(reconnectTarget)) {
            if (MessageBoxW(hwnd_, strings.Format(i18n::StrId::ReconnectAdmin, stoppedName_).c_str(),
                            QUICKEJECT_APP_NAME_W, MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return;
            if (stoppedTarget_ != reconnectTarget || stoppedDisk_ != reconnectDisk || !Ejector::HeldForEject(reconnectTarget)) {
                RefreshDevices();
                return;
            }
            const auto safeId = [](const std::wstring& id) {
                return !id.empty() && id.size() < MAX_DEVICE_ID_LEN &&
                       id.find_first_of(L"\"*?") == id.npos && id.back() != L'\\';
            };
            std::wstring exe(32768, L'\0');
            const DWORD copied = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
            if (!safeId(reconnectTarget) || !safeId(reconnectDisk)) error = ERROR_INVALID_DATA;
            else if (!copied || copied >= exe.size()) error = copied ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
            else {
                exe.resize(copied);
                const std::wstring args = usePort ? L"--restart-held-usb \"" + reconnectTarget + L"\""
                    : L"--restart-held-node \"" + reconnectDisk + L"\" \"" + reconnectTarget + L"\"";
                SHELLEXECUTEINFOW run{};
                run.cbSize = sizeof(run);
                run.fMask = SEE_MASK_NOCLOSEPROCESS;
                run.hwnd = hwnd_;
                run.lpVerb = L"runas";
                run.lpFile = exe.c_str();
                run.lpParameters = args.c_str();
                run.nShow = SW_HIDE;
                error = ShellExecuteExW(&run) ? ERROR_SUCCESS : GetLastError();
                if (error == ERROR_SUCCESS) restartProcess_.Reset(run.hProcess);
                if (error == ERROR_CANCELLED) return;
            }
        }
        if (error == ERROR_SUCCESS) {
            reconnectDeadline_ = GetTickCount64() + 10000;
            if (!SetTimer(hwnd_, kReconnectTimer, 500, nullptr)) {
                restartProcess_.Reset();
                reconnectDeadline_ = 0;
                MessageBoxW(hwnd_, strings.Get(i18n::StrId::ReconnectTimeout), QUICKEJECT_APP_NAME_W, MB_ICONWARNING);
            }
        } else {
            MessageBoxW(hwnd_, strings.Format(i18n::StrId::ReconnectFailed, str::Win32ErrorMessage(error)).c_str(),
                        QUICKEJECT_APP_NAME_W, MB_ICONWARNING);
        }
        RefreshDevices();
    } else if (id >= kHideBase) {
        auto& settings = Settings::Instance();
        auto& entries = settings.Data().hiddenDevices;
        const auto previous = entries;
        if (id >= kRestoreBase && id-kRestoreBase < restoreIds.size()) entries.erase(restoreIds[id-kRestoreBase]);
        else if (id < kRestoreBase && id-kHideBase < devices.size()) {
            const auto& d = devices[id-kHideBase];
            entries[str::Lower(d.instanceId)] = preferences.DeviceName(d.instanceId,d.displayName);
        } else return;
        if (!settings.Save()) {
            entries = previous;
            MessageBoxW(hwnd_,strings.Get(i18n::StrId::SettingsSaveFailed),QUICKEJECT_APP_NAME_W,MB_ICONWARNING);
        }
    } else if (id >= kMediaBase) {
        const auto index = id - kMediaBase;
        if (!scanRetryMs_ && index < devices.size()) EjectMedium(devices[index]);
    } else if (id >= kAliasBase) {
        const auto index = id - kAliasBase;
        if (!scanRetryMs_ && index < devices.size())
            Dialogs::EditAlias(instance_, hwnd_, devices[index].instanceId, devices[index].displayName);
    } else if (id >= kDeviceCommandBase) {
        const auto index = id - kDeviceCommandBase;
        if (!scanRetryMs_ && index < devices.size()) EjectDevice(devices[index]);
    } else {
        OnMenuCommand(id);
    }
}

void MainWindow::OnMenuCommand(UINT commandId) {
    switch (commandId) {
        case kCommandRefresh:
            RefreshDevices();
            return;
        case kCommandDevices:
            OpenDevices();
            return;
        case kCommandSettings:
            OpenSettings();
            return;
        case kCommandLogs: {
            const auto path = Logger::Instance().Path();
            const auto sep = path.find_last_of(L"\\/");
            if (sep == path.npos || reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open",
                    path.substr(0, sep).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
                MessageBoxW(hwnd_, i18n::I18n::Instance().Get(i18n::StrId::LogOpenFailed),
                            QUICKEJECT_APP_NAME_W, MB_OK | MB_ICONWARNING);
            }
            return;
        }
        case kCommandAbout:
            OpenAbout();
            return;
        case kCommandExit:
            DestroyWindow(hwnd_);
            return;
        default:
            break;
    }

}

void MainWindow::EjectMedium(const Device& device) {
    const auto& strings = i18n::I18n::Instance();
    using i18n::StrId;
    const auto name = Settings::Instance().Data().DeviceName(device.instanceId, device.displayName);
    const HWND owner = PromptOwner();
    if (MessageBoxW(owner, strings.Format(StrId::MediaConfirm, name).c_str(), strings.Get(StrId::MenuMedia),
                    MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES || !hwnd_) return;
    DeviceManager live;
    auto current = live.Refresh() ? live.FindByInstanceId(device.instanceId) : std::nullopt;
    if (!current || current->volumeInterfaces.size() != device.volumeInterfaces.size() ||
        !std::is_permutation(current->volumeInterfaces.begin(), current->volumeInterfaces.end(),
                             device.volumeInterfaces.begin())) {
        MessageBoxW(owner, strings.Get(StrId::MediaChanged), strings.Get(StrId::MenuMedia), MB_ICONWARNING);
    } else {
        const auto result = EjectMedia(*current);
        if (result.error) {
            QE_LOG_WARN(L"media eject failed at %ls (%lu)", result.stage, result.error);
            auto reason = str::Win32ErrorMessage(result.error);
            if (std::wstring_view(result.stage) == L"lock_volume")
                reason = std::wstring(strings.Get(StrId::MediaLockFailed)) + L"\n" + reason;
            MessageBoxW(owner, strings.Format(StrId::EjectFailText, name, reason).c_str(),
                        strings.Get(StrId::MenuMedia), MB_ICONWARNING);
        } else if (Settings::Instance().Data().showNotifications) {
            tray_.ShowBalloon(strings.Get(StrId::MenuMedia), strings.Format(StrId::MediaOk, name), NIIF_INFO);
        }
    }
    RefreshDevices();
}

void MainWindow::EjectDevice(const Device& device) {
    const auto& strings = i18n::I18n::Instance();
    const AppSettings& settings = Settings::Instance().Data();

    // The modal confirmation pumps messages; the device can disappear while
    // it is open. Resolve the copied instance ID again immediately before the
    // eject request instead of relying on the cached device-node handle.
    const std::wstring instanceId = device.instanceId;
    const std::wstring displayName = settings.DeviceName(instanceId, device.displayName);
    const HWND owner = PromptOwner();

    if (settings.confirmBeforeEject) {
        const std::wstring text = strings.Format(i18n::StrId::ConfirmEjectText, displayName);
        if (MessageBoxW(owner, text.c_str(), strings.Get(i18n::StrId::ConfirmEjectTitle),
                        MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION | MB_TOPMOST) != IDYES) {
            return;
        }
    }

    while (hwnd_ != nullptr) {
        QE_LOG_INFO(L"ejecting %ls", displayName.c_str());
        const EjectResult result = Ejector::Eject(instanceId);
        if (result.Succeeded()) {
            restartProcess_.Reset();
            KillTimer(hwnd_, kReconnectTimer);
            reconnectDeadline_ = 0;
            stoppedDisk_ = instanceId;
            stoppedTarget_ = result.stoppedInstanceId;
            stoppedName_ = displayName;
            // The flag describes the previous record's disk; this one has not
            // been observed gone yet.
            stoppedSeenAbsent_ = false;
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
        const bool offerRetry = result.CanRetry();
        if (result.status == EjectStatus::Vetoed && !result.reportedProcesses.empty()) {
            using i18n::StrId;
            const TASKDIALOG_BUTTON inspect{101, strings.Get(StrId::InspectFiles)};
            TASKDIALOGCONFIG dialog{};
            dialog.cbSize = sizeof(dialog);
            dialog.hwndParent = owner;
            dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
            dialog.pszMainIcon = TD_WARNING_ICON;
            dialog.pszWindowTitle = strings.Get(StrId::EjectFailTitle);
            auto message = strings.Format(StrId::EjectFailText, displayName, reason);
            dialog.pszContent = message.c_str();
            dialog.dwCommonButtons = offerRetry ? TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON : TDCBF_CLOSE_BUTTON;
            dialog.nDefaultButton = offerRetry ? IDCANCEL : IDCLOSE;
            dialog.cButtons = 1;
            dialog.pButtons = &inspect;
            int selected{};
            if (SUCCEEDED(TaskDialogIndirect(&dialog,&selected,nullptr,nullptr))) {
                if (selected == inspect.nButtonID) {
                    auto results = InspectOpenFiles(instanceId,result);
                    std::wstring detail = strings.Get(StrId::FilesIntro);
                    for (const auto& item : results.Items()) {
                        detail += str::Format(L"\n\n%ls (PID %lu)", item.GetString(L"name",L"").c_str(),
                                              static_cast<DWORD>(item.GetNumber(L"pid",0)));
                        const auto* paths = item.Find(L"paths");
                        if (paths) for (const auto& path : paths->Items()) detail += L"\n" + path.AsString();
                        const auto status = item.GetString(L"status",L"");
                        if (status == L"access_denied") detail += L"\n" + std::wstring(strings.Get(StrId::FilesDenied));
                        else if (status != L"inspected") detail += L"\n" + std::wstring(strings.Get(StrId::FilesPartial));
                        if ((!paths || !paths->Size()) && status == L"inspected")
                            detail += L"\n" + std::wstring(strings.Get(StrId::FilesEmpty));
                    }
                    FilesDialogData data{{},offerRetry};
                    for (auto ch : detail) {
                        if (ch == L'\n') data.text += L'\r';
                        data.text += ch;
                    }
                    selected = static_cast<int>(DialogBoxParamW(instance_,MAKEINTRESOURCEW(IDD_OPEN_FILES),hwnd_,
                                                                OpenFilesProc,reinterpret_cast<LPARAM>(&data)));
                }
                if (offerRetry && selected == IDRETRY) continue;
                return;
            }
        }
        const UINT buttons = offerRetry ? MB_RETRYCANCEL | MB_DEFBUTTON2 : MB_OK;
        const int choice = MessageBoxW(owner,
            strings.Format(i18n::StrId::EjectFailText, displayName, reason).c_str(),
            strings.Get(i18n::StrId::EjectFailTitle), buttons | MB_ICONWARNING);
        if (!offerRetry || choice != IDRETRY) return;
    }
}

void MainWindow::ApplyMenuHotkey() {
    const auto& key = Settings::Instance().Data().menuHotkey;
    if (registeredKey_ == key) return;
    UnregisterHotKey(hwnd_, kMenuHotkey);
    registeredKey_.clear();
    if (key.empty()) return;
    if (RegisterHotKey(hwnd_, kMenuHotkey, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, key.front()))
        registeredKey_ = key;
    else {
        QE_LOG_WARN(L"menu hotkey unavailable: %lu", GetLastError());
        tray_.ShowBalloon(QUICKEJECT_APP_NAME_W, i18n::I18n::Instance().Get(i18n::StrId::HotkeyConflict), NIIF_WARNING);
    }
}

void MainWindow::OpenSettings() {
    if (Dialogs::ShowSettings(instance_, hwnd_) != IDOK) return;
    ApplyMenuHotkey();
    RebuildTrayTip();
}

void MainWindow::OpenAbout() { Dialogs::ShowAbout(instance_, hwnd_); }

namespace {

std::wstring VisibleSmart(const Device& device) {
    const auto& show = Settings::Instance().Data();
    if (device.MediaUnavailable() || device.diskInterface.empty() ||
        !(show.showTemperature || show.showPowerOnHours || show.showPowerCycles || show.showReallocated ||
          show.showPendingSectors))
        return {};
    // SMART queries are synchronous IOCTLs that can take seconds on a slow or
    // dying drive, so every device window reload reuses fresh-enough readings
    // instead of re-asking each disk.
    struct CacheEntry {
        SmartSnapshot snapshot;
        unsigned long long tick = 0;
    };
    static std::unordered_map<std::wstring, CacheEntry> cache;
    constexpr unsigned long long kSmartCacheTtlMs = 30000;
    const unsigned long long now = GetTickCount64();
    SmartSnapshot snap;
    const auto found = cache.find(device.instanceId);
    if (found != cache.end() && now - found->second.tick < kSmartCacheTtlMs) {
        snap = found->second.snapshot;
    } else {
        snap = ReadSmart(device.diskInterface, device.mountPaths, device.deviceNumber);
        if (cache.size() > 128) cache.clear();
        cache[device.instanceId] = CacheEntry{snap, now};
    }
    const auto& strings = i18n::I18n::Instance();
    std::wstring text;
    auto append = [&](const std::wstring& part) {
        if (!text.empty()) text += L"  ";
        text += part;
    };
    if (show.showTemperature && snap.hasTemperature)
        append(strings.Format(i18n::StrId::SmartTempValue, std::to_wstring(snap.temperatureC)));
    if (show.showPowerOnHours && snap.hasPowerOnHours)
        append(strings.Format(i18n::StrId::SmartHoursValue, std::to_wstring(snap.powerOnHours)));
    if (show.showPowerCycles && snap.hasPowerCycles)
        append(strings.Format(i18n::StrId::SmartCyclesValue, std::to_wstring(snap.powerCycles)));
    if (show.showReallocated && snap.hasReallocated)
        append(strings.Format(i18n::StrId::SmartReallocatedValue, std::to_wstring(snap.reallocated)));
    if (show.showPendingSectors && snap.hasPending)
        append(strings.Format(i18n::StrId::SmartPendingValue, std::to_wstring(snap.pending)));
    return text;
}

INT_PTR CALLBACK SmartOptionsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (auto result = ThemeDialog(hwnd, message, wParam, lParam)) return result;
    if (PaintDialogHeader(message, lParam)) return TRUE;
    if (message == WM_INITDIALOG) {
        ApplyDialogTheme(hwnd, false);
        const auto& strings = i18n::I18n::Instance();
        const auto& show = Settings::Instance().Data();
        SetWindowTextW(hwnd, strings.Get(i18n::StrId::SmartTitle));
        SetDlgItemTextW(hwnd, IDC_DIALOG_HEADER, strings.Get(i18n::StrId::SmartTitle));
        SetDlgItemTextW(hwnd, IDC_SMART_TEMP, strings.Get(i18n::StrId::SmartTemperature));
        SetDlgItemTextW(hwnd, IDC_SMART_HOURS, strings.Get(i18n::StrId::SmartHours));
        SetDlgItemTextW(hwnd, IDC_SMART_CYCLES, strings.Get(i18n::StrId::SmartCycles));
        SetDlgItemTextW(hwnd, IDC_SMART_REALLOC, strings.Get(i18n::StrId::SmartReallocated));
        SetDlgItemTextW(hwnd, IDC_SMART_PENDING, strings.Get(i18n::StrId::SmartPending));
        SetDlgItemTextW(hwnd, IDC_SMART_NOTE, strings.Get(i18n::StrId::SmartNote));
        SetDlgItemTextW(hwnd, IDOK, strings.Get(i18n::StrId::DialogSave));
        SetDlgItemTextW(hwnd, IDCANCEL, strings.Get(i18n::StrId::DialogCancel));
        CheckDlgButton(hwnd, IDC_SMART_TEMP, show.showTemperature ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SMART_HOURS, show.showPowerOnHours ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SMART_CYCLES, show.showPowerCycles ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SMART_REALLOC, show.showReallocated ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SMART_PENDING, show.showPendingSectors ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        auto& show = Settings::Instance().Data();
        const auto previous = show;
        show.showTemperature = IsDlgButtonChecked(hwnd, IDC_SMART_TEMP) == BST_CHECKED;
        show.showPowerOnHours = IsDlgButtonChecked(hwnd, IDC_SMART_HOURS) == BST_CHECKED;
        show.showPowerCycles = IsDlgButtonChecked(hwnd, IDC_SMART_CYCLES) == BST_CHECKED;
        show.showReallocated = IsDlgButtonChecked(hwnd, IDC_SMART_REALLOC) == BST_CHECKED;
        show.showPendingSectors = IsDlgButtonChecked(hwnd, IDC_SMART_PENDING) == BST_CHECKED;
        if (!Settings::Instance().Save()) {
            show = previous;
            MessageBoxW(hwnd, i18n::I18n::Instance().Get(i18n::StrId::SettingsSaveFailed),
                        i18n::I18n::Instance().Get(i18n::StrId::SettingsTitle), MB_OK | MB_ICONERROR);
            return TRUE;
        }
        EndDialog(hwnd, IDOK);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) {
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

struct DeviceWindowState {
    MainWindow* owner = nullptr;
    DeviceList devices;
    HFONT bold = nullptr;
};

}  // namespace

void MainWindow::OpenDevices() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_DEVICES), hwnd_, DeviceWindowProc,
                    reinterpret_cast<LPARAM>(this));
    promptOwner_ = nullptr;
}

void MainWindow::UpdateDeviceButtons(HWND dialog) {
    const auto* state = reinterpret_cast<const DeviceWindowState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    HWND tree = GetDlgItem(dialog, IDC_DEVICE_LIST);
    const HTREEITEM selected = TreeView_GetSelection(tree);
    const Device* device = nullptr;
    if (state && selected) {
        TVITEMW item{};
        item.mask = TVIF_PARAM;
        item.hItem = selected;
        if (TreeView_GetItem(tree, &item) && item.lParam >= 0 &&
            static_cast<size_t>(item.lParam) < state->devices.size())
            device = &state->devices[static_cast<size_t>(item.lParam)];
    }
    const bool ejectable = device && device->CanEject() && !device->MediaUnavailable();
    const bool media = ejectable && device->driverMediaRemovable == true && !device->volumeInterfaces.empty();
    EnableWindow(GetDlgItem(dialog, IDC_DEVICE_EJECT), ejectable);
    EnableWindow(GetDlgItem(dialog, IDC_DEVICE_MEDIA), media);
}

void MainWindow::ReloadDeviceWindow(HWND dialog) {
    auto* state = reinterpret_cast<DeviceWindowState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (!state) return;
    state->devices = devices_.Devices();
    HWND tree = GetDlgItem(dialog, IDC_DEVICE_LIST);
    TreeView_DeleteAllItems(tree);

    const auto groups = GroupReaders(state->devices);

    const auto& strings = i18n::I18n::Instance();
    const auto& preferences = Settings::Instance().Data();
    auto detail = [&](const Device& device) {
        std::wstring line = preferences.DeviceName(device.instanceId, device.displayName);
        line += L"    ";
        line += device.driveLetters.empty() ? L"-" : device.driveLetters;
        if (device.sizeBytes) {
            line += L"    ";
            line += str::FileSizeString(device.sizeBytes);
        }
        line += L"    ";
        line += strings.Get(device.mediaState == MediaState::Present ? i18n::StrId::StatePresent :
                            device.mediaState == MediaState::Empty ? i18n::StrId::StateEmpty :
                            device.mediaState == MediaState::NotReady ? i18n::StrId::StateNotReady :
                            (!device.driveLetters.empty() || device.sizeBytes) ? i18n::StrId::StatePresent :
                            i18n::StrId::StateUnknown);
        const auto smart = VisibleSmart(device);
        if (!smart.empty()) {
            line += L"    ";
            line += smart;
        }
        return line;
    };
    for (const auto& group : groups) {
        const bool reader = !group.id.empty();
        HTREEITEM parent = TVI_ROOT;
        if (reader) {
            std::wstring title = group.name.empty() ? std::wstring(strings.Get(i18n::StrId::CardReader)) : group.name;
            TVINSERTSTRUCTW insert{};
            insert.hParent = TVI_ROOT;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_CHILDREN;
            insert.item.state = TVIS_BOLD | TVIS_EXPANDED;
            insert.item.stateMask = TVIS_BOLD | TVIS_EXPANDED;
            insert.item.cChildren = 1;
            insert.item.lParam = -1;
            insert.item.pszText = title.data();
            parent = TreeView_InsertItem(tree, &insert);
        }
        for (const auto index : group.indexes) {
            std::wstring line = detail(state->devices[index]);
            TVINSERTSTRUCTW insert{};
            insert.hParent = parent;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
            insert.item.state = reader ? 0 : TVIS_BOLD;
            insert.item.stateMask = TVIS_BOLD;
            insert.item.lParam = static_cast<LPARAM>(index);
            insert.item.pszText = line.data();
            TreeView_InsertItem(tree, &insert);
        }
    }
    const bool empty = state->devices.empty();
    ShowWindow(tree, empty ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(dialog, IDC_DEVICE_EMPTY), empty ? SW_SHOW : SW_HIDE);
    UpdateDeviceButtons(dialog);
}

INT_PTR CALLBACK MainWindow::DeviceWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (auto result = ThemeDialog(hwnd, message, wParam, lParam)) return result;
    if (PaintDialogHeader(message, lParam)) return TRUE;
    if (message == WM_INITDIALOG || message == WM_SETTINGCHANGE || message == WM_SYSCOLORCHANGE) {
        // The tree control draws its own background: without an explicit color
        // it stays white in the dark theme. Runs on theme switches too.
        HWND tree = GetDlgItem(hwnd, IDC_DEVICE_LIST);
        const bool dark = DialogDark(hwnd);
        SetWindowTheme(tree, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        SendMessageW(tree, TVM_SETBKCOLOR, 0, dark ? darkPaper : CLR_NONE);
        SendMessageW(tree, TVM_SETTEXTCOLOR, 0, dark ? darkInk : GetSysColor(COLOR_WINDOWTEXT));
        InvalidateRect(tree, nullptr, FALSE);
    }
    auto* state = reinterpret_cast<DeviceWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        state = new DeviceWindowState{reinterpret_cast<MainWindow*>(lParam), {}};
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->owner->promptOwner_ = hwnd;
        ApplyDialogTheme(hwnd, false);
        state->bold = nullptr;
        if (HFONT base = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0))) {
            LOGFONTW font{};
            if (GetObjectW(base, sizeof(font), &font)) {
                font.lfWeight = FW_SEMIBOLD;
                state->bold = CreateFontIndirectW(&font);
            }
        }
        const auto& strings = i18n::I18n::Instance();
        SetWindowTextW(hwnd, strings.Get(i18n::StrId::DevicesTitle));
        SetDlgItemTextW(hwnd, IDC_DIALOG_HEADER, strings.Get(i18n::StrId::DevicesTitle));
        SetDlgItemTextW(hwnd, IDC_DEVICE_EMPTY, strings.Get(i18n::StrId::DevicesEmpty));
        SetDlgItemTextW(hwnd, IDC_DEVICE_EJECT, strings.Get(i18n::StrId::DevicesEject));
        SetDlgItemTextW(hwnd, IDC_DEVICE_MEDIA, strings.Get(i18n::StrId::DevicesMedia));
        SetDlgItemTextW(hwnd, IDC_DEVICE_SMART, strings.Get(i18n::StrId::SmartChoose));
        SetDlgItemTextW(hwnd, IDC_DEVICE_REFRESH, strings.Get(i18n::StrId::DevicesRefresh));
        SetDlgItemTextW(hwnd, IDCANCEL, strings.Get(i18n::StrId::DevicesClose));
        HWND tree = GetDlgItem(hwnd, IDC_DEVICE_LIST);
        TreeView_SetItemHeight(tree, 24);
        TreeView_SetIndent(tree, 28);
        state->owner->ReloadDeviceWindow(hwnd);
        return TRUE;
    }
    if (!state || !state->owner) return FALSE;
    if (message == WM_COMMAND) {
        const UINT command = LOWORD(wParam);
        if (command == IDCANCEL) {
            EndDialog(hwnd, 0);
            return TRUE;
        }
        HWND list = GetDlgItem(hwnd, IDC_DEVICE_LIST);
        const HTREEITEM selected = TreeView_GetSelection(list);
        size_t index = state->devices.size();
        if (selected) {
            TVITEMW item{};
            item.mask = TVIF_PARAM;
            item.hItem = selected;
            if (TreeView_GetItem(list, &item) && item.lParam >= 0) index = static_cast<size_t>(item.lParam);
        }
        if (command == IDC_DEVICE_REFRESH) {
            state->owner->RefreshDevices();
            state->owner->ReloadDeviceWindow(hwnd);
            return TRUE;
        }
        if (command == IDC_DEVICE_SMART) {
            if (DialogBoxParamW(state->owner->instance_, MAKEINTRESOURCEW(IDD_SMART), hwnd, SmartOptionsProc, 0) == IDOK)
                state->owner->ReloadDeviceWindow(hwnd);
            return TRUE;
        }
        if (index < state->devices.size() && (command == IDC_DEVICE_EJECT || command == IDC_DEVICE_MEDIA)) {
            const Device device = state->devices[index];
            // The double-click notification sends the eject command directly,
            // so the same eligibility rule as UpdateDeviceButtons applies here.
            if (device.CanEject() && !device.MediaUnavailable()) {
                if (command == IDC_DEVICE_MEDIA) state->owner->EjectMedium(device);
                else state->owner->EjectDevice(device);
            }
            state->owner->ReloadDeviceWindow(hwnd);
            return TRUE;
        }
    }
    if (message == WM_NOTIFY) {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->idFrom == IDC_DEVICE_LIST && header->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                return TRUE;
            }
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                const bool nested = TreeView_GetParent(header->hwndFrom,
                    reinterpret_cast<HTREEITEM>(draw->nmcd.dwItemSpec)) != nullptr;
                const bool dark = DialogDark(hwnd);
                if (!nested) {
                    draw->clrText = dark ? RGB(236, 244, 242) : RGB(16, 40, 50);
                    if (state->bold) SelectObject(draw->nmcd.hdc, state->bold);
                    SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, CDRF_NEWFONT);
                } else {
                    draw->clrText = dark ? RGB(168, 190, 194) : RGB(84, 104, 114);
                    SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, CDRF_DODEFAULT);
                }
                return TRUE;
            }
        }
        if (header->idFrom == IDC_DEVICE_LIST && header->code == TVN_SELCHANGED)
            state->owner->UpdateDeviceButtons(hwnd);
        if (header->idFrom == IDC_DEVICE_LIST && header->code == NM_DBLCLK)
            SendMessageW(hwnd, WM_COMMAND, IDC_DEVICE_EJECT, 0);
        return FALSE;  // unhandled notifications take the default path
    }
    if (message == WM_DESTROY) {
        state->owner->promptOwner_ = nullptr;
        if (state->bold) DeleteObject(state->bold);
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return TRUE;
    }
    return FALSE;
}

void MainWindow::HandleSecondInstance() {
    tray_.ShowBalloon(QUICKEJECT_APP_NAME_W,
                      i18n::I18n::Instance().Get(i18n::StrId::TrayHintRunning), NIIF_INFO);
}

}  // namespace qe
