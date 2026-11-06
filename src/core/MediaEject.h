#pragma once
#include "core/Device.h"

namespace qe {
// Explicit media operation; never substitutes a PnP removal.
struct MediaResult { const wchar_t* stage; DWORD error; };
struct MediaIo {
    decltype(&::CreateFileW) open = ::CreateFileW;
    decltype(&::DeviceIoControl) control = ::DeviceIoControl;
};
MediaResult EjectMedia(const Device& device, const MediaIo& io = {});
}
