#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace qe {
// Draw at tray resolution: a stationary socket and a plug that seats or withdraws.
inline std::vector<uint32_t> ConnectionPixels(int size, bool inserting, double progress) {
    std::vector<uint32_t> pixels(size * size);
    const double ease = progress * progress * (3 - 2 * progress);
    const double gap = 4.0 * (inserting ? 1 - ease : ease);
    for (int y=0; y<size; ++y) for (int x=0; x<size; ++x) {
        unsigned a=0,r=0,g=0,b=0;
        for (int j=0;j<3;++j) for (int i=0;i<3;++i) {
            const double u=(x+(i+.5)/3)*16/size, v=(y+(j+.5)/3)*16/size;
            const double dx=(std::max)(0.,std::abs(u-8)-5.3);
            const double dy=(std::max)(0.,std::abs(v-8)-5.3);
            if (dx*dx+dy*dy>6.25) continue;
            const bool socket = u>=3 && u<=13 && v>=2 && v<=4;
            const double h=v-gap;
            const bool metal = u>=6 && u<=10 && h>=4 && h<=7;
            const bool plug = u>=4.4 && u<=11.6 && h>=7 && h<=11;
            const bool cord = u>=7 && u<=9 && h>=11;
            a+=255;
            if (socket || metal) {r+=245;g+=251;b+=249;}
            else if (plug || cord) {r+=inserting?100:255;g+=inserting?235:184;b+=inserting?190:101;}
            else {r+=inserting?15:76;g+=inserting?70:48;b+=inserting?65:33;}
        }
        pixels[y*size+x]=((a/9)<<24)|((r/9)<<16)|((g/9)<<8)|(b/9);
    }
    return pixels;
}

inline HICON ConnectionFrame(int size,bool inserting,double progress) {
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=size; info.bmiHeader.biHeight=-size;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* bits=nullptr;
    HBITMAP color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if (!color) return nullptr;
    auto pixels=ConnectionPixels(size,inserting,progress);
    CopyMemory(bits,pixels.data(),pixels.size()*sizeof(uint32_t));
    std::vector<BYTE> zeros(((size+15)/16)*2*size,0);
    HBITMAP mask=CreateBitmap(size,size,1,1,zeros.data());
    ICONINFO ii{TRUE,0,0,mask,color};
    HICON icon=mask ? CreateIconIndirect(&ii) : nullptr;
    if (mask) DeleteObject(mask);
    DeleteObject(color);
    return icon;
}
}
