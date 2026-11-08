#pragma once
#include <windows.h>
#include <deque>
#include <string>
#include <algorithm>
#include <cmath>
#include "ui/ConnectionFrame.h"

namespace qe {
// One passive desktop window per notice card. The card renders into a
// per-pixel alpha surface (UpdateLayeredWindow): a rounded card with a soft
// drop shadow that the classic layered-window alpha cannot express. Events
// arriving together are displayed in order.
class ConnectionToast {
    struct Event { bool inserted, dark, motion; std::wstring title, name; };
    HWND window_{};
    std::deque<Event> queue_;
    Event event_{};
    ULONGLONG start_{};
    int left_{}, top_{}, width_{}, height_{};  // card rectangle on the monitor
    int pad_{};                                // shadow margin around the card
    UINT dpi_=96;
    int px(int n) const { return MulDiv(n,dpi_,96); }
    static LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<ConnectionToast*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if (m==WM_NCCREATE) {
            self=static_cast<ConnectionToast*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (self && m==WM_TIMER && wp==1) { self->Tick(); return 0; }
        if (m==WM_PAINT) { ValidateRect(w,nullptr); return 0; }
        if (m==WM_ERASEBKGND) return 1;
        if (m==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (m==WM_NCHITTEST) return HTTRANSPARENT;
        return DefWindowProcW(w,m,wp,lp);
    }
    // Signed distance to a rounded rectangle centred on the origin.
    static double RoundedDistance(double x,double y,double halfW,double halfH,double radius) {
        const double qx=std::fabs(x)-(halfW-radius), qy=std::fabs(y)-(halfH-radius);
        const double ax=qx>0?qx:0, ay=qy>0?qy:0;
        return std::sqrt(ax*ax+ay*ay)+std::min(std::max(qx,qy),0.0)-radius;
    }
    // Composites shadow, card and lettering into one premultiplied ARGB
    // surface and hands it to the window. opacity fades the whole card.
    void Render(double opacity) {
        const int W=width_+2*pad_, H=height_+2*pad_;
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=W; info.bmiHeader.biHeight=-H;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        HDC screen=GetDC(nullptr);
        HDC dc=CreateCompatibleDC(screen);
        void* bits=nullptr;
        HBITMAP bitmap=dc?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
        if (!dc || !bitmap || !bits) {
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            ReleaseDC(nullptr,screen);
            return;
        }
        auto oldBitmap=SelectObject(dc,bitmap);
        auto* pixel=static_cast<uint32_t*>(bits);
        ZeroMemory(bits,static_cast<size_t>(W)*H*4);
        // Shadow: the card shape pushed down a little, blurred and dimmed.
        // Software drawing because GDI cannot write per-pixel alpha.
        const int radius=px(14);
        const double halfW=width_/2.0, halfH=height_/2.0;
        const double blur=px(13), lift=px(4);
        for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
            const double d=RoundedDistance(x-pad_-halfW+0.5, y-pad_-halfH+0.5+lift, halfW, halfH, radius);
            double s=1.0-(d-px(1))/blur;
            if (s<=0) continue;
            if (s>1) s=1;
            s*=s;  // quadratic falloff reads as ambient depth, not an outline
            pixel[y*W+x]=static_cast<uint32_t>(82*s)<<24;
        }
        // Card: clipped to the rounded shape so the corners let the shadow through.
        auto region=CreateRoundRectRgn(pad_,pad_,pad_+width_+1,pad_+height_+1,radius*2,radius*2);
        if (region) SelectClipRgn(dc,region);
        const bool dark=event_.dark;
        const COLORREF bg=dark?RGB(25,36,41):RGB(248,252,250);
        const COLORREF accent=event_.inserted?(dark?RGB(122,228,190):RGB(10,106,82)):
            (dark?RGB(255,191,117):RGB(148,78,13));
        auto fill=[&](RECT r,COLORREF c){SetDCBrushColor(dc,c);FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));};
        fill({pad_,pad_,pad_+width_,pad_+height_},bg);
        fill({pad_,pad_,pad_+px(4),pad_+height_},accent);
        const auto elapsed=GetTickCount64()-start_;
        auto icon=ConnectionFrame(px(39),event_.inserted,event_.motion?(std::min)(1.,elapsed/560.):1.);
        if(icon){DrawIconEx(dc,pad_+px(18),pad_+px(26),icon,px(39),px(39),0,nullptr,DI_NORMAL);DestroyIcon(icon);}
        SetBkMode(dc,TRANSPARENT);
        auto label=[&](const std::wstring& s,int y,int size,int weight,COLORREF color) {
            HFONT f=CreateFontW(-px(size),0,0,0,weight,0,0,0,DEFAULT_CHARSET,0,0,
                                ANTIALIASED_QUALITY,0,L"Microsoft YaHei UI");
            HGDIOBJ prior=f?SelectObject(dc,f):nullptr;
            SetTextColor(dc,color);
            RECT r{pad_+px(71),pad_+px(y),pad_+width_-px(16),pad_+px(y+size+7)};
            DrawTextW(dc,s.c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
            if(f){SelectObject(dc,prior);DeleteObject(f);}
        };
        label(L"USB-Switch",8,12,FW_SEMIBOLD,accent);
        label(event_.title,27,19,FW_SEMIBOLD,dark?RGB(243,251,248):RGB(28,44,47));
        label(event_.name,58,14,FW_NORMAL,dark?RGB(192,211,208):RGB(76,96,95));
        if (region) { SelectClipRgn(dc,nullptr); DeleteObject(region); }
        // Card coverage: mark the rounded interior opaque; GDI left the alpha
        // channel at zero, and the anti-aliased edge blends over the shadow.
        for (int y=pad_;y<pad_+height_;++y) for (int x=pad_;x<pad_+width_;++x) {
            const double d=RoundedDistance(x-pad_-halfW+0.5, y-pad_-halfH+0.5, halfW, halfH, radius);
            auto& p=pixel[y*W+x];
            if (d<=0) p|=0xFF000000u;
            else if (d<1 && (p>>24)<2) p=(static_cast<uint32_t>(255*(1-d))<<24)|(p&0xFFFFFF);
        }
        // Whole-card fade for the entrance and exit phases (premultiplied:
        // every channel scales together).
        if (opacity<1) {
            if (opacity<0) opacity=0;
            const double f=std::min(opacity,1.0);
            for (int i=0;i<W*H;++i) {
                const uint32_t p=pixel[i];
                if (!p) continue;
                pixel[i]=static_cast<uint32_t>((p>>24)*f)<<24
                    |static_cast<uint32_t>(((p>>16)&255)*f)<<16
                    |static_cast<uint32_t>(((p>>8)&255)*f)<<8
                    |static_cast<uint32_t>((p&255)*f);
            }
        }
        POINT src{};
        SIZE size{W,H};
        BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        UpdateLayeredWindow(window_,screen,nullptr,&size,dc,&src,0,&blend,ULW_ALPHA);
        SelectObject(dc,oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr,screen);
    }
    bool Begin() {
        if(queue_.empty())return false;
        event_=queue_.front();
        MONITORINFO mi{sizeof(MONITORINFO),{},{},0};
        if(!GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&mi))return false;
        // Created/kept at (0,0), i.e. on the primary monitor this toast is
        // shown on, so GetDpiForWindow (PerMonitorV2) already measures the
        // DPI of the monitor the card will appear on.
        dpi_=GetDpiForWindow(window_); if(!dpi_)dpi_=96;
        pad_=px(24);
        width_=(std::min)(px(340),int(mi.rcWork.right-mi.rcWork.left-2*pad_)); height_=px(91);
        left_=(std::max)(int(mi.rcWork.left),int(mi.rcWork.right)-width_-pad_-px(14));
        top_=mi.rcWork.bottom-height_-pad_-px(14);
        SetWindowTextW(window_,(event_.title+L" — "+event_.name).c_str());
        start_=GetTickCount64();
        SetWindowPos(window_,HWND_TOPMOST,left_-pad_,top_-pad_,width_+2*pad_,height_+2*pad_,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if(!SetTimer(window_,1,32,nullptr))return false;
        // Committed only once the card is actually going to show, so a failed
        // Begin leaves the event queued for the next attempt.
        queue_.pop_front();
        Tick();
        return true;
    }
    void Tick() {
        const auto elapsed=GetTickCount64()-start_;
        if(elapsed>=2900) {
            KillTimer(window_,1);
            ShowWindow(window_,SW_HIDE);
            start_=0;
            // A failed Begin keeps the queue and the hidden window; the next
            // Show() retries instead of dropping the remaining notices.
            if(!queue_.empty()) Begin();
            return;
        }
        // Keep the card above other topmost windows without stealing focus.
        SetWindowPos(window_,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        double opacity=1;
        if(elapsed<220) opacity=elapsed/220.0;
        else if(elapsed>2640) opacity=(2900-elapsed)/260.0;
        Render(opacity);
        // Animated draws tick until the plug has seated, then rest; stationary
        // cards only tick through the fade windows.
        if(event_.motion) {
            if(elapsed>=650 && elapsed<2640) SetTimer(window_,1,UINT(2640-elapsed),nullptr);
            else SetTimer(window_,1,32,nullptr);
        } else {
            if(elapsed<300) SetTimer(window_,1,32,nullptr);
            else if(elapsed<2640) SetTimer(window_,1,UINT(2640-elapsed),nullptr);
            else SetTimer(window_,1,32,nullptr);
        }
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
        // False lets the caller fall back to a balloon for this event; queued
        // events stay pending for the next Show().
        if(!start_ && !Begin()) return false;
        return true;
    }
};
}
