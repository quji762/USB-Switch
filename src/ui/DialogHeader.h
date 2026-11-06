#pragma once
#include <windows.h>
#include "app/AppInfo.h"
#include "resource.h"

namespace qe {
// Only the noninteractive banner is drawn here. Buttons, fields and tab order
// remain standard Windows controls.
inline bool PaintDialogHeader(UINT message, LPARAM lp) {
    if (message != WM_DRAWITEM) return false;
    auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
    if (!item || item->CtlID != IDC_DIALOG_HEADER) return false;
    HDC dc = item->hDC;
    const RECT r = item->rcItem;
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW),0,nullptr};
    const bool hc = SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) &&
                    (contrast.dwFlags & HCF_HIGHCONTRASTON);
    const auto px = [&](int n){ return MulDiv(n, r.bottom-r.top, 76); };
    const int saved = SaveDC(dc);
    SetDCBrushColor(dc,hc ? GetSysColor(COLOR_WINDOW) : RGB(20,58,68));
    FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(dc,TRANSPARENT);
    const COLORREF main = hc ? GetSysColor(COLOR_WINDOWTEXT) : RGB(250,255,255);
    SetTextColor(dc,hc ? main : RGB(163,222,210));
    LOGFONTW lf{};
    lf.lfHeight = -px(13); lf.lfWeight = FW_SEMIBOLD;
    lstrcpyW(lf.lfFaceName,L"Segoe UI");
    HFONT small = CreateFontIndirectW(&lf);
    if (small) SelectObject(dc,small);
    RECT brand{px(22),0,r.right-px(22),px(33)};
    DrawTextW(dc,L"USB-Switch",-1,&brand,DT_SINGLELINE|DT_BOTTOM|DT_NOPREFIX);
    RECT version{px(22),0,r.right-px(22),r.bottom};
    DrawTextW(dc,L"v" QUICKEJECT_VERSION_STRING_W,-1,&version,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
    lf.lfHeight = -px(23);
    HFONT large = CreateFontIndirectW(&lf);
    if (large) SelectObject(dc,large);
    SetTextColor(dc,main);
    WCHAR title[192]{};
    GetWindowTextW(item->hwndItem,title,192);
    RECT heading{px(22),px(34),r.right-px(86),r.bottom-px(5)};
    DrawTextW(dc,title,-1,&heading,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    RestoreDC(dc,saved);
    if (large) DeleteObject(large);
    if (small) DeleteObject(small);
    return true;
}
}
