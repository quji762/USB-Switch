#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include "app/Settings.h"
#include "resource.h"

namespace qe {
inline bool HighContrast() {
    HIGHCONTRASTW info{sizeof(info),0,nullptr};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(info),&info,0) &&
           (info.dwFlags & HCF_HIGHCONTRASTON);
}
inline bool SystemDark() {
    DWORD value = 1, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS && !value;
}
inline constexpr UINT_PTR kLightTimer = 0x5553;
inline constexpr COLORREF darkPaper = RGB(25,33,39), darkInk = RGB(231,241,240);
inline bool DialogDark(HWND hwnd) { return GetPropW(hwnd,L"USB.Dark") != nullptr; }
inline void ApplyDialogTheme(HWND hwnd, bool animate) {
    auto mode = Settings::Instance().Data().appearance;
    if (GetDlgItem(hwnd,IDC_SET_THEME)) {
        auto index = SendDlgItemMessageW(hwnd,IDC_SET_THEME,CB_GETCURSEL,0,0);
        if (index != CB_ERR) mode = index == 2 ? L"dark" : index == 1 ? L"light" : L"auto";
    }
    const bool hc = HighContrast();
    BOOL dark = !hc && (mode == L"dark" || (mode == L"auto" && SystemDark()));
    if (dark) SetPropW(hwnd,L"USB.Dark",reinterpret_cast<HANDLE>(1));
    else RemovePropW(hwnd,L"USB.Dark");
    DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    EnumChildWindows(hwnd,[](HWND child,LPARAM d) -> BOOL {
        wchar_t cls[32]{};
        GetClassNameW(child,cls,32);
        if (!_wcsicmp(cls,L"Button") || !_wcsicmp(cls,L"ComboBox") || !_wcsicmp(cls,L"Edit"))
            SetWindowTheme(child,d ? (!_wcsicmp(cls,L"ComboBox") ? L"DarkMode_CFD" : L"DarkMode_Explorer") : nullptr,nullptr);
        return TRUE;
    },dark);
    KillTimer(hwnd,kLightTimer);
    RemovePropW(hwnd,L"USB.LightStart");
    BOOL motion = FALSE;
    if (animate && !hc && SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0) && motion &&
        SetTimer(hwnd,kLightTimer,32,nullptr))
        SetPropW(hwnd,L"USB.LightStart",reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(GetTickCount() ? GetTickCount() : 1u)));
    RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}
inline INT_PTR ThemeDialog(HWND hwnd,UINT msg,WPARAM wp,LPARAM) {
    if (msg == WM_INITDIALOG || msg == WM_SETTINGCHANGE || msg == WM_SYSCOLORCHANGE)
        ApplyDialogTheme(hwnd,msg == WM_INITDIALOG);
    if (msg == WM_TIMER && wp == kLightTimer) {
        const auto start = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(GetPropW(hwnd,L"USB.LightStart")));
        if (!start || GetTickCount() - start >= 640) {
            KillTimer(hwnd,kLightTimer);
            RemovePropW(hwnd,L"USB.LightStart");
        }
        InvalidateRect(GetDlgItem(hwnd,IDC_DIALOG_HEADER),nullptr,FALSE);
        return TRUE;
    }
    if (msg == WM_NCDESTROY) {
        KillTimer(hwnd,kLightTimer);
        RemovePropW(hwnd,L"USB.Dark");
        RemovePropW(hwnd,L"USB.LightStart");
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORLISTBOX || msg == WM_CTLCOLORBTN) {
        bool dark = DialogDark(hwnd);
        static const struct Brush { HBRUSH h=CreateSolidBrush(darkPaper); ~Brush(){DeleteObject(h);} } brush;
        SetBkColor(reinterpret_cast<HDC>(wp),dark ? darkPaper : GetSysColor(COLOR_WINDOW));
        SetTextColor(reinterpret_cast<HDC>(wp),dark ? darkInk : GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<INT_PTR>(dark ? brush.h : GetSysColorBrush(COLOR_WINDOW));
    }
    return FALSE;
}
}
