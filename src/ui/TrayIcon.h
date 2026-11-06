#pragma once
// Shell tray icon wrapper.
//
// Uses NOTIFYICON_VERSION_4 (the modern behavior: the system owns balloon
// timeouts and accessibility). The icon handle itself stays owned by the
// caller so it can be reloaded on DPI changes.

#include <string>
#include <string_view>

#include <windows.h>
#include <shellapi.h>  // NOTIFYICONDATAW, Shell_NotifyIconW, NIIF_*

namespace qe {

class TrayIcon {
public:
    static constexpr UINT kIconId = 1;
    using NotifyFn = BOOL (WINAPI *)(DWORD, PNOTIFYICONDATAW);
    explicit TrayIcon(NotifyFn notify = Shell_NotifyIconW) : notify_(notify) {}

    bool Create(HWND hwnd, UINT callbackMessage, HICON icon, std::wstring_view tooltip);
    void UpdateIcon(HICON icon);
    void UpdateTooltip(std::wstring_view tooltip);
    void ShowBalloon(std::wstring_view title, std::wstring_view text,
                     DWORD infoFlags = NIIF_INFO);
    bool Recreate(HICON icon, std::wstring_view tooltip);
    void Remove() noexcept;

    bool IsCreated() const noexcept { return created_; }

private:
    void CopyToTip(std::wstring_view tooltip);
    void CopyToBalloonTitle(std::wstring_view title);
    void CopyToBalloonText(std::wstring_view text);

    NOTIFYICONDATAW data_{};
    NotifyFn notify_;
    bool created_ = false;
};

}  // namespace qe
