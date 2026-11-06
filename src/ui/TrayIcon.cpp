#include "ui/TrayIcon.h"

#include <cwchar>
#include <iterator>

#include "app/Logger.h"
#include "util/Str.h"

namespace qe {
namespace {

void CopyFixed(wchar_t* destination, size_t capacity, std::wstring_view source) {
    const size_t count = source.size() < capacity - 1 ? source.size() : capacity - 1;
    if (count > 0) std::wmemcpy(destination, source.data(), count);
    destination[count] = L'\0';
}

}  // namespace

void TrayIcon::CopyToTip(std::wstring_view tooltip) {
    CopyFixed(data_.szTip, std::size(data_.szTip), tooltip);
}

void TrayIcon::CopyToBalloonTitle(std::wstring_view title) {
    CopyFixed(data_.szInfoTitle, std::size(data_.szInfoTitle), title);
}

void TrayIcon::CopyToBalloonText(std::wstring_view text) {
    CopyFixed(data_.szInfo, std::size(data_.szInfo), text);
}

bool TrayIcon::Create(HWND hwnd, UINT callbackMessage, HICON icon, std::wstring_view tooltip) {
    Remove();
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = hwnd;
    data_.uID = kIconId;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = callbackMessage;
    data_.hIcon = icon;
    data_.uVersion = NOTIFYICON_VERSION_4;
    CopyToTip(tooltip);

    if (notify_(NIM_ADD, &data_) == FALSE) {
        QE_LOG_ERROR(L"Shell_NotifyIcon(NIM_ADD) failed: %lu", GetLastError());
        return false;
    }
    // NOTIFYICON_VERSION_4 must be applied with a second call.
    if (notify_(NIM_SETVERSION, &data_) == FALSE) {
        QE_LOG_WARN(L"Shell_NotifyIcon(NIM_SETVERSION) failed: %lu", GetLastError());
    }
    created_ = true;
    QE_LOG_INFO(L"tray icon created");
    return true;
}

void TrayIcon::UpdateIcon(HICON icon) {
    if (!created_) return;
    data_.uFlags = NIF_ICON;
    data_.hIcon = icon;
    notify_(NIM_MODIFY, &data_);
}

void TrayIcon::UpdateTooltip(std::wstring_view tooltip) {
    if (!created_) return;
    data_.uFlags = NIF_TIP;
    CopyToTip(tooltip);
    notify_(NIM_MODIFY, &data_);
}

void TrayIcon::ShowBalloon(std::wstring_view title, std::wstring_view text, DWORD infoFlags) {
    if (!created_) return;
    data_.uFlags = NIF_INFO;
    data_.dwInfoFlags = infoFlags;
    CopyToBalloonTitle(title);
    CopyToBalloonText(text);
    notify_(NIM_MODIFY, &data_);

    // Clear the local payload only. Sending NIF_INFO with empty text tells the
    // shell to remove the notification that was just displayed.
    data_.szInfo[0] = L'\0';
    data_.szInfoTitle[0] = L'\0';
}

void TrayIcon::Recreate(HICON icon, std::wstring_view tooltip) {
    if (data_.hWnd == nullptr) return;
    Create(data_.hWnd, data_.uCallbackMessage, icon, tooltip);
}

void TrayIcon::Remove() noexcept {
    if (created_) {
        notify_(NIM_DELETE, &data_);
        created_ = false;
    }
}

}  // namespace qe
