#pragma once
#include "core/Device.h"

namespace qe {
// Explicit media operation; never substitutes a PnP removal.
struct MediaResult { const wchar_t* stage; DWORD error; };
MediaResult EjectMedia(const Device& device);
}
