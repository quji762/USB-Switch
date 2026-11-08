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

// ---------------------------------------------------------------------------
// Design tokens. The dialog painter and the toast card share one vocabulary:
// a teal accent, a cool near-black paper, one outline color per state and a
// single corner-radius scale (buttons 5, cards 14, dialog frames via DWM).
// ---------------------------------------------------------------------------
inline COLORREF AccentColor(bool dark) { return dark ? RGB(23,140,118) : RGB(13,106,93); }

// One rounded rectangle with an optional outline; the shared drawing primitive
// for buttons and rules. radius 0 means square corners.
inline void FillRoundRect(HDC dc, const RECT& box, int radius, COLORREF fill, COLORREF border) {
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, border);
    if (radius > 0) RoundRect(dc, box.left, box.top, box.right, box.bottom, radius * 2, radius * 2);
    else {
        FillRect(dc, &box, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if (border != fill) {
            auto pen = CreatePen(PS_SOLID, 1, border);
            auto old = SelectObject(dc, pen);
            HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            auto oldBrush = SelectObject(dc, hollow);
            Rectangle(dc, box.left, box.top, box.right, box.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }
}

// Per-state button skin. Light and dark share the same structure: one surface,
// one outline, one ink, brightened on hover and dimmed while pressed.
struct ButtonSkin { COLORREF fill, border, text; };
inline ButtonSkin ButtonColors(bool dark, bool primary, bool hover, bool pressed, bool disabled) {
    if (primary) {
        if (disabled) return dark ? ButtonSkin{RGB(31,43,47), RGB(31,43,47), RGB(96,114,116)}
                                  : ButtonSkin{RGB(210,225,222), RGB(210,225,222), RGB(252,252,252)};
        if (pressed) return dark ? ButtonSkin{RGB(10,88,76), RGB(10,88,76), RGB(240,252,249)}
                                 : ButtonSkin{RGB(9,84,74), RGB(9,84,74), RGB(244,252,250)};
        if (hover) return dark ? ButtonSkin{RGB(26,128,108), RGB(26,128,108), RGB(244,252,250)}
                               : ButtonSkin{RGB(16,122,107), RGB(16,122,107), RGB(255,255,255)};
        return dark ? ButtonSkin{RGB(18,106,90), RGB(18,106,90), RGB(238,252,248)}
                    : ButtonSkin{RGB(13,106,93), RGB(13,106,93), RGB(255,255,255)};
    }
    if (disabled) return dark ? ButtonSkin{RGB(31,41,46), RGB(46,58,62), RGB(104,120,122)}
                              : ButtonSkin{RGB(247,250,249), RGB(223,231,229), RGB(150,163,166)};
    if (pressed) return dark ? ButtonSkin{RGB(32,43,48), RGB(88,108,112), darkInk}
                             : ButtonSkin{RGB(226,238,235), RGB(146,168,164), RGB(29,49,58)};
    if (hover) return dark ? ButtonSkin{RGB(47,61,67), RGB(98,122,126), darkInk}
                           : ButtonSkin{RGB(240,247,246), AccentColor(false), RGB(29,49,58)};
    return dark ? ButtonSkin{RGB(40,53,58), RGB(70,88,92), darkInk}
                : ButtonSkin{RGB(255,255,255), RGB(199,211,208), RGB(29,49,58)};
}

// Hover/focus tracking for owner-drawn buttons: the control reports press and
// focus states through DRAWITEMSTRUCT, but hovering must be observed here.
inline LRESULT CALLBACK ButtonSubclass(HWND wnd, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_MOUSEMOVE:
            if (!GetPropW(wnd, L"USB.Hover")) {
                SetPropW(wnd, L"USB.Hover", reinterpret_cast<HANDLE>(1));
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, wnd, 0};
                TrackMouseEvent(&track);
                InvalidateRect(wnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            RemovePropW(wnd, L"USB.Hover");
            InvalidateRect(wnd, nullptr, FALSE);
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(wnd, nullptr, FALSE);
            break;
        case WM_ENABLE:
            if (!wp) RemovePropW(wnd, L"USB.Hover");
            InvalidateRect(wnd, nullptr, FALSE);
            break;
        default:
            break;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

// Turns the dialog's push buttons into rounded, hover-aware owner-drawn ones
// (native rendering stays in charge under high contrast). The default-button
// flag is read from the template style before it is replaced by BS_OWNERDRAW,
// so Enter keeps activating the same button as before. mode: 0 = light owner
// draw, 1 = dark owner draw, 2 = high contrast (native buttons restored).
inline void ConvertDialogButtons(HWND hwnd, bool dark, bool hc) {
    const LPARAM mode = hc ? 2 : (dark ? 1 : 0);
    EnumChildWindows(hwnd, [](HWND child, LPARAM modeValue) -> BOOL {
        const int mode = static_cast<int>(modeValue);
        const bool dark = mode == 1, hc = mode == 2;
        wchar_t cls[32]{};
        if (GetClassNameW(child, cls, 32) && !_wcsicmp(cls, L"Button")) {
            const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
            const LONG_PTR type = style & BS_TYPEMASK;
            const bool push = type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON;
            if (!hc) {  // owner-draw rendering
                if (push) {
                    SetPropW(child, L"USB.Type", reinterpret_cast<HANDLE>(type + 1));
                    SetPropW(child, L"USB.Primary",
                             reinterpret_cast<HANDLE>(type == BS_DEFPUSHBUTTON ? 1 : 0));
                    SetWindowLongPtrW(child, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
                    SetWindowSubclass(child, ButtonSubclass, 1, 0);
                } else {
                    // Checkboxes and radios keep native drawing; without the
                    // dark theme their labels render in near-black ink and
                    // vanish into the dark paper.
                    SetWindowTheme(child, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
                }
            } else if (push) {  // restore native buttons (high contrast)
                const UINT_PTR saved = reinterpret_cast<UINT_PTR>(GetPropW(child, L"USB.Type"));
                if (saved) {
                    const LONG_PTR native = saved == 2 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON;
                    SetWindowLongPtrW(child, GWL_STYLE, (style & ~BS_TYPEMASK) | native);
                    RemovePropW(child, L"USB.Type");
                    RemovePropW(child, L"USB.Primary");
                    RemovePropW(child, L"USB.Hover");
                    RemoveWindowSubclass(child, ButtonSubclass, 1);
                    SetWindowTheme(child, nullptr, nullptr);
                }
            }
        } else if (!_wcsicmp(cls, L"ComboBox") || !_wcsicmp(cls, L"Edit")) {
            SetWindowTheme(child, dark ? (!_wcsicmp(cls, L"ComboBox") ? L"DarkMode_CFD" : L"DarkMode_Explorer")
                                       : nullptr, nullptr);
        } else if (!_wcsicmp(cls, L"SysLink")) {
            // Without the dark theme the link draws in dark blue and disappears
            // into the dark paper.
            SetWindowTheme(child, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        } else if (!_wcsicmp(cls, L"SysTreeView32")) {
            const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
            if (style & WS_BORDER) SetWindowLongPtrW(child, GWL_STYLE, style & ~WS_BORDER);
        }
        return TRUE;
    }, mode);
}

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
    DwmSetWindowAttribute(hwnd,19,&dark,sizeof(dark));  // older Windows builds use 19
    // Windows 11 rounds the frame and adds its matching soft shadow; Windows 10
    // ignores the attribute and keeps the square frame. High contrast stays
    // square on purpose.
    DWORD corner = hc ? 1 /*DONOTROUND*/ : 2 /*ROUND*/;
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    ConvertDialogButtons(hwnd, dark != FALSE, hc);
    KillTimer(hwnd,kLightTimer);
    RemovePropW(hwnd,L"USB.LightStart");
    BOOL motion = FALSE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);
    const bool canMove = !hc && motion != FALSE;
    if (animate && canMove && !IsWindowVisible(hwnd)) {
        if (!AnimateWindow(hwnd, 140, AW_BLEND | AW_ACTIVATE)) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    if (animate && canMove && SetTimer(hwnd,kLightTimer,32,nullptr))
        SetPropW(hwnd,L"USB.LightStart",reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(GetTickCount() ? GetTickCount() : 1u)));
    RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}

// Owner-drawn push buttons: rounded surface, hover/press/disabled states and a
// keyboard focus ring; the dialog's primary (default) button carries the
// accent fill.
inline void PaintDialogButton(const DRAWITEMSTRUCT* item) {
    const HWND dialog = GetParent(item->hwndItem);
    const bool hc = HighContrast(), dark = DialogDark(dialog);
    const int dpi = GetDpiForWindow(item->hwndItem);
    const auto px = [&](int n){ return MulDiv(n, dpi ? dpi : 96, 96); };
    const bool primary = GetPropW(item->hwndItem, L"USB.Primary") != nullptr;
    const bool hover = GetPropW(item->hwndItem, L"USB.Hover") != nullptr;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool focused = (item->itemState & ODS_FOCUS) != 0;
    HDC dc = item->hDC;
    const RECT r = item->rcItem;
    // Corners of the rounded button show the dialog paper, not black.
    const auto paper = dark ? darkPaper : GetSysColor(COLOR_WINDOW);
    auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, hc ? GetSysColor(COLOR_BTNFACE) : paper);
    FillRect(dc, &r, brush);
    const auto skin = hc ? ButtonSkin{GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_WINDOWFRAME),
                                      GetSysColor(COLOR_BTNTEXT)}
                         : ButtonColors(dark, primary, hover, pressed, disabled);
    RECT box{r.left + px(1), r.top + px(1), r.right - px(1), r.bottom - px(1)};
    const int radius = px(5);
    FillRoundRect(dc, box, radius, skin.fill, skin.border);
    if (focused && !disabled) {
        // A thin keyboard-focus ring just inside the border; on the accent
        // filled primary it takes the ink color so it stays visible.
        auto pen = CreatePen(PS_SOLID, px(1), primary ? skin.text : AccentColor(dark));
        auto oldPen = SelectObject(dc, pen);
        auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, box.left + px(2), box.top + px(2), box.right - px(2), box.bottom - px(2),
                  radius, radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    wchar_t text[128]{};
    GetWindowTextW(item->hwndItem, text, 128);
    if (text[0]) {
        SetBkMode(dc, TRANSPARENT);
        HFONT base = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
        HFONT font = nullptr;
        if (base) {
            LOGFONTW log{};
            if (GetObjectW(base, sizeof(log), &log) && primary) log.lfWeight = FW_SEMIBOLD;
            font = primary ? CreateFontIndirectW(&log) : nullptr;
        }
        HGDIOBJ old = SelectObject(dc, font ? font : base);
        SetTextColor(dc, disabled ? ButtonColors(dark, false, false, false, true).text
                                  : (hc ? GetSysColor(COLOR_BTNTEXT) : skin.text));
        RECT textRect{box.left + px(8), box.top, box.right - px(8), box.bottom};
        DrawTextW(dc, text, -1, &textRect,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
        if (font) DeleteObject(font);
    }
}

inline INT_PTR ThemeDialog(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
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
    if (msg == WM_DRAWITEM) {
        // Buttons are handled centrally; banner/section/rule statics keep
        // flowing to the per-dialog header painter.
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (item && item->CtlType == ODT_BUTTON) {
            PaintDialogButton(item);
            return TRUE;
        }
        return FALSE;
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
