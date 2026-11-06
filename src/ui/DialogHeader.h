#pragma once
#include <windows.h>
#include <cmath>
#include "ui/DialogTheme.h"
#include "app/AppInfo.h"
#include "resource.h"

namespace qe {
inline bool PaintDialogHeader(UINT message, LPARAM lp) {
    if (message != WM_DRAWITEM) return false;
    auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
    if (!item) return false;
    const bool banner = item->CtlID == IDC_DIALOG_HEADER;
    const bool section = item->CtlID == IDC_SET_BEHAVIOR || item->CtlID == IDC_SET_INPUT;
    const bool rule = item->CtlID == IDC_SET_DIVIDER || item->CtlID == IDC_SET_FOOTER;
    if (!banner && !section && !rule) return false;
    HDC dc = item->hDC;
    RECT r = item->rcItem;
    HWND dialog = GetParent(item->hwndItem);
    const bool hc = HighContrast(), dark = DialogDark(dialog);
    const auto ink = hc ? GetSysColor(COLOR_WINDOWTEXT) : dark ? darkInk : RGB(29,49,58);
    const auto accent = hc ? ink : dark ? RGB(128,224,197) : RGB(13,106,93);
    const auto paper = dark ? darkPaper : GetSysColor(COLOR_WINDOW);
    const auto muted = hc ? ink : dark ? RGB(180,208,204) : RGB(78,100,110);
    const auto line = hc ? ink : dark ? RGB(50,69,74) : RGB(218,229,226);
    // Composite the moving highlight and lettering before presenting the frame.
    HDC back = banner ? CreateCompatibleDC(dc) : nullptr;
    HBITMAP bitmap = back ? CreateCompatibleBitmap(dc,r.right,r.bottom) : nullptr;
    HGDIOBJ original = bitmap ? SelectObject(back,bitmap) : nullptr;
    if (original) dc = back;
    const int saved = SaveDC(dc);
    auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    const auto fill = [&](RECT box,COLORREF c){ SetDCBrushColor(dc,c); FillRect(dc,&box,brush); };
    fill(r, rule ? line : banner && !hc ? RGB(240,247,245) : paper);
    if (rule) { RestoreDC(dc,saved); return true; }
    const auto px = [&](int n){ return MulDiv(n,r.bottom-r.top,banner ? 76 : 58); };
    SetBkMode(dc,TRANSPARENT);
    auto label = [&](const wchar_t* text, RECT box, int size, int weight, COLORREF color, UINT align) {
        HFONT font = CreateFontW(-px(size),0,0,0,weight,0,0,0,DEFAULT_CHARSET,0,0,
                                  CLEARTYPE_QUALITY,0,L"Segoe UI");
        HGDIOBJ old = font ? SelectObject(dc,font) : nullptr;
        SetTextColor(dc,color);
        DrawTextW(dc,text,-1,&box,align|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        if (font) { SelectObject(dc,old); DeleteObject(font); }
    };
    WCHAR title[192]{};
    GetWindowTextW(item->hwndItem,title,192);
    if (banner) {
        if (!hc && r.right > 0) {
            const auto start = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(GetPropW(dialog,L"USB.LightStart")));
            const double t = start ? (GetTickCount() - start) / 640.0 : 1.0;
            // A single highlight travels across the header and settles at its right edge.
            const double center = .78 - .66 * std::pow(1.0 - (t < 1.0 ? t : 1.0), 3);
            for (int x = 0; x < r.right; x += 3) {
                double u = (double(x) / r.right - center) * 3.4;
                double g = std::exp(-u*u);
                COLORREF color = dark ? RGB(27+int(5*g),46+int(38*g),49+int(31*g))
                                      : RGB(238-int(35*g),246-int(8*g),243-int(17*g));
                fill({x,0,x+3,r.bottom},color);
            }
        }
        SelectObject(dc,GetStockObject(NULL_PEN));
        SelectObject(dc,brush);
        SetDCBrushColor(dc,hc ? ink : RGB(23,82,75));
        RoundRect(dc,px(19),px(16),px(64),px(61),px(11),px(11));
        const auto glyph = hc ? paper : RGB(230,252,244);
        fill({px(36),px(25),px(47),px(35)},glyph);
        const auto shell = hc ? ink : RGB(23,82,75);
        fill({px(38),px(28),px(40),px(31)},shell);
        fill({px(43),px(28),px(45),px(31)},shell);
        fill({px(33),px(36),px(50),px(49)},glyph);
        fill({px(39),px(50),px(44),px(53)},glyph);
        label(L"USB-Switch",{px(79),px(10),r.right-px(70),px(32)},13,FW_SEMIBOLD,accent,DT_LEFT);
        label(title,{px(78),px(30),r.right-px(70),px(65)},24,FW_SEMIBOLD,ink,DT_LEFT);
        label(L"v" QUICKEJECT_VERSION_STRING_W,{r.right-px(70),0,r.right-px(18),r.bottom},13,FW_NORMAL,muted,DT_RIGHT);
        fill({0,r.bottom-1,r.right,r.bottom},line);
    } else {
        label(item->CtlID == IDC_SET_BEHAVIOR ? L"01" : L"02",{0,0,r.right,px(21)},13,FW_SEMIBOLD,accent,DT_LEFT);
        label(title,{0,px(22),r.right,r.bottom},19,FW_SEMIBOLD,ink,DT_LEFT);
    }
    RestoreDC(dc,saved);
    if (original) {
        BitBlt(item->hDC,0,0,r.right,r.bottom,back,0,0,SRCCOPY);
        SelectObject(back,original);
    }
    if (bitmap) DeleteObject(bitmap);
    if (back) DeleteDC(back);
    return true;
}
}
