#pragma once
#include <windows.h>
#include <deque>
#include <string>
#include <algorithm>
#include "ui/ConnectionFrame.h"

namespace qe {
// One passive desktop window. Events arriving together are displayed in order.
class ConnectionToast {
    struct Event { bool inserted, dark, motion; std::wstring title, name; };
    HWND window_{};
    std::deque<Event> queue_;
    Event event_{};
    ULONGLONG start_{};
    int left_{}, top_{}, width_{}, height_{};
    UINT dpi_=96;
    int px(int n) const { return MulDiv(n,dpi_,96); }
    static LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<ConnectionToast*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if (m==WM_NCCREATE) {
            self=static_cast<ConnectionToast*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (self && m==WM_TIMER && wp==1) { self->Tick(); return 0; }
        if (self && m==WM_PAINT) { self->Paint(w); return 0; }
        if (m==WM_ERASEBKGND) return 1;
        if (m==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (m==WM_NCHITTEST) return HTTRANSPARENT;
        return DefWindowProcW(w,m,wp,lp);
    }
    void Paint(HWND w) {
        PAINTSTRUCT ps{}; HDC screen=BeginPaint(w,&ps);
        HDC dc=CreateCompatibleDC(screen);
        HBITMAP bmp=dc ? CreateCompatibleBitmap(screen,width_,height_) : nullptr;
        HGDIOBJ old=bmp ? SelectObject(dc,bmp) : nullptr;
        if (!old) { if(dc) DeleteDC(dc); dc=screen; }
        const bool dark=event_.dark;
        const COLORREF bg=dark ? RGB(25,36,41) : RGB(248,252,250);
        const COLORREF accent=event_.inserted ? (dark?RGB(122,228,190):RGB(10,106,82)) :
            (dark?RGB(255,191,117):RGB(148,78,13));
        auto fill=[&](RECT r,COLORREF c){SetDCBrushColor(dc,c);FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));};
        fill({0,0,width_,height_},bg);
        fill({0,0,px(4),height_},accent);
        const auto elapsed=GetTickCount64()-start_;
        auto icon=ConnectionFrame(px(39),event_.inserted,event_.motion ? (std::min)(1.,elapsed/560.) : 1.);
        if(icon){DrawIconEx(dc,px(18),px(26),icon,px(39),px(39),0,nullptr,DI_NORMAL);DestroyIcon(icon);}
        SetBkMode(dc,TRANSPARENT);
        auto label=[&](const std::wstring& s,int y,int size,int weight,COLORREF color) {
            HFONT f=CreateFontW(-px(size),0,0,0,weight,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            HGDIOBJ prior=f?SelectObject(dc,f):nullptr;
            SetTextColor(dc,color);
            RECT r{px(71),px(y),width_-px(16),px(y+size+7)};
            DrawTextW(dc,s.c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
            if(f){SelectObject(dc,prior);DeleteObject(f);}
        };
        label(L"USB-Switch",8,12,FW_SEMIBOLD,accent);
        label(event_.title,27,19,FW_SEMIBOLD,dark?RGB(243,251,248):RGB(28,44,47));
        label(event_.name,58,14,FW_NORMAL,dark?RGB(192,211,208):RGB(76,96,95));
        if(old){BitBlt(screen,0,0,width_,height_,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteDC(dc);}
        if(bmp)DeleteObject(bmp);
        EndPaint(w,&ps);
    }
    bool Begin() {
        event_=std::move(queue_.front()); queue_.pop_front();
        MONITORINFO mi{sizeof(MONITORINFO),{},{},0};
        if(!GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&mi))return false;
        dpi_=GetDpiForWindow(window_); if(!dpi_)dpi_=96;
        width_=(std::min)(px(340),int(mi.rcWork.right-mi.rcWork.left)); height_=px(91);
        left_=(std::max)(int(mi.rcWork.left),int(mi.rcWork.right)-width_-px(14));
        top_=mi.rcWork.bottom-height_-px(14);
        auto region=CreateRoundRectRgn(0,0,width_+1,height_+1,px(19),px(19));
        if(region && !SetWindowRgn(window_,region,FALSE)) DeleteObject(region);
        start_=GetTickCount64();
        SetWindowTextW(window_,(event_.title+L" — "+event_.name).c_str());
        if(!SetTimer(window_,1,event_.motion?32:2900,nullptr))return false;
        Tick();
        return true;
    }
    void Tick() {
        const auto elapsed=GetTickCount64()-start_;
        if(elapsed>=2900) {
            KillTimer(window_,1);
            ShowWindow(window_,SW_HIDE);
            start_=0;
            if(!queue_.empty() && !Begin()) Clear();
            return;
        }
        double opacity=1;
        if(event_.motion) opacity=(std::min)({1.,elapsed/200.,(2900-elapsed)/260.});
        SetLayeredWindowAttributes(window_,0,BYTE(255*opacity),LWA_ALPHA);
        SetWindowPos(window_,HWND_TOPMOST,left_,top_+int(px(12)*(1-opacity)),width_,height_,
                     SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if(elapsed<650 || !event_.motion) InvalidateRect(window_,nullptr,FALSE);
        // No animation work while the card is stationary.
        if(event_.motion && elapsed>=650 && elapsed<2640) SetTimer(window_,1,UINT(2640-elapsed),nullptr);
        else if(event_.motion && elapsed>=2640) SetTimer(window_,1,32,nullptr);
    }
public:
    ConnectionToast()=default;
    ConnectionToast(const ConnectionToast&)=delete;
    ~ConnectionToast(){Clear();}
    void Clear(){
        if(window_)DestroyWindow(window_);
        window_=nullptr;start_=0;queue_.clear();
    }
    bool Show(HWND owner,bool inserted,bool dark,bool motion,std::wstring title,std::wstring name) {
        if(!window_) {
            WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);
            cls.lpszClassName=L"USBSwitchConnectionToast";
            if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
            window_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT,
                cls.lpszClassName,L"",WS_POPUP,0,0,1,1,owner,nullptr,cls.hInstance,this);
            if(!window_)return false;
        }
        if(queue_.size()>=5)queue_.pop_front();
        queue_.push_back({inserted,dark,motion,std::move(title),std::move(name)});
        if(!start_ && !Begin()){Clear();return false;}
        return true;
    }
};
}
